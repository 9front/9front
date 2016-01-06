#include <u.h>
#include <libc.h>
#include <authsrv.h>

int
convM2TR(char *ap, int n, Ticketreq *f)
{
	uchar *p;

	memset(f, 0, sizeof(Ticketreq));
	if(n < TICKREQLEN)
		return -TICKREQLEN;

	p = (uchar*)ap;
	f->type = *p++;
	memmove(f->authid, p, ANAMELEN), p += ANAMELEN;
	memmove(f->authdom, p, DOMLEN), p += DOMLEN;
	memmove(f->chal, p, CHALLEN), p += CHALLEN;
	memmove(f->hostid, p, ANAMELEN), p += ANAMELEN;
	memmove(f->uid, p, ANAMELEN), p += ANAMELEN;

	f->authid[ANAMELEN-1] = 0;
	f->authdom[DOMLEN-1] = 0;
	f->hostid[ANAMELEN-1] = 0;
	f->uid[ANAMELEN-1] = 0;
	n = p - (uchar*)ap;

	return n;
}
