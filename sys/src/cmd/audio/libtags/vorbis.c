/*
 * https://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-810005
 * https://wiki.xiph.org/VorbisComment
 */
#include "tagspriv.h"

static const struct {
	char *s;
	int type;
}t[] = {
	{"album", Talbum},
	{"title", Ttitle},
	{"artist", Tartist},
	{"tracknumber", Ttrack},
	{"date", Tdate},
	{"replaygain_track_peak", Ttrackpeak},
	{"replaygain_track_gain", Ttrackgain},
	{"r128_track_gain", Ttrackgain},
	{"replaygain_album_peak", Talbumpeak},
	{"replaygain_album_gain", Talbumgain},
	{"r128_album_gain", Talbumgain},
	{"genre", Tgenre},
	{"composer", Tcomposer},
	{"comment", Tcomment},
	{"albumartist", Talbumartist},
	{"album artist", Talbumartist}, // some legacy leftovers
};

void
cbvorbiscomment(Tagctx *ctx, char *k, char *v){
	int i;

	if(*v == 0)
		return;
	for(i = 0; i < nelem(t); i++){
		if(cistrcmp(k, t[i].s) == 0){
			txtcb(ctx, t[i].type, k, v);
			break;
		}
	}
	if(i == nelem(t))
		txtcb(ctx, Tunknown, k, v);
}

int
tagvorbis(Tagctx *ctx)
{
	char *v;
	uchar *d, h[4];
	int sz, numtags, i, npages, pgend;

	d = (uchar*)ctx->buf;
	/* need to find vorbis frame with type=3 */
	for(npages = pgend = 0; npages < 2; npages++){ /* vorbis comment is the second header */
		int nsegs;
		if(ctx->read(ctx, d, 27) != 27)
			return -1;
		if(memcmp(d, "OggS", 4) != 0)
			return -1;

		/* calculate the size of the packet */
		nsegs = d[26];
		if(ctx->read(ctx, d, nsegs+1) != nsegs+1)
			return -1;
		for(sz = i = 0; i < nsegs; sz += d[i++]);

		if(d[nsegs] == 3){ /* comment */
			/* FIXME - embedded pics make tags span multiple packets */
			pgend = ctx->seek(ctx, 0, 1) + sz;
			break;
		}
		if(d[nsegs] == 1 && sz >= 28){ /* identification */
			if(ctx->read(ctx, d, 28) != 28)
				return -1;
			sz -= 28;
			ctx->channels = d[10];
			ctx->samplerate = leuint(&d[11]);
			if((ctx->bitrate = leuint(&d[15])) == 0) /* maximum */
				ctx->bitrate = leuint(&d[19]); /* nominal */
		}

		ctx->seek(ctx, sz-1, 1);
	}

	if(npages < 3) {
		if(ctx->read(ctx, &d[1], 10) != 10 || memcmp(&d[1], "vorbis", 6) != 0)
			return -1;
		sz = leuint(&d[7]);
		if(ctx->seek(ctx, sz, 1) < 0 || ctx->read(ctx, h, 4) != 4)
			return -1;
		numtags = leuint(h);

		for(i = 0; i < numtags; i++){
			if(ctx->read(ctx, h, 4) != 4)
				return -1;
			if((sz = leuint(h)) < 0)
				return -1;
			/* FIXME - embedded pics make tags span multiple packets */
			if(pgend < ctx->seek(ctx, 0, 1)+sz)
				break;

			if(sz > ctx->bufsz-1){
				if(ctx->seek(ctx, sz, 1) < 0)
					return -1;
				continue;
			}
			if(ctx->read(ctx, ctx->buf, sz) != sz)
				return -1;
			ctx->buf[sz] = 0;

			if((v = strchr(ctx->buf, '=')) == nil)
				return -1;
			*v++ = 0;
			cbvorbiscomment(ctx, ctx->buf, v);
		}
	}

	/* calculate the duration */
	if(ctx->samplerate > 0){
		uvlong first = 0;
		sz = ctx->bufsz <= 4096 ? ctx->bufsz : 4096;
		ctx->seek(ctx, -sz, 1);
		ctx->seek(ctx, 16, 1);
		for(i = -sz; i < 65536; i += sz - 16){
			if(ctx->seek(ctx, sz - 16, 1) <= 0)
				break;
			v = ctx->buf;
			if(ctx->read(ctx, v, sz) != sz)
				break;
			for(; v != nil && v < ctx->buf+sz;){
				v = memchr(v, 'O', ctx->buf+sz - v - 14);
				if(v != nil && v[1] == 'g' && v[2] == 'g' && v[3] == 'S'){
					first = leuint(v+6) | (uvlong)leuint(v+10)<<32;
					goto found;
				}
				if(v != nil)
					v++;
			}
		}

found:
		for(i = sz; i < 65536+16; i += sz - 16){
			if(ctx->seek(ctx, -i, 2) <= 0)
				break;
			v = ctx->buf;
			if(ctx->read(ctx, v, sz) != sz)
				break;
			for(; v != nil && v < ctx->buf+sz;){
				v = memchr(v, 'O', ctx->buf+sz - v - 14);
				if(v != nil && v[1] == 'g' && v[2] == 'g' && v[3] == 'S' && (v[5] & 4) == 4){ /* last page */
					uvlong g = leuint(v+6) | (uvlong)leuint(v+10)<<32;
					ctx->duration = (g - first) * 1000 / ctx->samplerate;
				}
				if(v != nil)
					v++;
			}
			if(ctx->duration != 0)
				break;
		}
	}

	return 0;
}
