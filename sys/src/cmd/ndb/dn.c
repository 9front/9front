#include <u.h>
#include <libc.h>
#include <ip.h>
#include <ctype.h>
#include "dns.h"

/*
 *  this comment used to say `our target is 4000 names cached, this should
 *  be larger on large servers'.  dns at Bell Labs starts off with
 *  about 1780 names.
 */
enum {
	/* these settings will trigger frequent aging */
	Deftarget	= 4000,
	Defmaxage	= 60*Min,	/* default domain name max. age */
	Defagefreq	= 15*Min,	/* age names this often (seconds) */
	Minage		=  1*Min,

	/* length of domain name hash table */
	HTLEN		= 4*1024,
};

/*
 *  Hash table for domain names.
 */
static DN *ht[HTLEN];

static struct {
	QLock;
	ulong	names;		/* names allocated */
	ulong	oldest;		/* longest we'll leave a name around */
	ulong	lastage;	/* time of lask dnageall() */
	ushort	id;		/* same size as in packet */
	uchar	mark;		/* current mark bit for gc */
	int	active[2];	/* number of active processes per mark */
} dnvars;

/* names of RR types */
static char *rrtname[] =
{
[Ta]		"ip",
[Tns]		"ns",
[Tmd]		"md",
[Tmf]		"mf",
[Tcname]	"cname",
[Tsoa]		"soa",
[Tmb]		"mb",
[Tmg]		"mg",
[Tmr]		"mr",
[Tnull]		"null",
[Twks]		"wks",
[Tptr]		"ptr",
[Thinfo]	"hinfo",
[Tminfo]	"minfo",
[Tmx]		"mx",
[Ttxt]		"txt",
[Trp]		"rp",
[Tafsdb]	"afsdb",
[Tx25]		"x.25",
[Tisdn]		"isdn",
[Trt]		"rt",
[Tnsap]		"nsap",
[Tnsapptr]	"nsap-ptr",
[Tsig]		"sig",
[Tkey]		"key",
[Tpx]		"px",
[Tgpos]		"gpos",
[Taaaa]		"ipv6",
[Tloc]		"loc",
[Tnxt]		"nxt",
[Teid]		"eid",
[Tnimloc]	"nimrod",
[Tsrv]		"srv",
[Tatma]		"atma",
[Tnaptr]	"naptr",
[Tkx]		"kx",
[Tcert]		"cert",
[Ta6]		"a6",
[Tdname]	"dname",
[Tsink]		"sink",
[Topt]		"opt",
[Tapl]		"apl",
[Tds]		"ds",
[Tsshfp]	"sshfp",
[Tipseckey]	"ipseckey",
[Trrsig]	"rrsig",
[Tnsec]		"nsec",
[Tdnskey]	"dnskey",
[Tspf]		"spf",
[Tuinfo]	"uinfo",
[Tuid]		"uid",
[Tgid]		"gid",
[Tunspec]	"unspec",
[Ttkey]		"tkey",
[Ttsig]		"tsig",
[Tixfr]		"ixfr",
[Taxfr]		"axfr",
[Tmailb]	"mailb",
[Tmaila]	"maila",
[Tall]		"all",
[Tcaa]		"caa",
};

/* names of response codes */
char *rname[Rmask+1] =
{
[Rok]			"ok",
[Rformat]		"format error",
[Rserver]		"server failure",
[Rname]			"bad name",
[Runimplimented]	"unimplemented",
[Rrefused]		"we don't like you",
[Ryxdomain]		"name should not exist",
[Ryxrrset]		"rr set should not exist",
[Rnxrrset]		"rr set should exist",
[Rnotauth]		"not authorative",
[Rnotzone]		"not in zone",
[Rbadvers]		"bad opt version",
/* [Rbadsig]		"bad signature", */
[Rbadkey]		"bad key",
[Rbadtime]		"bad signature time",
[Rbadmode]		"bad mode",
[Rbadname]		"duplicate key name",
[Rbadalg]		"bad algorithm",
};
unsigned nrname = nelem(rname);

/* names of op codes */
char *opname[] =
{
[Oquery]	"query",
[Oinverse]	"inverse query (retired)",
[Ostatus]	"status",
[Oupdate]	"update",
};

int maxage = Defmaxage;
ulong target = Deftarget;
int needrefresh;
ulong now;
uvlong nowms;

static Lock dnlock;
static ulong agefreq = Defagefreq;

static int rrequiv(RR *r1, RR *r2);

static void
ding(void*, char *msg)
{
	if(strstr(msg, "alarm") != nil) {
		noted(NCONT);		/* resume with system call error */
	} else
		noted(NDFLT);		/* die */
}

uvlong
timems(void)
{
	uvlong ms = nsec()/1000000L;
	now = ms/1000L;
	nowms = ms;
	return ms;
}

void
dninit(void)
{
	fmtinstall('E', eipfmt);
	fmtinstall('I', eipfmt);
	fmtinstall('V', eipfmt);
	fmtinstall('\\', bslashfmt);
	fmtinstall('R', rrfmt);
	fmtinstall('Q', rravfmt);
	fmtinstall('H', encodefmt);

	timems();

	if (maxage <= 0)
		maxage = Defmaxage;

	dnvars.names = 0;
	dnvars.oldest = maxage;
	dnvars.lastage = now;
	dnvars.id = 0;
	dnvars.mark = 0;

	notify(ding);
}

/*
 *  hash for a domain name
 */
static ulong
dnhash(char *name)
{
	ulong hash;
	uchar *val = (uchar*)name;

	for(hash = 0; *val; val++)
		hash = hash*13 + tolower(*val)-'a';
	return hash % HTLEN;
}

/*
 *  mark dn with current mark bit
 */
static void
dnmark(DN *dp)
{
	if(dp == nil)
		return;
	dp->mark = (dp->mark & ~1) | dnvars.mark;
}

/*
 *  lookup a symbol.  if enter is not zero and the name is
 *  not found, create it.
 */
DN*
dnlookup(char *name, int class, int enter)
{
	DN **l;
	DN *dp;
	int n;

	l = &ht[dnhash(name)];
	lock(&dnlock);
	for(dp = *l; dp; dp = dp->next) {
		if(dp->class == class && cistrcmp(dp->name, name) == 0)
			goto out;
		l = &dp->next;
	}

	if(!enter){
		unlock(&dnlock);
		return nil;
	}
	dnvars.names++;
	n = strlen(name) + 1;
	dp = emalloc(sizeof(*dp) + n);
	memmove(dp->name, name, n);
	dp->class = class;
	dp->rr = nil;
	/* add new DN to tail of the hash list.  *l points to last next ptr. */
	dp->next = nil;
	*l = dp;
out:
	dnmark(dp);
	unlock(&dnlock);

	return dp;
}

DN*
idnlookup(char *name, int class, int enter)
{
	char dom[Domlen];

	if(utf2idn(name, dom, sizeof dom) >= 0)
		name = dom;
	return dnlookup(name, class, enter);
}

DN*
ipalookup(uchar *ip, int class, int enter)
{
	char addr[64];

	snprint(addr, sizeof(addr), "%I", ip);
	return dnlookup(addr, class, enter);
}

static int
rrsame(RR *rr1, RR *rr2)
{
	return rr1 == rr2 ||
		rr1 != nil && rr2 != nil &&
		rr1->db == rr2->db &&
		rr1->auth == rr2->auth &&
		rrequiv(rr1, rr2);
}

static int
rronlist(RR *rp, RR *lp)
{
	for(; lp; lp = lp->next)
		if (rrsame(lp, rp))
			return 1;
	return 0;
}

/*
 *  purge all records
 */
void
dnpurge(void)
{
	DN *dp;
	RR *rp, *srp;
	int i;

	lock(&dnlock);

	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next){
			srp = rp = dp->rr;
			dp->rr = nil;
			for(; rp != nil; rp = rp->next)
				rp->cached = 0;
			rrfreelist(srp);
		}

	unlock(&dnlock);
}

/*
 *  mark all refernced domain names of an RR.
 *  call with dnlock held.
 */
static void
rrmark(RR *rp)
{
	dnmark(rp->owner);
	if(rp->negative){
		dnmark(rp->negsoaowner);
		return;
	}
	switch(rp->type){
	case Thinfo:
		dnmark(rp->cpu);
		dnmark(rp->os);
		break;
	case Ttxt:
		break;
	case Tcname:
	case Tmb:
	case Tmd:
	case Tmf:
	case Tns:
	case Tmx:
	case Tsrv:
		dnmark(rp->host);
		break;
	case Tmg:
	case Tmr:
		dnmark(rp->mb);
		break;
	case Tminfo:
		dnmark(rp->rmb);
		dnmark(rp->mb);
		break;
	case Trp:
		dnmark(rp->rmb);
		dnmark(rp->rp);
		break;
	case Ta:
	case Taaaa:
		dnmark(rp->ip);
		break;
	case Tptr:
		dnmark(rp->ptr);
		break;
	case Tsoa:
		dnmark(rp->host);
		dnmark(rp->rmb);
		break;
	case Tsig:
		dnmark(rp->sig->signer);
		break;
	case Tcaa:
		dnmark(rp->caa->tag);
		break;
	}
}

/*
 *  delete head of *l and free the old head.
 *  call with dnlock held.
 */
static void
rrdelhead(RR **l)
{
	RR *rp;

	rp = *l;
	if(rp == nil)
		return;
	*l = rp->next;		/* unlink head */
	rp->cached = 0;		/* avoid blowing an assertion in rrfree */
	rrfree(rp);
}

/*
 *  check the age of resource records,
 *  delete any that have timed out and
 *  mark referenced domain names of the remaining records.
 *
 *  note that db records are handled by dbagedb()/dnauthdb()
 *  so they are ignored here.
 *
 *  call with dnlock held.
 */
static void
dnage(DN *dp)
{
	RR **l, *rp;

	l = &dp->rr;
	while ((rp = *l) != nil){
		assert(rp->cached);
		assert(rp->owner == dp);

		if(!rp->db && ((long)(rp->expire - now) <= 0
		|| (long)(now - (rp->expire - rp->ttl)) > dnvars.oldest))
			rrdelhead(l); /* rp == *l before; *l == rp->next after */
		else {
			l = &rp->next;
			rrmark(rp);
		}
	}
}

/*
 *  periodicly sweep for old records and remove unreferenced domain names
 *
 *  this is called once all activity ceased for the non-current
 *  mark bit (previous cycle), meaning there are no more
 *  unaccounted references to DN's with the non-current mark
 *  from other activity slaves.
 *
 *  this can run concurrently to current mark bit activity slaves
 *  as DN's with current mark bit are not freed in this cycle, but
 *  in the next cycle when the previously current mark bit activity
 *  has ceased.
 */
void
dnageall(int doit)
{
	DN *dp, **l;
	int i;

	if(!doit){
		ulong period;

		if(dnvars.names < target){
			dnvars.oldest = maxage;
			return;
		}
		if(dnvars.names < target*2) {
			period = dnvars.oldest / 2;
			if(period > agefreq)
				period = agefreq;
		} else {
			period = Minage / 2;
		}
		if((long)(now - dnvars.lastage) < period)
			return;

		dnslog("more names (%lud) than target (%lud)", dnvars.names, target);

		dnvars.oldest /= 2;
		if(dnvars.oldest < Minage)
			dnvars.oldest = Minage;		/* don't be silly */
	}
	dnvars.lastage = now;

	lock(&dnlock);

	/*
	 * delete all expired records and
	 * mark referenced domain names
	 * of the remaining records.
	 */
	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next)
			dnage(dp);

	/* bump mark */
	dnvars.mark ^= 1;
	assert(dnvars.active[dnvars.mark] == 0);

	/* sweep and remove unreferenced domain names */
	for(i = 0; i < HTLEN; i++){
		l = &ht[i];
		for(dp = *l; dp; dp = *l){
			if(dp->mark == dnvars.mark){
				assert(dp->rr == nil);
				*l = dp->next;

				memset(dp, 0, sizeof *dp); /* cause trouble */
				free(dp);

				dnvars.names--;
				continue;
			}
			l = &dp->next;
		}
	}
	unlock(&dnlock);
}

/*
 *  timeout all database records (used when rereading db)
 */
void
dnagedb(void)
{
	DN *dp;
	int i;
	RR *rp;

	lock(&dnlock);

	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next) {
			for(rp = dp->rr; rp; rp = rp->next)
				if(rp->db)
					rp->expire = 0;
		}

	unlock(&dnlock);
}

/*
 *  mark all local db records about my area as authoritative,
 *  delete timed out ones
 */
void
dnauthdb(void)
{
	int i;
	Area *area;
	DN *dp;
	RR *rp, **l;

	lock(&dnlock);

	for(i = 0; i < HTLEN; i++)
		for(dp = ht[i]; dp; dp = dp->next){
			area = inmyarea(dp->name);
			l = &dp->rr;
			for(rp = *l; rp; rp = *l){
				if(rp->db){
					if(rp->expire == 0){
						rrdelhead(l);
						continue;
					}
					if(area){
						ulong minttl = area->soarr->soa->minttl;
						if(rp->ttl < minttl)
							rp->ttl = minttl;
						rp->auth = 1;
					} else if(rp->type == Tns && inmyarea(rp->host->name))
						rp->auth = 1;
				} else if(area){
					/* no outside spoofing */
					rrdelhead(l);
					continue;
				}
				l = &rp->next;
			}
		}

	unlock(&dnlock);
}

/*
 *  keep track of other processes to know if we can
 *  garbage collect.  block while garbage collecting.
 */
void
getactivity(Request *req)
{
	qlock(&dnvars);
	req->aux = nil;
	req->id = ++dnvars.id;
	req->mark = dnvars.mark;
	dnvars.active[req->mark]++;
	qunlock(&dnvars);
}

void
putactivity(Request *req)
{
	qlock(&dnvars);
	dnvars.active[req->mark]--;
	assert(dnvars.active[req->mark] >= 0);
	if(dnvars.active[dnvars.mark^1] == 0){
		db2cache(needrefresh);
		dnageall(needrefresh);
		needrefresh = 0;
	}
	qunlock(&dnvars);
}

/*
 *  Attach a single resource record to a domain name (new->owner).
 *	- Avoid duplicates with already present RR's
 *	- Chain all RR's of the same type adjacent to one another
 *	- chain authoritative RR's ahead of non-authoritative ones
 *	- remove any expired RR's
 *  If new is a stale duplicate, rrfree it.
 *  Must be called with dnlock held.
 */
static void
rrattach1(RR *new, int auth)
{
	RR **l;
	RR *rp;
	DN *dp;
	ulong ttl;

	assert(!new->cached);

	dp = new->owner;
	assert(dp != nil);
	new->auth |= auth;
	new->next = nil;

	/*
	 * try not to let responses expire before we
	 * can use them to complete this query, by extending
	 * past (or nearly past) expiration time.
	 */
	if(new->db)
		ttl = Year;
	else
		ttl = new->ttl;
	if(ttl <= Min)
		ttl = 10*Min;
	new->expire = now + ttl;

	/*
	 *  find first rr of the right type
	 */
	l = &dp->rr;
	for(rp = *l; rp; rp = *l){
		assert(rp->cached);
		assert(rp->owner == dp);
		if(rp->type == new->type)
			break;
		l = &rp->next;
	}

	/*
	 *  negative entries replace positive entries
	 *  positive entries replace negative entries
	 *  newer entries replace older entries with the same fields
	 *
	 *  look farther ahead than just the next entry when looking
	 *  for duplicates; RRs of a given type can have different rdata
	 *  fields (e.g. multiple NS servers).
	 */
	while ((rp = *l) != nil){
		assert(rp->cached);
		assert(rp->owner == dp);
		if(rp->type != new->type)
			break;

		if(rp->db == new->db && rp->auth == new->auth){
			/* negative drives out positive and vice versa */
			if(rp->negative != new->negative) {
				/* rp == *l before; *l == rp->next after */
				rrdelhead(l);
				continue;	
			}
			/* all things equal, pick the newer one */
			else if(rrequiv(rp, new)){
				/* old drives out new */
				if((long)(rp->expire - new->expire) > 0) {
					rrfree(new);
					return;
				}
				/* rp == *l before; *l == rp->next after */
				rrdelhead(l);
				continue;
			}
			/*
			 *  Hack for pointer records.  This makes sure
			 *  the ordering in the list reflects the ordering
			 *  received or read from the database
			 */
			else if(rp->type == Tptr &&
			    !rp->negative && !new->negative &&
			    rp->ptr->ordinal > new->ptr->ordinal)
				break;
		}
		l = &rp->next;
	}

	if (rronlist(new, rp)) {
		/* should not happen; duplicates were processed above */
		dnslog("adding duplicate %R to list of %R; aborting", new, rp);
		abort();
	}
	/*
	 *  add to chain
	 */
	new->cached = 1;
	new->next = rp;
	*l = new;
}

/*
 *  Attach a list of resource records to a domain name.
 *  May rrfree any stale duplicate RRs; dismembers the list.
 *  Upon return, every RR in the list will have been rrfree-d
 *  or attached to its domain name.
 *  See rrattach1 for properties preserved.
 */
void
rrattach(RR *rp, int auth)
{
	RR *next;

	lock(&dnlock);
	for(; rp; rp = next){
		next = rp->next;
		rp->next = nil;
		/* avoid any outside spoofing */
		if(cfg.cachedb && !rp->db && inmyarea(rp->owner->name)
		|| !rrsupported(rp->type))
			rrfree(rp);
		else
			rrattach1(rp, auth);
	}
	unlock(&dnlock);
}

RR**
rrcopy(RR *rp, RR **last)
{
	RR *nrp;
	SOA *soa;
	Srv *srv;
	Key *key;
	Caa *caa;
	Cert *cert;
	Sig *sig;
	Null *null;
	Txt *t, *nt, **l;

	nrp = rralloc(rp->type);
	switch(rp->type){
	case Tsoa:
		soa = nrp->soa;
		*nrp = *rp;
		nrp->soa = soa;
		*soa = *rp->soa;
		soa->slaves = copyserverlist(rp->soa->slaves);
		break;
	case Tsrv:
		srv = nrp->srv;
		*nrp = *rp;
		nrp->srv = srv;
		*srv = *rp->srv;
		break;
	case Tdnskey:
	case Tkey:
		key = nrp->key;
		*nrp = *rp;
		nrp->key = key;
		*key = *rp->key;
		key->data = emalloc(key->dlen);
		memmove(key->data, rp->key->data, rp->key->dlen);
		break;
	case Tcaa:
		caa = nrp->caa;
		*nrp = *rp;
		nrp->caa = caa;
		*caa = *rp->caa;
		caa->data = emalloc(caa->dlen);
		memmove(caa->data, rp->caa->data, rp->caa->dlen);
		break;
	case Tcert:
		cert = nrp->cert;
		*nrp = *rp;
		nrp->cert = cert;
		*cert = *rp->cert;
		cert->data = emalloc(cert->dlen);
		memmove(cert->data, rp->cert->data, rp->cert->dlen);
		break;
	case Tsig:
		sig = nrp->sig;
		*nrp = *rp;
		nrp->sig = sig;
		*sig = *rp->sig;
		sig->data = emalloc(sig->dlen);
		memmove(sig->data, rp->sig->data, rp->sig->dlen);
		break;
	case Tnull:
		null = nrp->null;
		*nrp = *rp;
		nrp->null = null;
		*null = *rp->null;
		null->data = emalloc(null->dlen);
		memmove(null->data, rp->null->data, rp->null->dlen);
		break;
	case Ttxt:
		*nrp = *rp;
		l = &nrp->txt;
		*l = nil;
		for(t = rp->txt; t != nil; t = t->next){
			nt = emalloc(sizeof(*nt));
			nt->dlen = t->dlen;
			nt->data = emalloc(t->dlen);
			memmove(nt->data, t->data, t->dlen);
			nt->next = nil;
			*l = nt;
			l = &nt->next;
		}
		break;
	default:
		/* cache must only contain supported RR's */
		assert(rrsupported(rp->type));
		*nrp = *rp;
		break;
	}
	nrp->pc = getcallerpc(&rp);
	setmalloctag(nrp, nrp->pc);
	nrp->cached = 0;
	nrp->next = nil;

	rrmark(nrp);

	*last = nrp;
	return &nrp->next;
}

/*
 *  lookup a resource record of a particular type and
 *  class attached to a domain name.  Return copies.
 *
 *  Priority ordering is:
 *	db authoritative
 *	not timed out network authoritative
 *	not timed out network unauthoritative
 *	unauthoritative db
 *
 *  if flag NOneg is set, don't return negative cached entries.
 *  return nothing instead.
 */
RR*
rrlookup(DN *dp, int type, int flag)
{
	RR *rp, *first, **last;

	first = nil;
	last = &first;
	lock(&dnlock);

	/* try for an authoritative db entry */
	for(rp = dp->rr; rp; rp = rp->next){
		assert(rp->cached);
		if(rp->db)
		if(rp->auth)
		if(tsame(type, rp->type))
			last = rrcopy(rp, last);
	}
	if(first)
		goto out;

	/* try for a living authoritative network entry */
	for(rp = dp->rr; rp; rp = rp->next){
		if(!rp->db)
		if(rp->auth)
		if((long)(rp->expire - now) > 0)
 		if(tsame(type, rp->type)){
			if(flag == NOneg && rp->negative)
				goto out;
			last = rrcopy(rp, last);
		}
	}
	if(first)
		goto out;

	/* try for a living unauthoritative network entry */
	for(rp = dp->rr; rp; rp = rp->next){
		if(!rp->db)
		if((long)(rp->expire - now) > 0)
		if(tsame(type, rp->type)){
			if(flag == NOneg && rp->negative)
				goto out;
			last = rrcopy(rp, last);
		}
	}
	if(first)
		goto out;

	/* try for an unauthoritative db entry */
	for(rp = dp->rr; rp; rp = rp->next){
		if(rp->db)
		if(tsame(type, rp->type))
			last = rrcopy(rp, last);
	}
	if(first)
		goto out;

	/* otherwise, settle for anything we got (except for negative caches) */
	for(rp = dp->rr; rp; rp = rp->next)
		if(tsame(type, rp->type)){
			if(rp->negative)
				goto out;
			last = rrcopy(rp, last);
		}

out:
	unlock(&dnlock);
	unique(first);
	return first;
}

static int
inzone(DN *dp, char *name, int namelen, int depth)
{
	int n;

	for(n = 0; dp->name[n]; n++)
		if(dp->name[n] == '.')
			depth--;

	if(depth != 1 || n < namelen)
		return 0;
	if(cistrcmp(name, dp->name + n - namelen) != 0)
		return 0;
	if(n > namelen && dp->name[n - namelen - 1] != '.')
		return 0;
	return 1;
}

/*
 *  return all resources (except SOA) of a zone.
 */
RR*
rrgetzone(char *name)
{	
	int found, depth, h, n;
	RR *rp, *first, **l;
	DN *dp;

	for(n = 0, depth = 1; name[n]; n++)
		if(name[n] == '.')
			depth++;

	first = nil;
	l = &first;
	lock(&dnlock);
	do {
		found = 0;
		for(h = 0; h < HTLEN; h++)
			for(dp = ht[h]; dp; dp = dp->next)
				if(inzone(dp, name, n, depth)){
					for(rp = dp->rr; rp; rp = rp->next){
						/*
						 * there shouldn't be negatives,
						 * but just in case.
						 * don't send any soa's,
						 * ns's are enough.
						 */
						if (rp->negative ||
						    rp->type == Tsoa)
							continue;
						l = rrcopy(rp, l);
					}
					found = 1;
				}
		depth++;
	} while(found);
	unlock(&dnlock);

	return first;
}

/*
 *  convert an ascii RR type name to its integer representation
 */
int
rrtype(char *atype)
{
	int i;

	for(i = 0; i < nelem(rrtname); i++)
		if(rrtname[i] && strcmp(rrtname[i], atype) == 0)
			return i;

	/* make any a synonym for all */
	if(strcmp(atype, "any") == 0)
		return Tall;
	else if(isascii(atype[0]) && isdigit(atype[0]))
		return atoi(atype);
	else
		return -1;
}

/*
 *  return 0 if not a supported rr type
 */
int
rrsupported(int type)
{
	if(type < 0 || type >= nelem(rrtname))
		return 0;
	return rrtname[type] != nil;
}

/*
 *  compare 2 types
 */
int
tsame(int t1, int t2)
{
	return t1 == t2 || t1 == Tall;
}

/*
 *  Add resource records to a list.
 */
RR*
rrcat(RR **start, RR *rp)
{
	RR *olp, *nlp;
	RR **last;

	/* check for duplicates */
	for (olp = *start; 0 && olp; olp = olp->next)
		for (nlp = rp; nlp; nlp = nlp->next)
			if (rrsame(nlp, olp))
				dnslog("rrcat: duplicate RR: %R", nlp);
	USED(olp);

	last = start;
	while(*last != nil)
		last = &(*last)->next;

	*last = rp;
	return *start;
}

RR*
rrremfilter(RR **l, int (*filter)(RR*, void*), void *arg)
{
	RR *first, *rp;
	RR **nl;

	first = nil;
	nl = &first;
	while(*l != nil){
		rp = *l;
		if((*filter)(rp, arg)){
			*l = rp->next;
			*nl = rp;
			nl = &rp->next;
			*nl = nil;
		} else
			l = &(*l)->next;
	}

	return first;
}

static int
filterneg(RR *rp, void*)
{
	return rp->negative;
}
static int
filtertype(RR *rp, void *arg)
{
	return rp->type == *((int*)arg);
}
static int
filterowner(RR *rp, void *arg)
{
	return rp->owner == (DN*)arg;
}

/*
 *  remove negative cache rr's from an rr list
 */
RR*
rrremneg(RR **l)
{
	return rrremfilter(l, filterneg, nil);
}

/*
 *  remove rr's of a particular type from an rr list
 */
RR*
rrremtype(RR **l, int type)
{
	return rrremfilter(l, filtertype, &type);
}

/*
 *  remove rr's of a particular owner from an rr list
 */
RR*
rrremowner(RR **l, DN *owner)
{
	return rrremfilter(l, filterowner, owner);
}

static char *
dnname(DN *dn)
{
	return dn? dn->name: "<null>";
}

static char *
idnname(DN *dn, char *buf, int nbuf)
{
	char *name;

	name = dnname(dn);
	if(idn2utf(name, buf, nbuf) >= 0)
		return buf;
	return name;
}

/*
 *  txt rr strings can contain binary data such as
 *  control characters and double quotes (") which would
 *  collide with ndb(6) format.
 *  escape special characters by encoding them as: \DDD
 *  where D is a decimal digit. backslash (\) is escaped
 *  by doubling. valid utf8 is encoded verbatim.
 */
int
bslashfmt(Fmt *f)
{
	int len, out, n, c;
	uchar *data;

	out = 0;
	len = f->prec;
	f->prec = 0;
	f->flags &= ~FmtPrec;
	data = va_arg(f->args, uchar*);
	for(; len > 0; data += n, len -= n){
		if(*data >= Runeself && fullrune((char*)data, len)){
			Rune r;

			n = chartorune(&r, (char*)data);
			if(r != Runeerror){
				out += fmtprint(f, "%C", r);
				continue;
			}
		}
		c = *data;
		if(c < ' ' || c == '"' || c > '~')
			out += fmtprint(f, "\\%.3d", c);
		else if(c == '\\')
			out += fmtprint(f, "\\\\");
		else
			out += fmtprint(f, "%c", c);
		n = 1;
	}
	return out;
}

/*
 *  print conversion for rr records
 */
int
rrfmt(Fmt *f)
{
	int rv;
	char *strp, buf[Domlen];
	Fmt fstr;
	RR *rp;
	Server *s;
	SOA *soa;
	Srv *srv;
	Txt *t;

	fmtstrinit(&fstr);

	rp = va_arg(f->args, RR*);
	if(rp == nil){
		fmtprint(&fstr, "<null>");
		goto out;
	}

	fmtprint(&fstr, "%s %s", dnname(rp->owner),
		rrname(rp->type, buf, sizeof buf));

	if(rp->negative){
		fmtprint(&fstr, "\tnegative - rcode %d", rp->negrcode);
		goto out;
	}

	switch(rp->type){
	case Thinfo:
		fmtprint(&fstr, "\t%s %s", dnname(rp->cpu), dnname(rp->os));
		break;
	case Tcname:
	case Tmb:
	case Tmd:
	case Tmf:
	case Tns:
		fmtprint(&fstr, "\t%s", dnname(rp->host));
		break;
	case Tmg:
	case Tmr:
		fmtprint(&fstr, "\t%s", dnname(rp->mb));
		break;
	case Tminfo:
		fmtprint(&fstr, "\t%s %s", dnname(rp->mb), dnname(rp->rmb));
		break;
	case Tmx:
		fmtprint(&fstr, "\t%lud %s", rp->pref, dnname(rp->host));
		break;
	case Ta:
	case Taaaa:
		fmtprint(&fstr, "\t%s", dnname(rp->ip));
		break;
	case Tptr:
		fmtprint(&fstr, "\t%s", dnname(rp->ptr));
		break;
	case Tsoa:
		soa = rp->soa;
		fmtprint(&fstr, "\t%s %s %lud %lud %lud %lud %lud",
			dnname(rp->host), dnname(rp->rmb),
			(soa? soa->serial: 0),
			(soa? soa->refresh: 0), (soa? soa->retry: 0),
			(soa? soa->expire: 0), (soa? soa->minttl: 0));
		if (soa)
			for(s = soa->slaves; s != nil; s = s->next)
				fmtprint(&fstr, " %s", s->name);
		break;
	case Tsrv:
		srv = rp->srv;
		fmtprint(&fstr, "\t%ud %ud %ud %s",
			(srv? srv->pri: 0), (srv? srv->weight: 0),
			rp->port, dnname(rp->host));
		break;
	case Tnull:
		if (rp->null == nil)
			fmtprint(&fstr, "\t<null>");
		else
			fmtprint(&fstr, "\t%.*H", rp->null->dlen,
				rp->null->data);
		break;
	case Ttxt:
		fmtprint(&fstr, "\t");
		for(t = rp->txt; t != nil; t = t->next)
			fmtprint(&fstr, "%.*\\", t->dlen, t->data);
		break;
	case Trp:
		fmtprint(&fstr, "\t%s %s", dnname(rp->rmb), dnname(rp->rp));
		break;
	case Tdnskey:
	case Tkey:
		if (rp->key == nil)
			fmtprint(&fstr, "\t<null> <null> <null>");
		else
			fmtprint(&fstr, "\t%d %d %d", rp->key->flags,
				rp->key->proto, rp->key->alg);
		break;
	case Tsig:
		if (rp->sig == nil)
			fmtprint(&fstr,
		   "\t<null> <null> <null> <null> <null> <null> <null> <null>");
		else
			fmtprint(&fstr, "\t%d %d %d %lud %lud %lud %d %s",
				rp->sig->type, rp->sig->alg, rp->sig->labels,
				rp->sig->ttl, rp->sig->exp, rp->sig->incep,
				rp->sig->tag, dnname(rp->sig->signer));
		break;
	case Tcert:
		if (rp->cert == nil)
			fmtprint(&fstr, "\t<null> <null> <null>");
		else
			fmtprint(&fstr, "\t%d %d %d",
				rp->cert->type, rp->cert->tag, rp->cert->alg);
		break;
	case Tcaa:
		if (rp->caa == nil)
			fmtprint(&fstr, "\t<null> <null> <null>");
		else
			fmtprint(&fstr, "\t%d %s %.*\\",
				rp->caa->flags, dnname(rp->caa->tag),
				rp->caa->dlen, rp->caa->data);
		break;
	default:
		if(rrsupported(rp->type))
			break;
		if (rp->unknown == nil)
			fmtprint(&fstr, "\t<null>");
		else
			fmtprint(&fstr, "\t%.*H",
				rp->unknown->dlen,
				rp->unknown->data);
	}
out:
	strp = fmtstrflush(&fstr);
	rv = fmtstrcpy(f, strp);
	free(strp);
	return rv;
}

/*
 *  print conversion for rr records in attribute value form
 */
int
rravfmt(Fmt *f)
{
	int rv;
	char *strp, buf[Domlen];
	Fmt fstr;
	RR *rp;
	Server *s;
	SOA *soa;
	Srv *srv;
	Txt *t;

	fmtstrinit(&fstr);

	rp = va_arg(f->args, RR*);
	if(rp == nil){
		fmtprint(&fstr, "<null>");
		goto out;
	}

	if(rp->type == Tptr)
		fmtprint(&fstr, "ptr=%s", dnname(rp->owner));
	else
		fmtprint(&fstr, "dom=%s", idnname(rp->owner, buf, sizeof(buf)));

	switch(rp->type){
	case Thinfo:
		fmtprint(&fstr, " cpu=%s os=%s", dnname(rp->cpu), dnname(rp->os));
		break;
	case Tcname:
		fmtprint(&fstr, " cname=%s", idnname(rp->host, buf, sizeof(buf)));
		break;
	case Tmb:
	case Tmd:
	case Tmf:
		fmtprint(&fstr, " mbox=%s", idnname(rp->host, buf, sizeof(buf)));
		break;
	case Tns:
		fmtprint(&fstr,  " ns=%s", idnname(rp->host, buf, sizeof(buf)));
		break;
	case Tmg:
	case Tmr:
		fmtprint(&fstr, " mbox=%s", idnname(rp->mb, buf, sizeof(buf)));
		break;
	case Tminfo:
		fmtprint(&fstr, " mbox=%s", idnname(rp->mb, buf, sizeof(buf)));
		fmtprint(&fstr, " mbox=%s", idnname(rp->rmb, buf, sizeof(buf)));
		break;
	case Tmx:
		fmtprint(&fstr, " pref=%lud mx=%s", rp->pref,
			idnname(rp->host, buf, sizeof(buf)));
		break;
	case Ta:
	case Taaaa:
		fmtprint(&fstr, " ip=%s", dnname(rp->ip));
		break;
	case Tptr:
		fmtprint(&fstr, " dom=%s", idnname(rp->ptr, buf, sizeof(buf)));
		break;
	case Tsoa:
		soa = rp->soa;
		fmtprint(&fstr, " ns=%s", idnname(rp->host, buf, sizeof(buf)));
		fmtprint(&fstr, " mbox=%s", idnname(rp->rmb, buf, sizeof(buf)));
		fmtprint(&fstr,
" serial=%lud refresh=%lud retry=%lud expire=%lud ttl=%lud",
			(soa? soa->serial: 0),
			(soa? soa->refresh: 0), (soa? soa->retry: 0),
			(soa? soa->expire: 0), (soa? soa->minttl: 0));
		for(s = soa->slaves; s != nil; s = s->next)
			fmtprint(&fstr, " dnsslave=%s", s->name);
		break;
	case Tsrv:
		srv = rp->srv;
		fmtprint(&fstr, " pri=%ud weight=%ud port=%ud target=%s",
			(srv? srv->pri: 0), (srv? srv->weight: 0),
			rp->port, idnname(rp->host, buf, sizeof(buf)));
		break;
	case Tnull:
		if (rp->null == nil)
			fmtprint(&fstr, " null=<null>");
		else
			fmtprint(&fstr, " null=%.*H", rp->null->dlen,
				rp->null->data);
		break;
	case Ttxt:
		fmtprint(&fstr, " txt=\"");
		for(t = rp->txt; t != nil; t = t->next)
			fmtprint(&fstr, "%.*\\", t->dlen, t->data);
		fmtprint(&fstr, "\"");
		break;
	case Trp:
		fmtprint(&fstr, " mbox=%s", idnname(rp->rmb, buf, sizeof(buf)));
		fmtprint(&fstr, " rp=%s", idnname(rp->rp, buf, sizeof(buf)));
		break;
	case Tdnskey:
	case Tkey:
		if (rp->key == nil)
			fmtprint(&fstr, " flags=<null> proto=<null> alg=<null>");
		else
			fmtprint(&fstr, " flags=%d proto=%d alg=%d",
				rp->key->flags, rp->key->proto, rp->key->alg);
		break;
	case Tsig:
		if (rp->sig == nil)
			fmtprint(&fstr,
" type=<null> alg=<null> labels=<null> ttl=<null> exp=<null> incep=<null> tag=<null> signer=<null>");
		else
			fmtprint(&fstr,
" type=%d alg=%d labels=%d ttl=%lud exp=%lud incep=%lud tag=%d signer=%s",
				rp->sig->type, rp->sig->alg, rp->sig->labels,
				rp->sig->ttl, rp->sig->exp, rp->sig->incep,
				rp->sig->tag, idnname(rp->sig->signer, buf, sizeof(buf)));
		break;
	case Tcert:
		if (rp->cert == nil)
			fmtprint(&fstr, " type=<null> tag=<null> alg=<null>");
		else
			fmtprint(&fstr, " type=%d tag=%d alg=%d",
				rp->cert->type, rp->cert->tag, rp->cert->alg);
		break;
	case Tcaa:
		if (rp->caa == nil)
			fmtprint(&fstr, " flags=<null> tag=<null> caa=<null>");
		else
			fmtprint(&fstr, " flags=%d tag=%s caa=\"%.*\\\"",
				rp->caa->flags, dnname(rp->caa->tag),
				rp->caa->dlen, rp->caa->data);
		break;
	default:
		if (rp->unknown == nil)
			fmtprint(&fstr, " type%d=<null>", rp->type);
		else
			fmtprint(&fstr, " type%d=%.*H", rp->type,
				rp->unknown->dlen,
				rp->unknown->data);
	}
out:
	strp = fmtstrflush(&fstr);
	rv = fmtstrcpy(f, strp);
	free(strp);
	return rv;
}

void
warning(char *fmt, ...)
{
	char dnserr[256];
	va_list arg;

	va_start(arg, fmt);
	vseprint(dnserr, dnserr+sizeof(dnserr), fmt, arg);
	va_end(arg);
	syslog(1, logfile, dnserr);		/* on console too */
}

void
dnslog(char *fmt, ...)
{
	char dnserr[256];
	va_list arg;

	va_start(arg, fmt);
	vseprint(dnserr, dnserr+sizeof(dnserr), fmt, arg);
	va_end(arg);
	syslog(0, logfile, dnserr);
}

/*
 *  create a slave process to handle a request to avoid one request blocking
 *  another
 */
void
slave(Request *req)
{
	int ppid, procs;

	if(req->isslave)
		return;		/* we're already a slave process */

	procs = dnvars.active[0] + dnvars.active[1];
	if(procs >= Maxactive){
		dnslog("%d: [%d] too much activity", req->id, getpid());
		return;
	}

	/*
	 * parent returns to main loop, child does the work.
	 * don't change note group.
	 */
	ppid = getpid();
	switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		break;
	case 0:
		procsetname("request slave of pid %d", ppid);

		/*
		 * this relies on rfork producing separate, initially-identical
		 * stacks, thus giving us two copies of `req', one in each
		 * process.
		 */
		req->isslave = 1;
		break;
	default:
		if(++procs > stats.slavehiwat)
			stats.slavehiwat = procs;
		alarm(0);
		longjmp(req->mret, 1);
	}
}

static int
blockequiv(Block *a, Block *b)
{
	return	a->dlen == b->dlen &&
		memcmp(a->data, b->data, a->dlen) == 0;
}

static int
keyequiv(Key *a, Key *b)
{
	return	a->flags == b->flags &&
		a->proto == b->proto &&
		a->alg == b->alg &&
		blockequiv(a, b);
}

static int
certequiv(Cert *a, Cert *b)
{
	return	a->type == a->type &&
		a->tag == a->tag &&
		a->alg == a->alg &&
		blockequiv(a, b);
}

static int
txtequiv(Txt *a, Txt *b)
{
	uchar *ap, *ae, *bp, *be;
	int n;

	for(ap = ae = bp = be = nil;;ap += n, bp += n){
		while(a != nil && (ap == nil || (ap >= ae && (a = a->next) != nil)))
			ap = a->data, ae = ap + a->dlen;
		while(b != nil && (bp == nil || (bp >= be && (b = b->next) != nil)))
			bp = b->data, be = bp + b->dlen;
		if(a == b || a == nil || b == nil)
			break;
		n = ae - ap;
		if(be - bp < n)
			n = be - bp;
		if(memcmp(ap, bp, n) != 0)
			return 0;
	}
	return a == b;
}

static int
rrequiv(RR *r1, RR *r2)
{
	if(r1->owner != r2->owner
	|| r1->type != r2->type
	|| r1->arg0 != r2->arg0
	|| r1->arg1 != r2->arg1)
		return 0;
	switch(r1->type){
	case Tkey:
		return keyequiv(r1->key, r2->key);
	case Tcert:
		return certequiv(r1->cert, r2->cert);
	case Tsig:
		return r1->sig->signer == r2->sig->signer && certequiv(r1->sig, r2->sig);
	case Tnull:
		return blockequiv(r1->null, r2->null);
	case Ttxt:
		return txtequiv(r1->txt, r2->txt);
	case Tcaa:
		return r1->caa->flags == r2->caa->flags && r1->caa->tag == r2->caa->tag && blockequiv(r1->caa, r2->caa);
	default:
		if(!rrsupported(r1->type))
			return 0;	/* unknown never equal */
	}
	return 1;
}

void
unique(RR *rp)
{
	RR **l, *nrp;

	for(; rp; rp = rp->next){
		l = &rp->next;
		for(nrp = *l; nrp; nrp = *l)
			if(rrequiv(rp, nrp)){
				*l = nrp->next;
				rrfree(nrp);
			} else
				l = &nrp->next;
	}
}

/*
 *  true if second domain is subsumed by the first
 */
int
subsume(char *higher, char *lower)
{
	int hn, ln;

	ln = strlen(lower);
	hn = strlen(higher);
	if (ln < hn || cistrcmp(lower + ln - hn, higher) != 0 ||
	    ln > hn && hn != 0 && lower[ln - hn - 1] != '.')
		return 0;
	return 1;
}

/*
 *  randomize the order we return items to provide some
 *  load balancing for servers.
 *
 *  only randomize the first class of entries
 */
RR*
randomize(RR *rp)
{
	RR *first, *last, *x, *base;
	ulong n;

	if(rp == nil || rp->next == nil)
		return rp;

	/* just randomize addresses, mx's and ns's */
	for(x = rp; x; x = x->next)
		if(x->type != Ta && x->type != Taaaa &&
		    x->type != Tmx && x->type != Tns)
			return rp;

	base = rp;

	n = rand();
	last = first = nil;
	while(rp != nil){
		/* stop randomizing if we've moved past our class */
		if(base->auth != rp->auth || base->db != rp->db){
			last->next = rp;
			break;
		}

		/* unchain */
		x = rp;
		rp = x->next;
		x->next = nil;

		if(n&1){
			/* add to tail */
			if(last == nil)
				first = x;
			else
				last->next = x;
			last = x;
		} else {
			/* add to head */
			if(last == nil)
				last = x;
			x->next = first;
			first = x;
		}

		/* reroll the dice */
		n >>= 1;
	}

	return first;
}

void*
emalloc(int size)
{
	char *x;

	x = mallocz(size, 1);
	if(x == nil)
		abort();
	setmalloctag(x, getcallerpc(&size));
	return x;
}

char*
estrdup(char *s)
{
	char *p;
	int n;

	n = strlen(s) + 1;
	p = mallocz(n, 0);
	if(p == nil)
		abort();
	memmove(p, s, n);
	setmalloctag(p, getcallerpc(&s));
	return p;
}

/*
 *  create a pointer record
 */
static RR*
mkptr(DN *dp, char *ptr, ulong ttl)
{
	DN *ipdp;
	RR *rp;

	ipdp = dnlookup(ptr, Cin, 1);

	rp = rralloc(Tptr);
	rp->ptr = dp;
	rp->owner = ipdp;
	rp->db = 1;
	if(ttl)
		rp->ttl = ttl;
	return rp;
}

void	bytes2nibbles(uchar *nibbles, uchar *bytes, int nbytes);

/*
 *  look for all ip addresses in this network and make
 *  pointer records for them.
 */
void
dnptr(uchar *net, uchar *mask, char *dom, int type, int subdoms, int ttl)
{
	int i, j, len;
	char *p, *e;
	char ptr[Domlen];
	uchar *ipp;
	uchar ip[IPaddrlen], nnet[IPaddrlen];
	uchar nibip[IPaddrlen*2];
	RR *rp, *first, **l;
	DN *dp;

	l = &first;
	first = nil;

	lock(&dnlock);
	for(i = 0; i < HTLEN; i++){
		for(dp = ht[i]; dp; dp = dp->next){
			for(rp = dp->rr; rp; rp = rp->next){
				if(rp->type != type || rp->negative)
					continue;
				if(parseip(ip, rp->ip->name) == -1)
					continue;
				maskip(ip, mask, nnet);
				if(ipcmp(net, nnet) != 0)
					continue;
				l = rrcopy(rp, l);
			}
		}
	}
	unlock(&dnlock);

	for(rp = first; rp; rp = rp->next){
		if(parseip(ip, rp->ip->name) == -1)
			continue;
		maskip(ip, mask, nnet);
		if(ipcmp(net, nnet) != 0)
			continue;

		ipp = ip;
		len = IPaddrlen;
		if (type == Taaaa) {
			bytes2nibbles(nibip, ip, IPaddrlen);
			ipp = nibip;
			len = 2*IPaddrlen;
		}

		p = ptr;
		e = ptr+sizeof(ptr);
		for(j = len - 1; j >= len - subdoms; j--)
			p = seprint(p, e, (type == Ta?
				"%d.": "%x."), ipp[j]);
		seprint(p, e, "%s", dom);

		rrattach(mkptr(rp->owner, ptr, ttl), Authoritative);
	}
	rrfreelist(first);
}

void
addserver(Server **l, char *name)
{
	Server *s;
	int n;

	while(*l)
		l = &(*l)->next;
	n = strlen(name) + 1;
	s = emalloc(sizeof(*s) + n);
	s->name = (char*)(s+1);
	memmove(s->name, name, n);
	s->next = nil;
	*l = s;
}

Server*
copyserverlist(Server *s)
{
	Server *ns;

	for(ns = nil; s != nil; s = s->next)
		addserver(&ns, s->name);
	return ns;
}


/* from here down is copied to ip/snoopy/dns.c periodically to update it */

/*
 *  convert an integer RR type to it's ascii name
 */
char*
rrname(int type, char *buf, int len)
{
	char *t;

	t = nil;
	if(type >= 0 && type < nelem(rrtname))
		t = rrtname[type];
	if(t==nil){
		snprint(buf, len, "%d", type);
		t = buf;
	}
	return t;
}

/*
 *  free a list of resource records and any related structs
 */
void
rrfreelist(RR *rp)
{
	RR *next;

	for(; rp; rp = next){
		next = rp->next;
		rrfree(rp);
	}
}

void
freeserverlist(Server *s)
{
	Server *next;

	for(; s != nil; s = next){
		next = s->next;
		memset(s, 0, sizeof *s);	/* cause trouble */
		free(s);
	}
}

/*
 *  allocate a resource record of a given type
 */
RR*
rralloc(int type)
{
	RR *rp;

	assert((type & ~0xFFFF) == 0);
	rp = emalloc(sizeof(*rp));
	rp->pc = getcallerpc(&type);
	rp->type = type;
	setmalloctag(rp, rp->pc);
	switch(type){
	case Tsoa:
		rp->soa = emalloc(sizeof(*rp->soa));
		rp->soa->slaves = nil;
		setmalloctag(rp->soa, rp->pc);
		break;
	case Tsrv:
		rp->srv = emalloc(sizeof(*rp->srv));
		setmalloctag(rp->srv, rp->pc);
		break;
	case Tdnskey:
	case Tkey:
		rp->key = emalloc(sizeof(*rp->key));
		setmalloctag(rp->key, rp->pc);
		break;
	case Tcaa:
		rp->caa = emalloc(sizeof(*rp->caa));
		setmalloctag(rp->caa, rp->pc);
		break;
	case Tcert:
		rp->cert = emalloc(sizeof(*rp->cert));
		setmalloctag(rp->cert, rp->pc);
		break;
	case Tsig:
		rp->sig = emalloc(sizeof(*rp->sig));
		setmalloctag(rp->sig, rp->pc);
		break;
	case Tnull:
		rp->null = emalloc(sizeof(*rp->null));
		setmalloctag(rp->null, rp->pc);
		break;
	default:
		if(rrsupported(type))
			break;
		rp->unknown = emalloc(sizeof(*rp->unknown));
		setmalloctag(rp->unknown, rp->pc);
	}
	rp->ttl = 0;
	rp->expire = 0;
	rp->next = nil;
	return rp;
}

/*
 *  free a resource record and any related structs
 */
void
rrfree(RR *rp)
{
	Txt *t;

	assert(!rp->cached);

	switch(rp->type){
	case Tsoa:
		freeserverlist(rp->soa->slaves);
		memset(rp->soa, 0, sizeof *rp->soa);	/* cause trouble */
		free(rp->soa);
		break;
	case Tsrv:
		memset(rp->srv, 0, sizeof *rp->srv);	/* cause trouble */
		free(rp->srv);
		break;
	case Tdnskey:
	case Tkey:
		free(rp->key->data);
		memset(rp->key, 0, sizeof *rp->key);	/* cause trouble */
		free(rp->key);
		break;
	case Tcert:
		free(rp->cert->data);
		memset(rp->cert, 0, sizeof *rp->cert);	/* cause trouble */
		free(rp->cert);
		break;
	case Tsig:
		free(rp->sig->data);
		memset(rp->sig, 0, sizeof *rp->sig);	/* cause trouble */
		free(rp->sig);
		break;
	case Tnull:
		free(rp->null->data);
		memset(rp->null, 0, sizeof *rp->null);	/* cause trouble */
		free(rp->null);
		break;
	case Tcaa:
		free(rp->caa->data);
		memset(rp->caa, 0, sizeof *rp->caa);	/* cause trouble */
		free(rp->caa);
		break;
	case Ttxt:
		while(t = rp->txt){
			rp->txt = t->next;
			free(t->data);
			memset(t, 0, sizeof *t);	/* cause trouble */
			free(t);
		}
		break;
	default:
		if(rrsupported(rp->type))
			break;
		free(rp->unknown->data);
		memset(rp->unknown, 0, sizeof *rp->unknown);	/* cause trouble */
		free(rp->unknown);
		break;
	}
	memset(rp, 0, sizeof *rp);		/* cause trouble */
	free(rp);
}
