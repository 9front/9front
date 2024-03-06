#include "tagspriv.h"

int
tags3m(Tagctx *ctx)
{
	uchar d[28+1+1], o[28*UTFmax+1], *s;

	if(ctx->read(ctx, d, 28+1+1) != 28+1+1 || (d[28] != 0x1a && d[28] != 0) || d[29] != 0x10)
		return -1;
	d[28] = 0;
	for(s = d+27; s != d-1 && (*s == ' ' || *s == 0); s--);
	s[1] = 0;
	if(cp437toutf8(o, sizeof(o), d, s+1-d) > 0)
		txtcb(ctx, Ttitle, "", o);

	return 0;
}
