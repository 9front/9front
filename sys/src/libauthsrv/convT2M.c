#include <u.h>
#include <libc.h>
#include <authsrv.h>
#include <libsec.h>

extern int form1B2M(char *ap, int n, uchar key[32]);

int
convT2M(Ticket *f, char *ap, int n, Authkey *key)
{
	uchar *p;

	if(n < 1+CHALLEN+2*ANAMELEN)
		return 0;

	p = (uchar*)ap;
	*p++ = f->num;
	memmove(p, f->chal, CHALLEN), p += CHALLEN;
	memmove(p, f->cuid, ANAMELEN), p += ANAMELEN;
	memmove(p, f->suid, ANAMELEN), p += ANAMELEN;
	switch(f->form){
	case 0:
		if(n < 1+CHALLEN+2*ANAMELEN+DESKEYLEN)
			return 0;

		memmove(p, f->key, DESKEYLEN), p += DESKEYLEN;
		n = p - (uchar*)ap;
		encrypt(key->des, ap, n);
		return n;
	case 1:
		if(n < 12+CHALLEN+2*ANAMELEN+NONCELEN+16)
			return 0;

		memmove(p, f->key, NONCELEN), p += NONCELEN;
		return form1B2M(ap, p - (uchar*)ap, key->pakkey);
	}

	return 0;
}
