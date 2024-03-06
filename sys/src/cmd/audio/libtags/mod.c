#include "tagspriv.h"

/* insane. */
static char *variants[] =
{
	"M.K.",
	"M!K!",
	"M&K!",
	"N.T.",
	"NSMS",
	"FLT4",
	"M\0\0\0",
	"8\0\0\0",
	"FEST",
	"FLT8",
	"CD81",
	"OCTA",
	"OKTA",
	"4CHN",
	"6CHN",
	"8CHN",
	"10CH",
	"16CN",
	"32CN",
	nil,
};

int
tagmod(Tagctx *ctx)
{
	uchar d[20], o[20*2+1];
	int i;

	if(ctx->seek(ctx, 1080, 0) != 1080)
		return -1;
	if(ctx->read(ctx, d, 4) != 4)
		return -1;
	for(i = 0; ; i++){
		if(variants[i] == nil)
			return -1;
		if(memcmp(d, variants[i], 4) == 0)
			break;
	}
	if(ctx->seek(ctx, 0, 0) != 0)
		return -1;
	if(ctx->read(ctx, d, 20) != 20)
		return -1;
	if(cp437toutf8(o, sizeof(o), d, 20) > 0)
		txtcb(ctx, Ttitle, "", o);

	return 0;
}
