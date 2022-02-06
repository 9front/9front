#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

int
hname(char *buf)
{
	uchar d[SHA1dlen];
	u32int x;
	int n;

	n = strlen(buf);
	sha1((uchar*)buf, n, d, nil);
	x = d[0] | d[1]<<8 | d[2]<<16;
	return snprint(buf, n+1, "%.5ux", x & 0xfffff);
}
