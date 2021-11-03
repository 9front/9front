#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <ndb.h>
#include "dns.h"

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

static void
query(int fd)
{
	int n;
	char *lp, *bang;
	char buf[1024], line[1024];
	Biobuf in;

	Binit(&in, 0, OREAD);
	for(fprint(2, "> "); lp = Brdline(&in, '\n'); fprint(2, "> ")){
		n = Blinelen(&in);
		while(n > 0 && isspace(lp[n-1]))
			n--;
		lp[n] = 0;
		while(isspace(*lp))
			lp++;
		bang = "";
		while(*lp == '!'){
			bang = "!";
			lp++;
		}
		while(isspace(*lp))
			lp++;
		if(*lp == 0)
			continue;

		/* default to an "ip" request if alpha, "ptr" if numeric */
		if(strchr(lp, ' ') == nil){
			if(strcmp(ipattr(lp), "ip") == 0) {
				n = snprint(line, sizeof line, "%s ptr", lp);
			} else {
				n = snprint(line, sizeof line, "%s ip", lp);
			}
		} else {
			n = snprint(line, sizeof line, "%s", lp);
		}

		if(n > 4 && strcmp(" ptr", &line[n-4]) == 0){
			line[n-4] = 0;
			mkptrname(line, buf, sizeof buf);
			snprint(line, sizeof line, "%s ptr", buf);
		}

		n = snprint(buf, sizeof buf, "%s%s", bang, line);
		querydns(fd, buf, n);
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
