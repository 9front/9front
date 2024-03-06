#include "tagspriv.h"

typedef struct Getter Getter;

struct Getter
{
	int (*f)(Tagctx *ctx);
	int format;
};

static const Getter g[] =
{
	{tagid3v2, Fmp3},
	{tagid3v1, Fmp3},
	{tagvorbis, Fvorbis},
	{tagflac, Fflac},
	{tagm4a, Fm4a},
	{tagopus, Fopus},
	{tagwav, Fwav},
	{tagit, Fit},
	{tagxm, Fxm},
	{tags3m, Fs3m},
	{tagmod, Fmod},
};

void
tagscallcb(Tagctx *ctx, int type, const char *k, char *s, int offset, int size, Tagread f)
{
	char *e;

	if(f == nil && size == 0){
		while((uchar)*s <= ' ' && *s)
			s++;
		e = s + strlen(s);
		while(e != s && (uchar)e[-1] <= ' ')
			e--;
		*e = 0;
	}
	if(*s){
		ctx->tag(ctx, type, k, s, offset, size, f);
		if(type != Tunknown){
			ctx->found |= 1<<type;
			ctx->num++;
		}
	}
}

int
tagsget(Tagctx *ctx)
{
	int i, res;

	ctx->channels = ctx->samplerate = ctx->bitrate = ctx->duration = 0;
	ctx->found = 0;
	ctx->format = Funknown;
	ctx->restart = 0;
	res = -1;
	for(i = 0; i < nelem(g); i++){
		ctx->num = 0;
		if(g[i].f(ctx) == 0){
			ctx->format = g[i].format;
			res = 0;
		}
		ctx->seek(ctx, ctx->restart, 0);
	}

	return res;
}
