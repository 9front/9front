#include <u.h>
#include <libc.h>
#include <ip.h>

/*
 *  convert address into a reverse lookup address
 */
void
mkptrname(char *ip, char *rip, int rlen)
{
	uchar a[IPaddrlen];
	char *p, *e;
	int i;

	if(cistrstr(ip, "in-addr.arpa") || cistrstr(ip, "ip6.arpa") || parseip(a, ip) == -1)
		snprint(rip, rlen, "%s", ip);
	else if(isv4(a))
		snprint(rip, rlen, "%ud.%ud.%ud.%ud.in-addr.arpa",
			a[15], a[14], a[13], a[12]);
	else{
		p = rip;
		e = rip + rlen;
		for(i = 15; i >= 0; i--){
			p = seprint(p, e, "%ux.", a[i]&0xf);
			p = seprint(p, e, "%ux.", a[i]>>4);
		}
		seprint(p, e, "ip6.arpa");
	}
}
