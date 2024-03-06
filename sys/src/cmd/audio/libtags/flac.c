/* https://xiph.org/flac/format.html */
#include "tagspriv.h"

#define beu3(d) ((d)[0]<<16 | (d)[1]<<8 | (d)[2]<<0)

int
tagflac(Tagctx *ctx)
{
	uchar *d;
	int sz, last;
	uvlong g;

	d = (uchar*)ctx->buf;
	/* 8 bytes for marker, block type, length. 18 bytes for the stream info */
	if(ctx->read(ctx, d, 8+18) != 8+18 || memcmp(d, "fLaC\x00", 5) != 0)
		return -1;

	sz = beu3(&d[5]); /* size of the stream info */
	ctx->samplerate = beu3(&d[18]) >> 4;
	ctx->channels = ((d[20]>>1) & 7) + 1;
	if(ctx->samplerate < 1 || ctx->channels < 1)
		return -1;

	g = (uvlong)(d[21] & 0xf)<<32 | beu3(&d[22])<<8 | d[25];
	ctx->duration = g * 1000 / ctx->samplerate;

	/* skip the rest of the stream info */
	if(ctx->seek(ctx, sz-18, 1) != ctx->restart+8+sz)
		return -1;

	for(last = 0; !last;){
		if(ctx->read(ctx, d, 4) != 4)
			return -1;

		if((sz = beu3(&d[1])) < 0)
			return -1;
		if((d[0] & 0x80) != 0)
			last = 1;

		if((d[0] & 0x7f) == 6){ /* 6 = picture */
			int n, offset;
			char *mime;

			if(sz < 8+4+20 || ctx->read(ctx, d, 8) != 8) /* type, mime length */
				return -1;
			sz -= 8;
			n = beuint(&d[4]);
			mime = ctx->buf+20;
			if(n < 0 || n >= sz-4-20 || n >= ctx->bufsz-20 || ctx->read(ctx, mime, n) != n)
				return -1;
			sz -= n;
			mime[n] = 0;
			ctx->read(ctx, d, 4); /* description */
			sz -= 4;
			offset = beuint(d) + ctx->seek(ctx, 0, 1) + 20;
			ctx->read(ctx, d, 20);
			sz -= 20;
			if((n = beuint(&d[16])) < 0)
				return -1;
			if(n > 0)
				tagscallcb(ctx, Timage, "", mime, offset, n, nil);
			if(ctx->seek(ctx, sz, 1) <= 0)
				return -1;
		}else if((d[0] & 0x7f) == 4){ /* 4 = vorbis comment */
			int i, numtags, tagsz, vensz;
			char *k, *v;

			if(sz < 12 || ctx->read(ctx, d, 4) != 4)
				return -1;

			sz -= 4;
			vensz = leuint(d);
			if(vensz < 0 || vensz > sz-4)
				return -1;
			/* skip vendor, read the number of tags */
			if(ctx->seek(ctx, vensz, 1) < 0 || ctx->read(ctx, d, 4) != 4)
				return -1;
			sz -= vensz + 4;
			numtags = leuint(d);

			for(i = 0; i < numtags && sz > 4; i++){
				if(ctx->read(ctx, d, 4) != 4)
					return -1;
				tagsz = leuint(d);
				sz -= 4;
				if(tagsz < 0 || tagsz > sz)
					return -1;

				/* if it doesn't fit, ignore it */
				if(tagsz == 0 || tagsz >= ctx->bufsz){
					if(ctx->seek(ctx, tagsz, 1) < 0)
						return -1;
					continue;
				}

				k = ctx->buf;
				if(ctx->read(ctx, k, tagsz) != tagsz)
					return -1;
				/* some tags have a stupid '\r'; ignore */
				if(k[tagsz-1] == '\r')
					k[tagsz-1] = 0;
				k[tagsz] = 0;

				if((v = strchr(k, '=')) != nil){
					*v++ = 0;
					cbvorbiscomment(ctx, k, v);
				}
			}
		}else if(ctx->seek(ctx, sz, 1) <= 0)
			return -1;
	}

	return 0;
}
