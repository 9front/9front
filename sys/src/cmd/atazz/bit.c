#include <u.h>
#include <libc.h>
#include <fis.h>
#include "atazz.h"

char*
sebtab(char *p, char *e, Btab *t, int nt, uint u)
{
	char *p0;
	int i;

	p0 = p;
	for(i = 0; i < nt; i++)
		if(u & 1<< t[i].bit)
			p = seprint(p, e, "%s ", t[i].name);
	if(p > p0)
		p--;
	*p = 0;
	return p;
}

void
pw(uchar *p, ushort i)
{
	p[0] = i >> 0;
	p[1] = i >> 8;
}

void
pdw(uchar *p, uint i)
{
	p[0] = i >> 0;
	p[1] = i >> 8;
	p[2] = i >> 16;
	p[3] = i >> 24;
}

void
pqw(uchar *p, uvlong i)
{
	pdw(p, i);
	pdw(p + 4, i >> 32);
}

ushort
w(uchar *u)
{
	ushort r;

	r = u[0] << 0;
	r |= u[1] << 8;
	return r;
}

uint
dw(uchar *u)
{
	ulong r;

	r = u[0] << 0;
	r |= u[1] << 8;
	r |= u[2] << 16;
	r |= u[3] << 24;
	return r;
}

uvlong
qw(uchar *u)
{
	return dw(u) | (uvlong)dw(u + 4)<<32;
}
