#include "tagspriv.h"

int
tagxm(Tagctx *ctx)
{
	char d[17+20+1], *s;

	if(ctx->read(ctx, d, 17+20) != 17+20 || memcmp(d, "Extended Module: ", 17) != 0)
		return -1;
	d[17+20] = 0;
	for(s = d+17; *s == ' '; s++);
	txtcb(ctx, Ttitle, "", s);

	return 0;
}
