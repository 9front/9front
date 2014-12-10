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
int	testing		= 0;
int	traceactivity	= 0;
char	*zonerefreshprogram;

int aflag;

void
resolve(char *name, int type)
{
	Request req;
	RR *rp;

	memset(&req, 0, sizeof req);
	getactivity(&req, 0);
	req.isslave = 1;
	req.aborttime = NS2MS(nowns) + Maxreqtm;

	rp = dnresolve(name, Cin, type, &req, 0, 0, Recurse, 0, 0);
	rrremneg(&rp);
	while(rp != nil){
		print("%s\n", rp->ip->name);
		if(!aflag)
			exits(nil);
		rp = rp->next;
	}
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
		resolve(*argv, Ta);
		resolve(*argv, Taaaa);
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
