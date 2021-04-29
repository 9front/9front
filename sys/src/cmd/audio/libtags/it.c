#include "tagspriv.h"

int
tagit(Tagctx *ctx)
{
	char d[4+26+1], o[26*UTFmax+1];

	if(ctx->read(ctx, d, 4+26) != 4+26 || memcmp(d, "IMPM", 4) != 0)
		return -1;
	d[4+26] = 0;
	if(cp437toutf8(o, sizeof(o), d+4, 26) > 0)
		txtcb(ctx, Ttitle, "", o);

	return 0;
}
