/* http://wiki.multimedia.cx/?title=QuickTime_container */
/* https://developer.apple.com/library/mac/documentation/QuickTime/QTFF/QTFFChap2/qtff2.html */
#include "tagspriv.h"

#define beuint16(d) (ushort)((d)[0]<<8 | (d)[1]<<0)

int
tagm4a(Tagctx *ctx)
{
	uvlong duration;
	uint x;
	uchar *d;
	int sz, type, dtype, i, skip, n;

	d = (uchar*)ctx->buf;
	/* 4 bytes for atom size, 4 for type, 4 for data - exect "ftyp" to come first */
	if(ctx->read(ctx, d, 4+4+4) != 4+4+4 || memcmp(d+4, "ftypM4A ", 8) != 0)
		return -1;
	sz = beuint(d) - 4; /* already have 8 bytes */

	for(;;){
		if(sz < 0 || ctx->seek(ctx, sz, 1) < 0)
			return -1;
		if(ctx->read(ctx, d, 4) != 4) /* size */
			break;
		sz = beuint(d);
		if(sz == 0)
			continue;
		if(ctx->read(ctx, d, 4) != 4) /* type */
			return -1;
		if(sz < 8)
			continue;

		d[4] = 0;

		if(memcmp(d, "meta", 4) == 0){
			sz = 4;
			continue;
		}else if(
			memcmp(d, "udta", 4) == 0 ||
			memcmp(d, "ilst", 4) == 0 ||
			memcmp(d, "trak", 4) == 0 ||
			memcmp(d, "mdia", 4) == 0 ||
			memcmp(d, "minf", 4) == 0 ||
			memcmp(d, "moov", 4) == 0 ||
			memcmp(d, "trak", 4) == 0 ||
			memcmp(d, "stbl", 4) == 0){
			sz = 0;
			continue;
		}else if(memcmp(d, "stsd", 4) == 0){
			sz -= 8;
			if(ctx->read(ctx, d, 8) != 8)
				return -1;
			sz -= 8;

			for(i = beuint(&d[4]); i > 0 && sz > 0; i--){
				if(ctx->read(ctx, d, 8) != 8) /* size + format */
					return -1;
				sz -= 8;
				skip = beuint(d) - 8;
				if(skip < 0)
					return -1;

				if(memcmp(&d[4], "mp4a", 4) == 0){ /* audio */
					n = 6+2 + 2+4+2 + 2+2 + 2+2 + 4; /* read a bunch at once */
					/* reserved+id, ver+rev+vendor, channels+bps, ?+?, sample rate */
					if(ctx->read(ctx, d, n) != n)
						return -1;
					skip -= n;
					sz -= n;
					ctx->channels = beuint16(&d[16]);
					ctx->samplerate = beuint(&d[24])>>16;
				}

				if(ctx->seek(ctx, skip, 1) < 0)
					return -1;
				sz -= skip;
			}
			continue;
		}

		sz -= 8;
		type = -1;
		if(memcmp(d, "\251nam", 4) == 0)
			type = Ttitle;
		else if(memcmp(d, "\251alb", 4) == 0)
			type = Talbum;
		else if(memcmp(d, "\251ART", 4) == 0)
			type = Tartist;
		else if(memcmp(d, "aART", 4) == 0)
			type = Talbumartist;
		else if(memcmp(d, "\251gen", 4) == 0 || memcmp(d, "gnre", 4) == 0)
			type = Tgenre;
		else if(memcmp(d, "\251day", 4) == 0)
			type = Tdate;
		else if(memcmp(d, "covr", 4) == 0)
			type = Timage;
		else if(memcmp(d, "trkn", 4) == 0)
			type = Ttrack;
		else if(memcmp(d, "\251wrt", 4) == 0)
			type = Tcomposer;
		else if(memcmp(d, "\251cmt", 4) == 0)
			type = Tcomment;
		else if(memcmp(d, "mdhd", 4) == 0){
			if(ctx->read(ctx, d, 4) != 4)
				return -1;
			sz -= 4;
			duration = 0;
			if(d[0] == 0){ /* version 0 */
				if(ctx->read(ctx, d, 16) != 16)
					return -1;
				sz -= 16;
				if((x = beuint(&d[8])) > 0)
					duration = beuint(&d[12]) / x;
			}else if(d[1] == 1){ /* version 1 */
				if(ctx->read(ctx, d, 28) != 28)
					return -1;
				sz -= 28;
				if((x = beuint(&d[16])) > 0)
					duration = ((uvlong)beuint(&d[20])<<32 | beuint(&d[24])) / (uvlong)x;
			}
			ctx->duration = duration * 1000;
			continue;
		}

		if(type < 0)
			continue;

		if(sz < 16 || ctx->seek(ctx, 8, 1) < 0) /* skip size and "data" */
			return -1;
		sz -= 8;
		if(ctx->read(ctx, d, 8) != 8) /* read data type and 4 bytes of whatever else */
			return -1;
		sz -= 8;
		d[0] = 0;
		dtype = beuint(d);

		if(type == Ttrack){
			if(ctx->read(ctx, d, 4) != 4)
				return -1;
			sz -= 4;
			snprint((char*)d, ctx->bufsz, "%d", beuint(d));
			txtcb(ctx, type, "", d);
		}else if(type == Tgenre && dtype != 1){
			if(ctx->read(ctx, d, 2) != 2)
				return -1;
			sz -= 2;
			if((i = d[1]-1) >= 0 && i < Numgenre)
				txtcb(ctx, type, "", id3genres[i]);
		}else if(dtype == 1){ /* text */
			if(sz >= ctx->bufsz) /* skip tags that can't fit into memory. ">=" because of '\0' */
				continue;
			if(sz < 0 || ctx->read(ctx, d, sz) != sz)
				return -1;
			d[sz] = 0;
			txtcb(ctx, type, "", d);
			sz = 0;
		}else if(type == Timage && dtype == 13) /* jpeg cover image */
			tagscallcb(ctx, Timage, "", "image/jpeg", ctx->seek(ctx, 0, 1), sz, nil);
		else if(type == Timage && dtype == 14) /* png cover image */
			tagscallcb(ctx, Timage, "", "image/png", ctx->seek(ctx, 0, 1), sz, nil);
	}

	return 0;
}
