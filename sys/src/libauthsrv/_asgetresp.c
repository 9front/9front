#include <u.h>
#include <libc.h>
#include <authsrv.h>

int
_asgetresp(int fd, Ticket *t, Authenticator *a, Authkey *k)
{
	char tbuf[TICKETLEN+AUTHENTLEN];
	int n, m;

	m = TICKETLEN;
	memset(t, 0, sizeof(Ticket));
	if(a != nil){
		m += AUTHENTLEN;
		memset(a, 0, sizeof(Authenticator));
	}

	n = _asrdresp(fd, tbuf, m);
	if(n <= 0)
		return -1;

	m = convM2T(tbuf, n, t, k);
	if(m <= 0)
		return -1;

	if(a != nil){
		if(convM2A(tbuf+m, n-m, a, t) <= 0)
			return -1;
	}

	return 0;
}
