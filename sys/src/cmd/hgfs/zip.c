#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

#include <flate.h>

struct zbuf {
	int	fd;
	int	len;

	uchar	*b, *p, *e;
};

static int
zwrite(void *a, void *p, int n)
{
	int *ofd = a;

	if(write(*ofd, p, n) != n)
		return -1;
	return n;
}

static int
zgetc(void *a)
{
	struct zbuf *z = a;
	int n;

	for(;;){
		if(z->p < z->e)
			return *z->p++;
		if(z->len <= 0)
			return -1;
		if((n = BUFSZ) > z->len)
			n = z->len;
		if((n = read(z->fd, z->p = z->b, n)) <= 0)
			return -1;
		z->len -= n;
		z->e = z->p + n;
	}
}

int
funzip(int ofd, int zfd, int len)
{
	uchar buf[BUFSZ];
	struct zbuf z;
	int wlen, n;
	uchar *p;

	if(len == 0)
		return 0;

	wlen = 0;
	if((n = BUFSZ) > len)
		n = len;
	if((n = read(zfd, p = buf, n)) <= 0)
		return -1;
	len -= n;
	switch(*p){
	default:
		return -1;
	case 'u':
		p++;
		n--;
		/* no break */
	case '\0':
		while((n > 0) && (write(ofd, p, n) == n)){
			wlen += n;
			if(len <= 0)
				break;
			if((n = BUFSZ) > len)
				n = len;
			if((n = read(zfd, p = buf, n)) <= 0)
				break;
			len -= n;
		}
		break;
	case 'x':
		if(n < 2)
			return -1;
		z.fd = zfd;
		z.len = len;
		z.b = buf;
		z.p = p + 2;
		z.e = p + n;
		if((wlen = inflate(&ofd, zwrite, &z, zgetc)) < 0){
			werrstr("%s", flateerr(wlen));
			return -1;
		}
		break;
	}
	return wlen;
}
