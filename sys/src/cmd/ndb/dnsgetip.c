/* one-shot resolver */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include "dns.h"

Cfg cfg;
char *dbfile;
char *logfile		= "dnsgetip";
int debug		= 0;
char mntpt[Maxpath];

int aflag = 0;
int addresses = 0;

char*
resolve(char *name, int type)
{
	int rcode;
	Request req;
	RR *rr, *rp, *neg;

	memset(&req, 0, sizeof req);
	getactivity(&req);
	req.aborttime = timems() + Maxreqtm;
	req.isslave = 1;
	req.from = argv0;

	rcode = Rok;
	rr = dnresolve(name, Cin, type, &req, nil, 0, Recurse, 0, &rcode);
	neg = rrremneg(&rr);
	if(neg != nil)
		rcode = neg->negrcode;

	rrfreelist(neg);

	for(rp = rr; rp != nil; rp = rp->next){
		print("%s\n", rp->ip->name);
		addresses++;
		if(!aflag)
			exits(nil);
	}

	rrfreelist(rr);
	putactivity(&req);

	return rcode!=Rok? rcname(rcode): nil;
}

void
usage(void)
{
	fprint(2, "%s: [-adx] domain\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *e4, *e6;

	strcpy(mntpt, "/net");
	cfg.resolver = 1;

	ARGBEGIN{
	case 'a':
		aflag = 1;
		break;
	case 'd':
		debug++;
		break;
	case 'x':
		strcpy(mntpt, "/net.alt");
		break;
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	if(strcmp(ipattr(*argv), "ip") == 0)
		print("%s\n", *argv);
	else {
		dninit();
		e4 = resolve(*argv, Ta);
		e6 = resolve(*argv, Taaaa);

		if(addresses == 0){
			if(e4 == e6)
				sysfatal("%s: dns failure: %s", *argv, e4);

			sysfatal("%s: dns failure: v4: %s: v6: %s", *argv, e4, e6);
		}
	}
	exits(nil);
}

RR*
getdnsservers(int class)
{
	return dnsservers(class);
}

/* stubs */
void syslog(int, char*, char*, ...){}
void logreply(int, char*, uchar*, DNSmsg*){}
void logrequest(int, int, char*, uchar*, char*, char*, int){}
