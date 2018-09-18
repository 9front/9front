#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "dat.h"
#include "fns.h"

typedef struct Webfid Webfid;
typedef struct Client Client;

struct Client
{
	Ref;

	char	request[16];
	Url	*baseurl;
	Url	*url;
	Key	*hdr;

	int	obody;	/* body opend */
	int	cbody;	/* body closed */
	Buq	*qbody;
};

struct Webfid
{
	int	level;

	Client	*client;
	Key	*key;	/* copy for Qheader */
	Buq	*buq;	/* reference for Qbody, Qpost */
};

enum {
	Qroot,
		Qrctl,
		Qclone,
		Qclient,
			Qctl,
			Qbody,
			Qpost,
			Qparsed,
				Qurl,
				Qurlschm,
				Qurluser,
				Qurlpass,
				Qurlhost,
				Qurlport,
				Qurlpath,
				Qurlqwry,
				Qurlfrag,
			Qheader,
};

static char *nametab[] = {
	"/",
		"ctl",
		"clone",
		nil,
			"ctl",
			"body",
			"postbody",
			"parsed",
				"url",
				"scheme",
				"user",
				"passwd",
				"host",
				"port",
				"path",
				"query",
				"fragment",
			nil,
};

static char *mtpt;
static char *service;
static long time0;
static char *user;
static char *agent;
static Client client[64];
static int nclient;

#define	CLIENTID(c)	((int)(((Client*)(c)) - client))

Client*
newclient(void)
{
	Client *cl;
	int i;

	for(i = 0; i < nclient; i++)
		if(client[i].ref == 0)
			break;
	if(i >= nelem(client))
		return nil;
	if(i == nclient)
		nclient++;
	cl = &client[i];
	incref(cl);

	cl->request[0] = 0;
	cl->baseurl = nil;
	cl->url = nil;
	cl->hdr = nil;
	cl->qbody = nil;
	
	return cl;
}

void
freeclient(Client *cl)
{
	Key *k;

	if(cl == nil || decref(cl))
		return;

	buclose(cl->qbody, 0);
	bufree(cl->qbody);

	while(k = cl->hdr){
		cl->hdr = k->next;
		free(k);
	}

	freeurl(cl->url);
	freeurl(cl->baseurl);

	memset(cl, 0, sizeof(*cl));
}

static Url*
clienturl(Client *cl)
{
	static Url nullurl;

	if(cl->qbody && cl->qbody->url)
		return cl->qbody->url;
	if(cl->url)
		return cl->url;
	return &nullurl;
}

static void*
wfaux(Webfid *f)
{
	if(f->level < Qclient)
		return nil;
	else if(f->level < Qurl)
		return f->client;
	else if(f->level < Qheader)
		return clienturl(f->client);
	else
		return f->key;
}

static void
fsmkqid(Qid *q, int level, void *aux)
{
	q->type = 0;
	q->vers = 0;
	switch(level){
	case Qroot:
	case Qparsed:
	case Qclient:
		q->type = QTDIR;
	default:
		q->path = (level<<24) | (((uintptr)aux ^ time0) & 0x00ffffff);
	}
}

static char*
fshdrname(char *s)
{
	char *k, *w;

	for(k=w=s; *k; k++)
		if(isalnum(*k))
			*w++ = tolower(*k);
	*w = 0;
	return s;
}

static int
urlstr(char *buf, int nbuf, Url *u, int level)
{
	char *s;

	if(level == Qurl)
		return snprint(buf, nbuf, "%U", u);
	if(level == Qurlpath)
		return snprint(buf, nbuf, "%s", Upath(u));
	if((s = (&u->scheme)[level - Qurlschm]) == nil){
		buf[0] = 0;
		return 0;
	}
	return snprint(buf, nbuf, "%s", s);
}


static void
fsmkdir(Dir *d, int level, void *aux)
{
	char buf[1024];

	memset(d, 0, sizeof(*d));
	fsmkqid(&d->qid, level, aux);
	d->mode = 0444;
	d->atime = d->mtime = time0;
	d->uid = estrdup(user);
	d->gid = estrdup(user);
	d->muid = estrdup(user);
	if(d->qid.type & QTDIR)
		d->mode |= DMDIR | 0111;
	switch(level){
	case Qheader:
		d->name = fshdrname(estrdup(((Key*)aux)->key));
		d->length = strlen(((Key*)aux)->val);
		break;
	case Qclient:
		snprint(buf, sizeof(buf), "%d", CLIENTID(aux));
		d->name = estrdup(buf);
		break;
	case Qctl:
	case Qrctl:
	case Qclone:
		d->mode = 0666;
		if(0){
	case Qpost:
		d->mode = 0222;
		}
	default:
		d->name = estrdup(nametab[level]);
		if(level >= Qurl && level <= Qurlfrag)
			d->length = urlstr(buf, sizeof(buf), (Url*)aux, level);
	}
}

static void
fsattach(Req *r)
{
	Webfid *f;

	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, "invalid attach specifier");
		return;
	}
	f = emalloc(sizeof(*f));
	f->level = Qroot;
	fsmkqid(&r->fid->qid, f->level, wfaux(f));
	r->ofcall.qid = r->fid->qid;
	r->fid->aux = f;
	respond(r, nil);
}

static void
fsstat(Req *r)
{
	Webfid *f;

	f = r->fid->aux;
	fsmkdir(&r->d, f->level, wfaux(f));
	respond(r, nil);
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	Webfid *f;
	int i, j;

	if(!(fid->qid.type&QTDIR))
		return "walk in non-directory";

	f = fid->aux;
	if(strcmp(name, "..") == 0){
		switch(f->level){
		case Qroot:
			break;
		case Qclient:
			freeclient(f->client);
			f->client = nil;
			break;
		default:
			if(f->level > Qparsed)
				f->level = Qparsed;
			else
				f->level = Qclient;
		}
	} else {
		for(i=f->level+1; i < nelem(nametab); i++){
			if(nametab[i]){
				if(strcmp(name, nametab[i]) == 0)
					break;
				if(i == Qbody && strncmp(name, "body.", 5) == 0)
					break;
			}
			if(i == Qclient){
				j = atoi(name);
				if(j >= 0 && j < nclient){
					f->client = &client[j];
					incref(f->client);
					break;
				}
			}
			if(i == Qheader && f->client && f->client->qbody){
				char buf[128];
				Key *k;

				for(k = f->client->qbody->hdr; k; k = k->next){
					nstrcpy(buf, k->key, sizeof(buf));
					if(!strcmp(name, fshdrname(buf)))
						break;
				}
				if(k != nil){
					/* need to copy as key is owned by qbody wich might go away */
					f->key = addkey(0, k->key, k->val);
					break;
				}
			}
		}
		if(i >= nelem(nametab))
			return "directory entry not found";
		f->level = i;
	}
	fsmkqid(qid, f->level, wfaux(f));
	fid->qid = *qid;
	return nil;
}

static char*
fsclone(Fid *oldfid, Fid *newfid)
{
	Webfid *f, *o;

	o = oldfid->aux;
	if(o == nil || o->key || o->buq)
		return "bad fid";
	f = emalloc(sizeof(*f));
	memmove(f, o, sizeof(*f));
	if(f->client)
		incref(f->client);
	newfid->aux = f;
	return nil;
}

static void
fsopen(Req *r)
{
	Webfid *f;
	Client *cl;

	f = r->fid->aux;
	cl = f->client;
	switch(f->level){
	case Qclone:
		if((cl = newclient()) == nil){
			respond(r, "no more clients");
			return;
		}
		f->level = Qctl;
		f->client = cl;
		fsmkqid(&r->fid->qid, f->level, wfaux(f));
		r->ofcall.qid = r->fid->qid;
		break;
	case Qpost:
		if(cl->qbody && !cl->cbody){
		Inuse:
			respond(r, "client in use");
			return;
		}
	case Qbody:
		if(cl->obody)
			goto Inuse;
		if(cl->cbody){
			bufree(cl->qbody);
			cl->qbody = nil;
			cl->cbody = 0;
		}
		if(cl->qbody == nil){
			char *m;

			if(cl->url == nil){
				respond(r, "no url set");
				return;
			}
			cl->qbody = bualloc(16*1024);
			if(f->level != Qbody){
				f->buq = bualloc(64*1024);
				if(!lookkey(cl->hdr, "Content-Type"))
					cl->hdr = addkey(cl->hdr, "Content-Type", 
						"application/x-www-form-urlencoded");
				m = "POST";
			} else
				m = "GET";
			if(cl->request[0])
				m = cl->request;

			/*
			 * some sites give a 403 Forbidden if we dont include
			 * a meaningless Accept header in the request.
			 */
			if(!lookkey(cl->hdr, "Accept"))
				cl->hdr = addkey(cl->hdr, "Accept", "*/*");

			if(!lookkey(cl->hdr, "Connection"))
				cl->hdr = addkey(cl->hdr, "Connection", "keep-alive");

			if(agent && !lookkey(cl->hdr, "User-Agent"))
				cl->hdr = addkey(cl->hdr, "User-Agent", agent);

			http(m, cl->url, cl->hdr, cl->qbody, f->buq);
			cl->request[0] = 0;
			cl->url = nil;
			cl->hdr = nil;
		}
		if(f->buq)
			break;
		cl->obody = 1;
		incref(cl->qbody);
		bureq(f->buq = cl->qbody, r);
		return;
	}
	respond(r, nil);
}

static int
rootgen(int i, Dir *d, void *)
{
	i += Qroot+1;
	if(i < Qclient){
		fsmkdir(d, i, 0);
		return 0;
	}
	i -= Qclient;
	if(i < nclient){
		fsmkdir(d, Qclient, &client[i]);
		return 0;
	}
	return -1;
}

static int
clientgen(int i, Dir *d, void *aux)
{
	i += Qclient+1;
	if(i > Qparsed){
		Client *cl = aux;
		Key *k;

		i -= Qparsed+1;
		if(cl == nil || cl->qbody == nil)
			return -1;
		for(k = cl->qbody->hdr; i > 0 && k; i--, k = k->next)
			;
		if(k == nil || i > 0)
			return -1;
		i = Qheader;
		aux = k;
	}
	fsmkdir(d, i, aux);
	return 0;
}

static int
parsedgen(int i, Dir *d, void *aux)
{
	i += Qparsed+1;
	if(i > Qurlfrag)
		return -1;
	fsmkdir(d, i, aux);
	return 0;
}

static void
fsread(Req *r)
{
	char buf[1024];
	Webfid *f;

	f = r->fid->aux;
	switch(f->level){
	case Qroot:
		dirread9p(r, rootgen, nil);
		respond(r, nil);
		return;
	case Qclient:
		dirread9p(r, clientgen, f->client);
		respond(r, nil);
		return;
	case Qparsed:
		dirread9p(r, parsedgen, clienturl(f->client));
		respond(r, nil);
		return;
	case Qrctl:
		snprint(buf, sizeof(buf), "useragent %s\ntimeout %d\n", agent, timeout);
	String:
		readstr(r, buf);
		respond(r, nil);
		return;
	case Qctl:
		snprint(buf, sizeof(buf), "%d\n", CLIENTID(f->client));
		goto String;
	case Qheader:
		snprint(buf, sizeof(buf), "%s", f->key->val);
		goto String;
	case Qurl:
	case Qurlschm:
	case Qurluser:
	case Qurlpass:
	case Qurlhost:
	case Qurlport:
	case Qurlpath:
	case Qurlqwry:
	case Qurlfrag:
		urlstr(buf, sizeof(buf), clienturl(f->client), f->level);
		goto String;
	case Qbody:
		bureq(f->buq, r);
		return;
	}
	respond(r, "not implemented");
}

static char*
rootctl(Srv *fs, char *ctl, char *arg)
{
	Url *u;

	if(debug)
		fprint(2, "rootctl: %q %q\n", ctl, arg);

	if(!strcmp(ctl, "useragent")){
		free(agent);
		if(arg && *arg)
			agent = estrdup(arg);
		else
			agent = nil;
		return nil;
	}

	if(!strcmp(ctl, "flushauth")){
		u = nil;
		if(arg && *arg)
			u = saneurl(url(arg, 0));
		flushauth(u, 0);
		freeurl(u);
		return nil;
	}

	if(!strcmp(ctl, "timeout")){
		if(arg && *arg)
			timeout = atoi(arg);
		else
			timeout = 0;
		if(timeout < 0)
			timeout = 0;
		return nil;
	}

	/* ppreemptive authentication only basic
	 * auth supported, ctl message of the form:
	 *    preauth url realm
	 */
	if(!strcmp(ctl, "preauth")){
		char *a[3], buf[256];
		int rc;

		if(tokenize(arg, a, nelem(a)) != 2)
			return "preauth - bad field count";
		if((u = saneurl(url(a[0], 0))) == nil)
			return "preauth - malformed url";
		snprint(buf, sizeof(buf), "BASIC realm=\"%s\"", a[1]);
		srvrelease(fs);
		rc = authenticate(u, u, "GET", buf);
		srvacquire(fs);
		freeurl(u);
		if(rc == -1)
			return "preauth failed";
		return nil;
	}

	return "bad ctl message";
}

static char*
clientctl(Client *cl, char *ctl, char *arg)
{
	char *p;
	Url *u;
	Key *k;

	if(debug)
		fprint(2, "clientctl: %q %q\n", ctl, arg);

	if(!strcmp(ctl, "url")){
		if((u = saneurl(url(arg, cl->baseurl))) == nil)
			return "bad url";
		freeurl(cl->url);
		cl->url = u;
	}
	else if(!strcmp(ctl, "baseurl")){
		if((u = url(arg, 0)) == nil)
			return "bad baseurl";
		freeurl(cl->baseurl);
		cl->baseurl = u;
	}
	else if(!strcmp(ctl, "request")){
		p = cl->request;
		nstrcpy(p, arg, sizeof(cl->request));
		for(; *p && isalpha(*p); p++)
			*p = toupper(*p);
		*p = 0;
	}
	else if(!strcmp(ctl, "headers")){
		while(arg && *arg){
			ctl = arg;
			while(*ctl && strchr(whitespace, *ctl))
				ctl++;
			if(arg = strchr(ctl, '\n'))
				*arg++ = 0;
			if(k = parsehdr(ctl)){
				k->next = cl->hdr;
				cl->hdr = k;
			}
		}
	}
	else {
		char buf[128], **t;
		static char *tab[] = {
			"User-Agent",
			"Content-Type",
			nil,
		};
		for(t = tab; *t; t++){
			nstrcpy(buf, *t, sizeof(buf));
			if(!strcmp(ctl, fshdrname(buf))){
				cl->hdr = delkey(cl->hdr, *t);
				if(arg && *arg)
					cl->hdr = addkey(cl->hdr, *t, arg);
				break;
			}
		}
		if(*t == nil)
			return "bad ctl message";
	}
	return nil;
}

static void
fswrite(Req *r)
{
	int n;
	Webfid *f;
	char *s, *t;

	f = r->fid->aux;
	switch(f->level){
	case Qrctl:
	case Qctl:
		n = r->ofcall.count = r->ifcall.count;
		s = emalloc(n+1);
		memmove(s, r->ifcall.data, n);
		while(n > 0 && strchr("\r\n", s[n-1]))
			n--;
		s[n] = 0;
		t = s;
		while(*t && strchr(whitespace, *t)==0)
			t++;
		while(*t && strchr(whitespace, *t))
			*t++ = 0;
		if(f->level == Qctl)
			t = clientctl(f->client, s, t);
		else
			t = rootctl(r->srv, s, t);
		free(s);
		respond(r, t);
		return;
	case Qpost:
		bureq(f->buq, r);
		return;
	}
	respond(r, "not implemented");
}

static void
fsflush(Req *r)
{
	Webfid *f;
	Req *o;

	if(o = r->oldreq)
	if(f = o->fid->aux)
		buflushreq(f->buq, o);
	respond(r, nil);
}

static void
fsdestroyfid(Fid *fid)
{
	Webfid *f;

	if(f = fid->aux){
		fid->aux = nil;
		if(f->buq){
			buclose(f->buq, 0);
			if(f->client->qbody == f->buq){
				f->client->obody = 0;
				f->client->cbody = 1;
			}
			bufree(f->buq);
		}
		if(f->key)
			free(f->key);
		freeclient(f->client);
		free(f);
	}
}

static void
fsstart(Srv*)
{
	/* drop reference to old webfs mount */
	if(mtpt != nil)
		unmount(nil, mtpt);
}

static void
fsend(Srv*)
{
	postnote(PNGROUP, getpid(), "shutdown");
	exits(nil);
}

Srv fs = 
{
	.start=fsstart,
	.attach=fsattach,
	.stat=fsstat,
	.walk1=fswalk1,
	.clone=fsclone,
	.open=fsopen,
	.read=fsread,
	.write=fswrite,
	.flush=fsflush,
	.destroyfid=fsdestroyfid,
	.end=fsend,
};

void
usage(void)
{
	fprint(2, "usage: %s [-Dd] [-A useragent] [-T timeout] [-m mtpt] [-s service]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char *s;

	quotefmtinstall();
	fmtinstall('U', Ufmt);
	fmtinstall('N', Nfmt);
	fmtinstall('E', Efmt);
	fmtinstall('[', encodefmt);
	fmtinstall('H', encodefmt);

	mtpt = "/mnt/web";
	user = getuser();
	time0 = time(0);
	timeout = 10000;

	ARGBEGIN {
	case 'D':
		chatty9p++;
		break;
	case 'A':
		agent = EARGF(usage());
		break;
	case 'T':
		timeout = atoi(EARGF(usage()));
		if(timeout < 0)
			timeout = 0;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 's':
		service = EARGF(usage());
		break;
	case 'd':
		debug++;
		break;
	default:
		usage();
	} ARGEND;

	rfork(RFNOTEG);

	if(agent == nil)
		agent = "Mozilla/5.0 (compatible; hjdicks)";
	agent = estrdup(agent);

	if(s = getenv("httpproxy")){
		proxy = saneurl(url(s, 0));
		if(proxy == nil || strcmp(proxy->scheme, "http") && strcmp(proxy->scheme, "https"))
			sysfatal("invalid httpproxy url: %s", s);
		free(s);
	}

	postmountsrv(&fs, service, mtpt, MREPL);
	exits(nil);
}
