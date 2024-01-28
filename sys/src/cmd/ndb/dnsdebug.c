#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <ip.h>
#include <ndb.h>
#include "dns.h"

Cfg cfg;

char	*dbfile;
char	*logfile = "dnsdebug";
int	debug;
char	mntpt[Maxpath];

static char *servername;

void	docmd(int, char**);
void	doquery(char*, char*);
int	setserver(char*);

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
	char tname[32], *strp, *t;
	Fmt fstr;
	RR *rp;
	int rv;

	fmtstrinit(&fstr);
	rp = va_arg(f->args, RR*);
	fmtprint(&fstr, "%R", rp);
	strp = fmtstrflush(&fstr);
	if((t = strchr(strp, '\t')) == nil || rp == nil)
		rv = fmtstrcpy(f, strp);
	else
		rv = fmtprint(f, "%-32.32s %-15.15s %-5.5s%s",
			rp->owner->name, longtime(rp->ttl),
			rrname(rp->type, tname, sizeof tname), t);
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
logreply(int id, char *rcvd, uchar *addr, DNSmsg *mp)
{
	char tname[32];
	RR *rp;

	print("%d: %s %I %s (%s%s%s%s%s)\n", id, rcvd, addr, rcname(getercode(mp)),
		mp->flags & Fauth? "authoritative": "",
		mp->flags & Ftrunc? " truncated": "",
		mp->flags & Frecurse? " recurse": "",
		mp->flags & Fcanrec? " can_recurse": "",
		(mp->flags & (Fauth|Rmask)) == (Fauth|Rname)? " nx": "");
	for(rp = mp->qd; rp != nil; rp = rp->next)
		print("\tQ:    %s %s\n", rp->owner->name,
			rrname(rp->type, tname, sizeof tname));
	logsection("Ans:  ", mp->an);
	logsection("Auth: ", mp->ns);
	logsection("Hint: ", mp->ar);
}

void
logrequest(int id, int depth, char *send, uchar *addr, char *sname, char *rname, int type)
{
	char tname[32];

	print("%d.%d: %s %I/%s %s %s\n", id, depth, send,
		addr, sname, rname, rrname(type, tname, sizeof tname));
}

RR*
getdnsservers(int class)
{
	uchar ip[IPaddrlen];
	DN *nsdp;
	RR *rp, *ns;
	char name[64];

	if(servername == nil)
		return dnsservers(class);

	snprint(name, sizeof name, "override#%s#server", servername[0] == '!' ? "dot" : "dns");
	ns = rralloc(Tns);
	if(parseip(ip, servername+1) == -1){
		nsdp = idnlookup(servername+1, class, 1);
	} else {
		nsdp = dnlookup(name, class, 1);
		rp = rralloc(isv4(ip) ? Ta : Taaaa);
		rp->owner = nsdp;
		rp->ip = ipalookup(ip, class, 1);
		rp->db = 1;
		rp->ttl = 10*Min;
		rrattach(rp, Authoritative);
	}
	ns->owner = dnlookup(name, class, 1);
	ns->host = nsdp;
	return ns;
}

int
setserver(char *server)
{
	if(servername != nil){
		free(servername);
		servername = nil;
		cfg.resolver = 0;
	}
	if(server == nil || server[0] == 0 || server[1] == 0)
		return 0;
	servername = estrdup(server);
	cfg.resolver = 1;
	return 0;
}

void
doquery(char *name, char *tstr)
{
	int len, type, rooted;
	char buf[1024];
	RR *rr, *rp;
	Request req;

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
	req.from = argv0;

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

	if(n == 1 && strcmp(f[0], "refresh") == 0){
		db2cache(1);
		dnageall(1);
		return;
	}

	name = type = nil;
	tmpsrv = 0;

	if(*f[0] == '@' || *f[0] == '!') {
		if(setserver(f[0]) < 0)
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
		setserver("@");
}
