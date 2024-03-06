#include "tagspriv.h"

#define le16u(d) (u16int)((d)[0] | (d)[1]<<8)

static const struct {
	char *s;
	int type;
}t[] = {
	{"IART", Tartist},
	{"ICRD", Tdate},
	{"IGNR", Tgenre},
	{"INAM", Ttitle},
	{"IPRD", Talbum},
	{"ITRK", Ttrack},
	{"ICMT", Tcomment},
	{"????", Tunknown},
};

int
tagwav(Tagctx *ctx)
{
	uchar *d;
	int i, n, info, csz, x, sz;

	d = (uchar*)ctx->buf;

	sz = 1;
	info = 0;
	for(i = 0; sz > 0; i++){
		if(ctx->read(ctx, d, 4+4+(i?0:4)) != 4+4+(i?0:4))
			return -1;
		if(i == 0){
			if(memcmp(d, "RIFF", 4) != 0 || memcmp(d+8, "WAVE", 4) != 0)
				return -1;
			sz = leuint(d+4);
			if(sz < 4)
				return -1;
			sz -= 4;
			continue;
		}else if(memcmp(d, "INFO", 4) == 0){
			info = 1;
			ctx->seek(ctx, -4, 1);
			continue;
		}

		if(sz <= 8)
			break;
		sz -= 4+4;
		csz = leuint(d+4);
		if(sz < csz || csz < 0)
			break;
		sz -= csz;

		if(i == 1){
			if(memcmp(d, "fmt ", 4) != 0 || csz < 16)
				return -1;
			if(ctx->read(ctx, d, 16) != 16)
				return -1;
			csz -= 16;
			ctx->channels = le16u(d+2);
			ctx->samplerate = leuint(d+4);
			x = leuint(d+8);
			if(ctx->channels < 1 || ctx->samplerate < 1 || x < 1)
				return -1;
			ctx->duration = sz*1000 / x;
		}else if(memcmp(d, "LIST", 4) == 0){
			if(csz < 4)
				return -1;
			sz = csz - 4;
			continue;
		}else if(info && csz < ctx->bufsz-5){
			for(n = 0; n < nelem(t); n++){
				if(memcmp(d, t[n].s, 4) == 0 || t[n].type == Tunknown){
					if(ctx->read(ctx, d+5, csz) != csz)
						return -1;
					d[4] = 0;
					d[5+csz] = 0;
					txtcb(ctx, t[n].type, t[n].type == Tunknown ? (char*)d : "", d+5);
					csz = 0;
					break;
				}
			}
			if(n < nelem(t))
				continue;
		}

		if(ctx->seek(ctx, csz, 1) < 0)
			return -1;
	}

	/* some software may append an id3v2 tag */
	if(i > 0 && ctx->read(ctx, d, 8) == 8 && memcmp(d, "id3 ", 4) == 0)
		tagid3v2(ctx);

	return i > 0 ? 0 : -1;
}
