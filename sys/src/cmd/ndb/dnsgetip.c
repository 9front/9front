/* one-shot resolver */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include "dns.h"

Cfg cfg;
char *dbfile;
int debug		= 0;
char *logfile		= "dnsgetip";
int	maxage		= 60*60;
char mntpt[Maxpath];
int	needrefresh	= 0;
ulong	now		= 0;
vlong	nowns		= 0;
int	traceactivity	= 0;
char	*zonerefreshprogram;

int aflag = 0;
int addresses = 0;

char Ebotch[] = "dns botch";

char*
resolve(char *name, int type)
{
	int status;
	char *errmsg;
	Request req;
	RR *rr, *rp, *neg;

	status = Rok;
	errmsg = nil;

	memset(&req, 0, sizeof req);
	getactivity(&req, 0);
	req.isslave = 1;
	req.aborttime = NS2MS(nowns) + Maxreqtm;

	rr = dnresolve(name, Cin, type, &req, nil, 0, Recurse, 0, &status);
	neg = rrremneg(&rr);
	if(rr == nil || neg != nil){
		if(neg != nil)
			status = neg->negrcode;
		errmsg = Ebotch;
		if(status > 0 && status < nrname)
			errmsg = rname[status];
	}

	rrfreelist(neg);

	for(rp = rr; rp != nil; rp = rp->next){
		print("%s\n", rp->ip->name);
		addresses++;
		if(!aflag)
			exits(nil);
	}

	rrfreelist(rr);

	return errmsg;
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
	cfg.inside = 1;
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
void logreply(int, uchar*, DNSmsg*){}
void logsend(int, int, uchar*, char*, char*, int){}
