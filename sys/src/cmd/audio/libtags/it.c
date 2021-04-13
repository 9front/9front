#include "tagspriv.h"

int
tagit(Tagctx *ctx)
{
	char d[4+26+1];

	if(ctx->read(ctx, d, 4+26) != 4+26 || memcmp(d, "IMPM", 4) != 0)
		return -1;
	d[4+26] = 0;
	txtcb(ctx, Ttitle, "", d+4);

	return 0;
}
