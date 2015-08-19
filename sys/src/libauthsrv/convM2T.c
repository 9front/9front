#include <u.h>
#include <libc.h>
#include <authsrv.h>

#define	CHAR(x)		f->x = *p++
#define	SHORT(x)	f->x = (p[0] | (p[1]<<8)); p += 2
#define	VLONG(q)	q = (p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)); p += 4
#define	LONG(x)		VLONG(f->x)
#define	STRING(x,n)	memmove(f->x, p, n); p += n

int
convM2T(char *ap, int n, Ticket *f, Authkey *key)
{
	uchar *p, buf[TICKETLEN];

	memset(f, 0, sizeof(Ticket));
	if(n < TICKETLEN)
		return -TICKETLEN;

	if(key){
		memmove(buf, ap, TICKETLEN);
		ap = (char*)buf;
		decrypt(key->des, ap, TICKETLEN);
	}
	p = (uchar*)ap;
	CHAR(num);
	STRING(chal, CHALLEN);
	STRING(cuid, ANAMELEN);
	f->cuid[ANAMELEN-1] = 0;
	STRING(suid, ANAMELEN);
	f->suid[ANAMELEN-1] = 0;
	STRING(key, DESKEYLEN);
	n = p - (uchar*)ap;
	return n;
}
