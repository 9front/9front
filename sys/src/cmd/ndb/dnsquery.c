#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <ndb.h>
#include "dns.h"
#include "ip.h"

static void
querydns(int fd, char *line, int n)
{
	char buf[8192+1];

	seek(fd, 0, 0);
	if(write(fd, line, n) != n) {
		print("!%r\n");
		return;
	}
	seek(fd, 0, 0);
	while((n = read(fd, buf, sizeof(buf)-1)) > 0){
		buf[n++] = '\n';
		write(1, buf, n);
	}
}

/*
 *  convert address into a reverse lookup address
 */
static void
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

static void
query(int fd)
{
	int n;
	char *lp;
	char buf[1024], line[1024];
	Biobuf in;

	Binit(&in, 0, OREAD);
	for(fprint(2, "> "); lp = Brdline(&in, '\n'); fprint(2, "> ")){
		n = Blinelen(&in) -1;
		while(isspace(lp[n]))
			lp[n--] = 0;
		n++;
		while(isspace(*lp)){
			lp++;
			n--;
		}
		if(!*lp)
			continue;
		strcpy(line, lp);

		/* default to an "ip" request if alpha, "ptr" if numeric */
		if(strchr(line, ' ') == nil)
			if(strcmp(ipattr(line), "ip") == 0) {
				strcat(line, " ptr");
				n += 4;
			} else {
				strcat(line, " ip");
				n += 3;
			}

		if(n > 4 && strcmp(" ptr", &line[n-4]) == 0){
			line[n-4] = 0;
			mkptrname(line, buf, sizeof buf);
			n = snprint(line, sizeof line, "%s ptr", buf);
		}

		querydns(fd, line, n);
	}
	Bterm(&in);
}

void
main(int argc, char *argv[])
{
	char *dns  = "/net/dns";
	int fd;

	ARGBEGIN {
	case 'x':
		dns = "/net.alt/dns";
		break;
	default:
		fprint(2, "usage: %s [-x] [/net/dns]\n", argv0);
		exits("usage");
	} ARGEND;

	if(argc > 0)
		dns = argv[0];

	fd = open(dns, ORDWR);
	if(fd < 0)
		sysfatal("can't open %s: %r", dns);

	query(fd);
	exits(0);
}
