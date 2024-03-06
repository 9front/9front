#include "tagspriv.h"

int
tagopus(Tagctx *ctx)
{
	char *v;
	uchar *d, h[4];
	int sz, numtags, i, npages, pgend;

	d = (uchar*)ctx->buf;
	for(npages = pgend = 0; npages < 2; npages++){
		int nsegs;
		if(ctx->read(ctx, d, 27) != 27)
			return -1;
		if(memcmp(d, "OggS", 4) != 0)
			return -1;

		/* calculate the size of the packet */
		nsegs = d[26];
		if(nsegs > ctx->bufsz-8 || ctx->read(ctx, d, nsegs+8) != nsegs+8)
			return -1;
		for(sz = i = 0; i < nsegs; sz += d[i++]);

		if(memcmp(&d[nsegs], "OpusHead", 8) == 0){
			if(ctx->read(ctx, d, 8) != 8 || d[0] != 1)
				return -1;
			sz -= 8;
			ctx->channels = d[1];
			ctx->samplerate = leuint(&d[4]);
		}else if(memcmp(&d[nsegs], "OpusTags", 8) == 0){
			/* FIXME - embedded pics make tags span multiple packets */
			pgend = ctx->seek(ctx, 0, 1) + sz;
			break;
		}

		ctx->seek(ctx, sz-8, 1);
	}

	if(npages < 3){
		if(ctx->read(ctx, d, 4) != 4)
			return -1;
		sz = leuint(d);
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
		/* go back a bit but make sure first page is skipped */
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
				if(v != nil && v[1] == 'g' && v[2] == 'g' && v[3] == 'S'){
					uvlong g = leuint(v+6) | (uvlong)leuint(v+10)<<32;
					ctx->duration = (g - first) * 1000 / 48000; /* granule positions are always 48KHz */
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
