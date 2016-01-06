#include <u.h>
#include <libc.h>
#include <authsrv.h>

int
convTR2M(Ticketreq *f, char *ap, int n)
{
	uchar *p;

	if(n < TICKREQLEN)
		return 0;

	p = (uchar*)ap;
	*p++ = f->type;
	memmove(p, f->authid, ANAMELEN), p += ANAMELEN;
	memmove(p, f->authdom, DOMLEN), p += DOMLEN;
	memmove(p, f->chal, CHALLEN), p += CHALLEN;
	memmove(p, f->hostid, ANAMELEN), p += ANAMELEN;
	memmove(p, f->uid, ANAMELEN), p += ANAMELEN;
	n = p - (uchar*)ap;

	return n;
}
