#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <ip.h>
#include <ndb.h>
#include "dns.h"

enum {
	Maxrequest=		128,
};

Cfg cfg;

static char *servername;
static RR *serveraddrs;

char	*dbfile;
int	debug;
char	*logfile = "dnsdebug";
int	maxage  = 60*60;
char	mntpt[Maxpath];
int	needrefresh;
ulong	now;
uvlong	nowms;
char	*trace;
int	traceactivity;
char	*zonerefreshprogram;

void	docmd(int, char**);
void	doquery(char*, char*);
void	preloadserveraddrs(void);
int	setserver(char*);
void	squirrelserveraddrs(void);

#pragma	varargck	type	"P"	RR*
int	prettyrrfmt(Fmt*);

void
usage(void)
{
	fprint(2, "%s: [-crdx] [-f db-file] [[@server] domain [type]]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int n;
	Biobuf in;
	char *p;
	char *f[4];

	strcpy(mntpt, "/net");
	cfg.inside = 1;

	ARGBEGIN{
	case 'f':
		dbfile = EARGF(usage());
		break;
	case 'c':
		cfg.cachedb = 1;
		break;
	case 'r':
		cfg.resolver = 1;
		break;
	case 'd':
		debug = 1;
		traceactivity = 1;
		break;
	case 'x':
		dbfile = "/lib/ndb/external";
		strcpy(mntpt, "/net.alt");
		break;
	default:
		usage();
	}ARGEND

	dninit();
	fmtinstall('P', prettyrrfmt);
	opendatabase();
	srand(truerand());
	db2cache(1);

	if(cfg.resolver)
		squirrelserveraddrs();

	debug = 1;

	if(argc > 0){
		docmd(argc, argv);
		exits(0);
	}

	Binit(&in, 0, OREAD);
	for(print("> "); p = Brdline(&in, '\n'); print("> ")){
		p[Blinelen(&in)-1] = 0;
		n = tokenize(p, f, 3);
		if(n>=1) {
			docmd(n, f);
		}
	}
	exits(0);
}

static char*
longtime(long t)
{
	int d, h, m, n;
	static char x[128];

	for(d = 0; t >= 24*60*60; t -= 24*60*60)
		d++;
	for(h = 0; t >= 60*60; t -= 60*60)
		h++;
	for(m = 0; t >= 60; t -= 60)
		m++;
	n = 0;
	if(d)
		n += sprint(x, "%d day ", d);
	if(h)
		n += sprint(x+n, "%d hr ", h);
	if(m)
		n += sprint(x+n, "%d min ", m);
	if(t || n == 0)
		sprint(x+n, "%ld sec", t);
	return x;
}

int
prettyrrfmt(Fmt *f)
{
	int rv;
	char *strp, *t, buf[32];
	Fmt fstr;
	RR *rp;

	fmtstrinit(&fstr);
	rp = va_arg(f->args, RR*);
	fmtprint(&fstr, "%R", rp);
	strp = fmtstrflush(&fstr);
	if((t = strchr(strp, '\t')) == nil || rp == nil)
		rv = fmtstrcpy(f, strp);
	else
		rv = fmtprint(f, "%-32.32s %-15.15s %-5.5s%s",
			rp->owner->name, longtime(rp->ttl),
			rrname(rp->type, buf, sizeof buf), t);
	free(strp);
	return rv;
}

void
logsection(char *flag, RR *rp)
{
	if(rp == nil)
		return;
	print("\t%s%P\n", flag, rp);
	for(rp = rp->next; rp != nil; rp = rp->next)
		print("\t      %P\n", rp);
}

void
logreply(int id, uchar *addr, DNSmsg *mp)
{
	RR *rp;
	char buf[12], resp[32];

	switch(mp->flags & Rmask){
	case Rok:
		strcpy(resp, "OK");
		break;
	case Rformat:
		strcpy(resp, "Format error");
		break;
	case Rserver:
		strcpy(resp, "Server failed");
		break;
	case Rname:
		strcpy(resp, "Nonexistent");
		break;
	case Runimplimented:
		strcpy(resp, "Unimplemented");
		break;
	case Rrefused:
		strcpy(resp, "Refused");
		break;
	default:
		sprint(resp, "%d", mp->flags & Rmask);
		break;
	}

	print("%d: rcvd %s from %I (%s%s%s%s%s)\n", id, resp, addr,
		mp->flags & Fauth? "authoritative": "",
		mp->flags & Ftrunc? " truncated": "",
		mp->flags & Frecurse? " recurse": "",
		mp->flags & Fcanrec? " can_recurse": "",
		(mp->flags & (Fauth|Rmask)) == (Fauth|Rname)? " nx": "");
	for(rp = mp->qd; rp != nil; rp = rp->next)
		print("\tQ:    %s %s\n", rp->owner->name,
			rrname(rp->type, buf, sizeof buf));
	logsection("Ans:  ", mp->an);
	logsection("Auth: ", mp->ns);
	logsection("Hint: ", mp->ar);
}

void
logsend(int id, int subid, uchar *addr, char *sname, char *rname, int type)
{
	char buf[12];

	print("%d.%d: sending to %I/%s %s %s\n", id, subid,
		addr, sname, rname, rrname(type, buf, sizeof buf));
}

RR*
getdnsservers(int class)
{
	RR *rr;

	if(servername == nil)
		return dnsservers(class);

	rr = rralloc(Tns);
	rr->owner = dnlookup("local#dns#servers", class, 1);
	rr->host = idnlookup(servername, class, 1);

	return rr;
}

void
squirrelserveraddrs(void)
{
	int v4;
	char *attr;
	RR *rr, *rp, **l;
	Request req;

	/* look up the resolver address first */
	cfg.resolver = 0;
	debug = 0;
	if(serveraddrs){
		rrfreelist(serveraddrs);
		serveraddrs = nil;
	}
	rr = getdnsservers(Cin);
	l = &serveraddrs;
	for(rp = rr; rp != nil; rp = rp->next){
		attr = ipattr(rp->host->name);
		v4 = strcmp(attr, "ip") == 0;
		if(v4 || strcmp(attr, "ipv6") == 0){
			*l = rralloc(v4? Ta: Taaaa);
			(*l)->owner = rp->host;
			(*l)->ip = rp->host;
			l = &(*l)->next;
			continue;
		}
		memset(&req, 0, sizeof req);
		req.isslave = 1;
		req.aborttime = timems() + Maxreqtm;
		*l = dnresolve(rp->host->name, Cin, Ta, &req, nil, 0, Recurse, 0, nil);
		if(*l == nil)
			*l = dnresolve(rp->host->name, Cin, Taaaa, &req,
				nil, 0, Recurse, 0, nil);
		while(*l != nil)
			l = &(*l)->next;
	}
	cfg.resolver = 1;
	debug = 1;
}

void
preloadserveraddrs(void)
{
	RR *rp, **l, *first;

	first = nil;
	l = &first;
	for(rp = serveraddrs; rp != nil; rp = rp->next){
		rrcopy(rp, l);
		rrattach(first, Authoritative);
	}
}

int
setserver(char *server)
{
	if(servername != nil){
		free(servername);
		servername = nil;
		cfg.resolver = 0;
	}
	if(server == nil || *server == 0)
		return 0;
	servername = strdup(server);
	squirrelserveraddrs();
	if(serveraddrs == nil){
		print("can't resolve %s\n", servername);
		cfg.resolver = 0;
	} else
		cfg.resolver = 1;
	return cfg.resolver? 0: -1;
}

void
doquery(char *name, char *tstr)
{
	int len, type, rooted;
	char buf[1024];
	RR *rr, *rp;
	Request req;

	if(cfg.resolver)
		preloadserveraddrs();

	/* default to an "ip" request if alpha, "ptr" if numeric */
	if(tstr == nil || *tstr == 0)
		if(strcmp(ipattr(name), "ip") == 0)
			tstr = "ptr";
		else
			tstr = "ip";

	/* look it up */
	type = rrtype(tstr);
	if(type < 0){
		print("!unknown type %s\n", tstr);
		return;
	}

	/* if name end in '.', remove it */
	len = strlen(name);
	if(len > 0 && name[len-1] == '.'){
		rooted = 1;
		name[len-1] = 0;
	} else
		rooted = 0;

	/* inverse queries may need to be permuted */
	if(type == Tptr)
		mkptrname(name, buf, sizeof buf);
	else
		strncpy(buf, name, sizeof buf);

	memset(&req, 0, sizeof req);
	getactivity(&req);
	req.isslave = 1;
	req.aborttime = timems() + Maxreqtm;
	rr = dnresolve(buf, Cin, type, &req, nil, 0, Recurse, rooted, nil);
	if(rr){
		print("----------------------------\n");
		for(rp = rr; rp; rp = rp->next)
			print("answer %P\n", rp);
		print("----------------------------\n");
	}
	rrfreelist(rr);
	putactivity(&req);
}

void
docmd(int n, char **f)
{
	int tmpsrv;
	char *name, *type;

	name = type = nil;
	tmpsrv = 0;

	if(strcmp(f[0], "refresh") == 0){
		db2cache(1);
		dnageall(1);
		return;
	}

	if(*f[0] == '@') {
		if(setserver(f[0]+1) < 0)
			return;

		switch(n){
		case 3:
			type = f[2];
			/* fall through */
		case 2:
			name = f[1];
			tmpsrv = 1;
			break;
		}
	} else
		switch(n){
		case 2:
			type = f[1];
			/* fall through */
		case 1:
			name = f[0];
			break;
		}

	if(name == nil)
		return;

	if(!cfg.cachedb) dnpurge();		/* flush the cache */
	doquery(name, type);

	if(tmpsrv)
		setserver("");
}
