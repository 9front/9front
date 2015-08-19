#include <u.h>
#include <libc.h>
#include <authsrv.h>

#define	CHAR(x)		f->x = *p++
#define	SHORT(x)	f->x = (p[0] | (p[1]<<8)); p += 2
#define	VLONG(q)	q = (p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)); p += 4
#define	LONG(x)		VLONG(f->x)
#define	STRING(x,n)	memmove(f->x, p, n); p += n

int
convM2PR(char *ap, int n, Passwordreq *f, Ticket *t)
{
	uchar *p, buf[PASSREQLEN];

	memset(f, 0, sizeof(Passwordreq));
	if(n < PASSREQLEN)
		return -PASSREQLEN;

	if(t){
		memmove(buf, ap, PASSREQLEN);
		ap = (char*)buf;
		decrypt(t->key, ap, PASSREQLEN);
	}
	p = (uchar*)ap;
	CHAR(num);
	STRING(old, ANAMELEN);
	f->old[ANAMELEN-1] = 0;
	STRING(new, ANAMELEN);
	f->new[ANAMELEN-1] = 0;
	CHAR(changesecret);
	STRING(secret, SECRETLEN);
	f->secret[SECRETLEN-1] = 0;
	n = p - (uchar*)ap;
	return n;
}
