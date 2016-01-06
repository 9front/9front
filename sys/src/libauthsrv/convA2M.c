#include <u.h>
#include <libc.h>
#include <authsrv.h>

extern int form1B2M(char *ap, int n, uchar key[32]);

int
convA2M(Authenticator *f, char *ap, int n, Ticket *t)
{
	uchar *p;

	if(n < 1+CHALLEN)
		return 0;

	p = (uchar*)ap;
	*p++ = f->num;
	memmove(p, f->chal, CHALLEN), p += CHALLEN;
	switch(t->form){
	case 0:
		if(n < 1+CHALLEN+4)
			return 0;

		memset(p, 0, 4), p += 4;	/* unused id field */
		n = p - (uchar*)ap;
		encrypt(t->key, ap, n);
		return n;
	case 1:
		if(n < 12+CHALLEN+NONCELEN+16)
			return 0;

		memmove(p, f->rand, NONCELEN), p += NONCELEN;
		return form1B2M(ap, (char*)p - ap, t->key);
	}

	return 0;
}
