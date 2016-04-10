#include "sys9.h"

typedef unsigned long long uvlong;
typedef long long vlong;
typedef unsigned char uchar;

static uvlong order = 0x0001020304050607ULL;

static void
be2vlong(vlong *to, uchar *f)
{
	uchar *t, *o;
	int i;

	t = (uchar*)to;
	o = (uchar*)&order;
	for(i = 0; i < 8; i++)
		t[o[i]] = f[i];
}

long long
_NSEC(void)
{
	uchar b[8];
	vlong t;
	int opened;
	static int fd = -1;

	opened = 0;
	for(;;) {
		if(fd < 0)
			if(opened++ ||
			    (fd = _OPEN("/dev/bintime", OREAD|OCEXEC)) < 0)
				return 0;
		if(_PREAD(fd, b, sizeof b, 0) == sizeof b)
			break;		/* leave fd open for future use */
		/* short read, perhaps try again */
		_CLOSE(fd);
		fd = -1;
	}
	be2vlong(&t, b);
	return t;
}
