#include <u.h>
#include <libc.h>
#include <authsrv.h>

#define	CHAR(x)		f->x = *p++
#define	SHORT(x)	f->x = (p[0] | (p[1]<<8)); p += 2
#define	VLONG(q)	q = (p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)); p += 4
#define	LONG(x)		VLONG(f->x)
#define	STRING(x,n)	memmove(f->x, p, n); p += n

int
convM2A(char *ap, int n, Authenticator *f, Ticket *t)
{
	uchar *p, buf[AUTHENTLEN];

	memset(f, 0, sizeof(Authenticator));
	if(n < AUTHENTLEN)
		return -AUTHENTLEN;

	if(t) {
		memmove(buf, ap, AUTHENTLEN);
		ap = (char*)buf;
		decrypt(t->key, ap, AUTHENTLEN);
	}
	p = (uchar*)ap;
	CHAR(num);
	STRING(chal, CHALLEN);
	LONG(id);
	n = p - (uchar*)ap;
	return n;
}
