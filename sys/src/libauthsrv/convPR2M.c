#include <u.h>
#include <libc.h>
#include <authsrv.h>

extern int form1B2M(char *ap, int n, uchar key[32]);

int
convPR2M(Passwordreq *f, char *ap, int n, Ticket *t)
{
	uchar *p;

	if(n < 1+2*PASSWDLEN+1+SECRETLEN)
		return 0;

	p = (uchar*)ap;
	*p++ = f->num;
	memmove(p, f->old, PASSWDLEN), p += PASSWDLEN;
	memmove(p, f->new, PASSWDLEN), p += PASSWDLEN;
	*p++ = f->changesecret;
	memmove(p, f->secret, SECRETLEN), p += SECRETLEN;
	switch(t->form){
	case 0:
		n = p - (uchar*)ap;
		encrypt(t->key, ap, n);
		return n;
	case 1:
		if(n < 12+2*PASSWDLEN+1+SECRETLEN+16)
			return 0;
		return form1B2M(ap, p - (uchar*)ap, t->key);
	}

	return 0;
}

