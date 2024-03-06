#include "tagspriv.h"

int
tagxm(Tagctx *ctx)
{
	uchar d[17+20+1], o[20*UTFmax+1];

	if(ctx->read(ctx, d, 17+20) != 17+20 || cistrncmp((char*)d, "Extended Module: ", 17) != 0)
		return -1;
	d[17+20] = 0;
	if(cp437toutf8(o, sizeof(o), d+17, 20) > 0)
		txtcb(ctx, Ttitle, "", o);

	return 0;
}
