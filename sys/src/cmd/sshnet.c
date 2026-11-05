/*
 * SSH network file system.
 * Presents remote TCP stack as /net-style file system.
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>

typedef struct Client Client;
typedef struct Msg Msg;

enum
{
	Qroot,
	Qcs,
	Qtcp,
	Qclone,
	Qn,
	Qctl,
	Qdata,
	Qlocal,
	Qremote,
	Qstatus,
	Qlisten,
};

#define PATH(type, n)		((type)|((n)<<8))
#define TYPE(path)		((int)(path) & 0xFF)
#define NUM(path)		((uint)(path)>>8)

int sessionopen = 0;
Channel *sshmsgchan;		/* chan(Msg*) */
Channel *fsreqchan;		/* chan(Req*) */
Channel *fsreqwaitchan;		/* chan(nil) */
Channel *fsclunkchan;		/* chan(Fid*) */
Channel *fsclunkwaitchan;	/* chan(nil) */
ulong time0;

enum
{
	Closed,
	Dialing,
	Listen,
	Established,
	Teardown,
	Finished,
};

char *statestr[] = {
	"Closed",
	"Dialing",
	"Listen",
	"Established",
	"Teardown",
	"Finished",
};

struct Client
{
	int ref;
	int state;
	int num;
	int servernum;

	int rport, lport;
	char *rhost;
	char *lhost;

	int sendpkt;
	int sendwin;
	int recvwin;
	int recvacc;

	int eof;

	Req *wq;
	Req **ewq;

	Req *rq;
	Req **erq;

	Msg *mq;
	Msg **emq;
};

enum {
	MSG_GLOBAL_REQUEST = 80,

	MSG_CHANNEL_OPEN = 90,
	MSG_CHANNEL_OPEN_CONFIRMATION,
	MSG_CHANNEL_OPEN_FAILURE,
	MSG_CHANNEL_WINDOW_ADJUST,
	MSG_CHANNEL_DATA,
	MSG_CHANNEL_EXTENDED_DATA,
	MSG_CHANNEL_EOF,
	MSG_CHANNEL_CLOSE,
	MSG_CHANNEL_REQUEST,
	MSG_CHANNEL_SUCCESS,
	MSG_CHANNEL_FAILURE,

	Overhead = 256,
	MaxPacket = (1<<15)-256,	/* 32K is maxatomic for pipe */
	WinPackets = 8,

	SESSIONCHAN = 1<<24,
};

struct Msg
{
	Msg	*link;

	uchar	*rp;
	uchar	*wp;
	uchar	*ep;
	uchar	buf[MaxPacket + Overhead];
};

#define PUT4(p, u) (p)[0] = (u)>>24, (p)[1] = (u)>>16, (p)[2] = (u)>>8, (p)[3] = (u)
#define GET4(p)	(u32int)(p)[3] | (u32int)(p)[2]<<8 | (u32int)(p)[1]<<16 | (u32int)(p)[0]<<24

int nclient;
Client **client;
char *mtpt, *service;
int sshfd;

int
vpack(uchar *p, int n, char *fmt, va_list a)
{
	uchar *p0 = p, *e = p+n;
	u32int u;
	void *s;
	int c;

	for(;;){
		switch(c = *fmt++){
		case '\0':
			return p - p0;
		case '_':
			if(++p > e) goto err;
			break;
		case '.':
			*va_arg(a, void**) = p;
			break;
		case 'b':
			if(p >= e) goto err;
			*p++ = va_arg(a, int);
			break;
		case '[':
		case 's':
			s = va_arg(a, void*);
			u = va_arg(a, int);
			if(c == 's'){
				if(p+4 > e) goto err;
				PUT4(p, u), p += 4;
			}
			if(u > e-p) goto err;
			memmove(p, s, u);
			p += u;
			break;
		case 'u':
			u = va_arg(a, int);
			if(p+4 > e) goto err;
			PUT4(p, u), p += 4;
			break;
		}
	}
err:
	return -1;
}

int
vunpack(uchar *p, int n, char *fmt, va_list a)
{
	uchar *p0 = p, *e = p+n;
	u32int u;
	void *s;

	for(;;){
		switch(*fmt++){
		case '\0':
			return p - p0;
		case '_':
			if(++p > e) goto err;
			break;
		case '.':
			*va_arg(a, void**) = p;
			break;
		case 'b':
			if(p >= e) goto err;
			*va_arg(a, int*) = *p++;
			break;
		case 's':
			if(p+4 > e) goto err;
			u = GET4(p), p += 4;
			if(u > e-p) goto err;
			*va_arg(a, void**) = p;
			*va_arg(a, int*) = u;
			p += u;
			break;
		case '[':
			s = va_arg(a, void*);
			u = va_arg(a, int);
			if(u > e-p) goto err;
			memmove(s, p, u);
			p += u;
			break;
		case 'u':
			if(p+4 > e) goto err;
			u = GET4(p);
			*va_arg(a, int*) = u;
			p += 4;
			break;
		}
	}
err:
	return -1;
}

Msg*
allocmsg(void)
{
	Msg *m;

	m = emalloc9p(sizeof(Msg));
	m->link = nil;
	m->rp = m->wp = m->buf;
	m->ep = m->rp + sizeof(m->buf);
	return m;
}

Msg*
pack(Msg *m, char *fmt, ...)
{
	va_list a;
	int n;

	if(m == nil)
		m = allocmsg();
	va_start(a, fmt);
	n = vpack(m->wp, m->ep - m->wp, fmt, a);
	if(n < 0)
		sysfatal("pack faild");
	m->wp += n;
	va_end(a);
	return m;
}

int
unpack(Msg *m, char *fmt, ...)
{
	va_list a;
	int n;

	va_start(a, fmt);
	n = vunpack(m->rp, m->wp - m->rp, fmt, a);
	if(n > 0)
		m->rp += n;
	va_end(a);
	return n;
}

void
sendmsg(Msg *m)
{
	int n;

	if(m == nil)
		return;
	n = m->wp - m->rp;
	if(n > 0){
		if(write(sshfd, m->rp, n) != n)
			sysfatal("write to ssh failed: %r");
	}
	free(m);
}

int
newclient(void)
{
	int i;
	Client *c;

	for(i=0; i<nclient; i++)
		if(client[i]->ref==0 && client[i]->state == Closed)
			return i;

	if(nclient%16 == 0)
		client = erealloc9p(client, (nclient+16)*sizeof(client[0]));

	c = emalloc9p(sizeof(Client));
	memset(c, 0, sizeof(*c));
	c->num = nclient;
	client[nclient++] = c;
	return c->num;
}

Client*
getclient(int num)
{
	if(num < 0 || num >= nclient)
		return nil;
	return client[num];
}

Client*
acceptclient(char *lhost, int lport, char *rhost, int rport)
{
	Client *c, *nc;
	int i;

	for(i = 0; i < nclient; i++){
		c = client[i];
		if(c->state == Listen && c->lport == lport && c->wq != nil){
			nc = client[newclient()];
			nc->wq = c->wq;
			c->wq = nc->wq->aux;
			nc->wq->aux = nil;
			free(nc->lhost);
			nc->lhost = lhost;
			nc->lport = lport;
			free(nc->rhost);
			nc->rhost = rhost;
			nc->rport = rport;
			return nc;
		}
	}
	return nil;
}

void
adjustwin(Client *c, int len)
{
	c->recvacc += len;
	if(c->recvacc >= MaxPacket*WinPackets/2 || c->recvwin < MaxPacket){
		sendmsg(pack(nil, "buu", MSG_CHANNEL_WINDOW_ADJUST, c->servernum, c->recvacc));
		c->recvacc = 0;
	}
	c->recvwin += len;
}

void
senddata(Client *c, void *data, int len)
{
	sendmsg(pack(nil, "bus", MSG_CHANNEL_DATA, c->servernum, (char*)data, len));
	c->sendwin -= len;
}

void
queuerreq(Client *c, Req *r)
{
	if(c->rq==nil)
		c->erq = &c->rq;
	*c->erq = r;
	r->aux = nil;
	c->erq = (Req**)&r->aux;
}

void
queuermsg(Client *c, Msg *m)
{
	if(c->mq==nil)
		c->emq = &c->mq;
	*c->emq = m;
	m->link = nil;
	c->emq = (Msg**)&m->link;
}

void
matchrmsgs(Client *c)
{
	Req *r;
	Msg *m;
	int n, rm;

	while((r = c->rq) != nil && (m = c->mq) != nil){
		c->rq = r->aux;
		r->aux = nil;
		rm = 0;
		n = r->ifcall.count;
		if(n >= m->wp - m->rp){
			n = m->wp - m->rp;
			c->mq = m->link;
			rm = 1;
		}
		memmove(r->ofcall.data, m->rp, n);
		if(rm)
			free(m);
		else
			m->rp += n;
		r->ofcall.count = n;
		respond(r, nil);
		adjustwin(c, n);
	}

	if(c->eof){
		while((r = c->rq) != nil){
			c->rq = r->aux;
			r->aux = nil;
			r->ofcall.count = 0;
			respond(r, nil);
		}
	}
}

void
queuewreq(Client *c, Req *r)
{
	if(c->wq==nil)
		c->ewq = &c->wq;
	*c->ewq = r;
	r->aux = nil;
	c->ewq = (Req**)&r->aux;
}

void
procwreqs(Client *c)
{
	Req *r;
	int n;

	while((r = c->wq) != nil && (n = c->sendwin) > 0){
		if(n > c->sendpkt)
			n = c->sendpkt;
		if(r->ifcall.count > n){
			senddata(c, r->ifcall.data, n);
			r->ifcall.count -= n;
			memmove(r->ifcall.data, (char*)r->ifcall.data + n, r->ifcall.count);
			continue;
		}
		c->wq = (Req*)r->aux;
		r->aux = nil;
		senddata(c, r->ifcall.data, r->ifcall.count);
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
	}
}

Req*
findreq(Client *c, Req *r)
{
	Req **l;

	for(l=&c->rq; *l; l=(Req**)&(*l)->aux){
		if(*l == r){
			*l = r->aux;
			if(*l == nil)
				c->erq = l;
			return r;
		}
	}
	for(l=&c->wq; *l; l=(Req**)&(*l)->aux){
		if(*l == r){
			*l = r->aux;
			if(*l == nil)
				c->ewq = l;
			return r;
		}
	}
	return nil;
}

void
hangupclient(Client *c, char *err)
{
	Req *r;

	c->eof = 1;
	c->recvwin = 0;
	c->sendwin = 0;
	while((r = c->wq) != nil){
		c->wq = r->aux;
		r->aux = nil;
		respond(r, err);
	}
	matchrmsgs(c);
}

void
teardownclient(Client *c)
{
	c->state = Teardown;
	hangupclient(c, "i/o on hungup channel");
	sendmsg(pack(nil, "bu", MSG_CHANNEL_CLOSE, c->servernum));
}

void
closeclient(Client *c)
{
	Msg *m;

	if(--c->ref)
		return;
	switch(c->state){
	case Established:
		teardownclient(c);
		break;
	case Finished:
		c->state = Closed;
		sendmsg(pack(nil, "bu", MSG_CHANNEL_CLOSE, c->servernum));
		break;
	case Listen:
		c->state = Closed;
		sendmsg(pack(nil, "bsbsu", MSG_GLOBAL_REQUEST,
			"cancel-tcpip-forward", 20,
			0,
			c->lhost, strlen(c->lhost),
			c->lport));
		break;
	}
	while((m = c->mq) != nil){
		c->mq = m->link;
		free(m);
	}
}

void
sshreadproc(void*)
{
	Msg *m;
	int n;

	for(;;){
		m = allocmsg();
		n = read(sshfd, m->rp, m->ep - m->rp);
		if(n <= 0)
			sysfatal("eof on ssh connection");
		m->wp += n;
		sendp(sshmsgchan, m);
	}
}

typedef struct Tab Tab;
struct Tab
{
	char *name;
	ulong mode;
};

Tab tab[] =
{
	"/",		DMDIR|0555,
	"cs",		0666,
	"tcp",		DMDIR|0555,	
	"clone",	0666,
	nil,		DMDIR|0555,
	"ctl",		0666,
	"data",		0666,
	"local",	0444,
	"remote",	0444,
	"status",	0444,
	"listen",	0666,
};

static void
fillstat(Dir *d, uvlong path)
{
	Tab *t;

	memset(d, 0, sizeof(*d));
	d->uid = estrdup9p("ssh");
	d->gid = estrdup9p("ssh");
	d->qid.path = path;
	d->atime = d->mtime = time0;
	t = &tab[TYPE(path)];
	if(t->name)
		d->name = estrdup9p(t->name);
	else{
		d->name = smprint("%ud", NUM(path));
		if(d->name == nil)
			sysfatal("out of memory");
	}
	d->qid.type = t->mode>>24;
	d->mode = t->mode;
}

static void
fsattach(Req *r)
{
	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, "invalid attach specifier");
		return;
	}
	r->fid->qid.path = PATH(Qroot, 0);
	r->fid->qid.type = QTDIR;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static void
fsstat(Req *r)
{
	fillstat(&r->d, r->fid->qid.path);
	respond(r, nil);
}

static int
rootgen(int i, Dir *d, void*)
{
	i += Qroot+1;
	if(i <= Qtcp){
		fillstat(d, i);
		return 0;
	}
	return -1;
}

static int
tcpgen(int i, Dir *d, void*)
{
	i += Qtcp+1;
	if(i < Qn){
		fillstat(d, i);
		return 0;
	}
	i -= Qn;
	if(i < nclient){
		fillstat(d, PATH(Qn, i));
		return 0;
	}
	return -1;
}

static int
clientgen(int i, Dir *d, void *aux)
{
	Client *c;

	c = aux;
	i += Qn+1;
	if(i <= Qstatus){
		fillstat(d, PATH(i, c->num));
		return 0;
	}
	return -1;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	int i, n;
	char buf[32];
	ulong path;

	path = fid->qid.path;
	if(!(fid->qid.type&QTDIR))
		return "walk in non-directory";

	if(strcmp(name, "..") == 0){
		switch(TYPE(path)){
		case Qn:
			qid->path = PATH(Qtcp, NUM(path));
			qid->type = tab[Qtcp].mode>>24;
			return nil;
		case Qtcp:
			qid->path = PATH(Qroot, 0);
			qid->type = tab[Qroot].mode>>24;
			return nil;
		case Qroot:
			return nil;
		default:
			return "bug in fswalk1";
		}
	}

	i = TYPE(path)+1;
	for(; i<nelem(tab); i++){
		if(i==Qn){
			n = atoi(name);
			snprint(buf, sizeof buf, "%d", n);
			if(n < nclient && strcmp(buf, name) == 0){
				qid->path = PATH(i, n);
				qid->type = tab[i].mode>>24;
				return nil;
			}
			break;
		}
		if(strcmp(name, tab[i].name) == 0){
			qid->path = PATH(i, NUM(path));
			qid->type = tab[i].mode>>24;
			return nil;
		}
		if(tab[i].mode&DMDIR)
			break;
	}
	return "directory entry not found";
}

typedef struct Cs Cs;
struct Cs
{
	char *resp;
	int isnew;
	int fd;		/* real /net/cs file-descriptor */
};

static int
ndbfindport(char *p)
{
	char *s, *port;
	int n;
	static Ndb *db;

	if(*p == '\0')
		return -1;

	n = strtol(p, &s, 0);
	if(*s == '\0')
		return n;

	if(db == nil){
		db = ndbopen("/lib/ndb/common");
		if(db == nil)
			return -1;
	}

	port = ndbgetvalue(db, nil, "tcp", p, "port", nil);
	if(port == nil)
		return -1;
	n = atoi(port);
	free(port);

	return n;
}

static void
csread(Req *r)
{
	Cs *cs;

	cs = r->fid->aux;
	if(cs->fd >= 0){
		int n;

		if((n = read(cs->fd, r->ofcall.data, r->ifcall.count)) < 0)
			responderror(r);
		else {
			r->ofcall.count = n;
			respond(r, nil);
		}
		return;
	}
	if(cs->resp==nil){
		respond(r, "cs read without write");
		return;
	}
	if(r->ifcall.offset==0){
		if(!cs->isnew){
			r->ofcall.count = 0;
			respond(r, nil);
			return;
		}
		cs->isnew = 0;
	}
	readstr(r, cs->resp);
	respond(r, nil);
}

static void
cswrite(Req *r)
{
	int port, nf;
	char err[ERRMAX], *f[4], *s, *ns;
	Cs *cs;

	cs = r->fid->aux;

	if(r->ifcall.count > 0 && ((char*)r->ifcall.data)[0] == '!'){
		int n;

		s = smprint("%s/cs", mtpt);
		if(cs->fd >= 0)
			close(cs->fd);
		cs->fd = open(s, ORDWR);
		if(cs->fd < 0 || (n = write(cs->fd, r->ifcall.data, r->ifcall.count)) < 0)
			responderror(r);
		else {
			r->ofcall.count = n;
			respond(r, nil);
		}
		free(s);
		return;
	}

	s = emalloc9p(r->ifcall.count+1);
	memmove(s, r->ifcall.data, r->ifcall.count);
	s[r->ifcall.count] = '\0';

	nf = getfields(s, f, nelem(f), 0, "!");
	if(nf != 3){
		free(s);
		respond(r, "can't translate");
		return;
	}
	if(strcmp(f[0], "tcp") != 0 && strcmp(f[0], "net") != 0){
		free(s);
		respond(r, "unknown protocol");
		return;
	}
	port = ndbfindport(f[2]);
	if(port < 0){
		free(s);
		respond(r, "no translation found");
		return;
	}

	ns = smprint("%s/tcp/clone %s!%d", mtpt, f[1], port);
	if(ns == nil){
		free(s);
		rerrstr(err, sizeof err);
		respond(r, err);
		return;
	}
	free(s);
	free(cs->resp);
	cs->resp = ns;
	cs->isnew = 1;
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

static void
ctlread(Req *r, Client *c)
{
	char buf[32];

	sprint(buf, "%d", c->num);
	readstr(r, buf);
	respond(r, nil);
}

static void
ctlwrite(Req *r, Client *c)
{
	char *f[3], *s;
	int nf, port;

	s = emalloc9p(r->ifcall.count+1);
	r->ofcall.count = r->ifcall.count;
	memmove(s, r->ifcall.data, r->ifcall.count);
	s[r->ifcall.count] = '\0';

	nf = tokenize(s, f, 3);
	if(nf == 0){
		free(s);
		respond(r, nil);
		return;
	}

	if(strcmp(f[0], "hangup") == 0){
		if(c->state != Established)
			goto Badarg;
		if(nf != 1)
			goto Badarg;
		teardownclient(c);
		respond(r, nil);
	}else if(strcmp(f[0], "connect") == 0){
		if(nf != 2 || c->state != Closed)
			goto Badarg;
		if(getfields(f[1], f, nelem(f), 0, "!") != 2)
			goto Badarg;
		if((port = ndbfindport(f[1])) < 0)
			goto Badarg;
		free(c->lhost);
		c->lhost = estrdup9p("::");
		c->lport = 0;
		free(c->rhost);
		c->rhost = estrdup9p(f[0]);
		c->rport = port;
		c->recvwin = WinPackets*MaxPacket;
		c->recvacc = 0;
		c->state = Dialing;
		queuewreq(c, r);

		sendmsg(pack(nil, "bsuuususu", MSG_CHANNEL_OPEN,
			"direct-tcpip", 12,
			c->num, c->recvwin, MaxPacket,
			c->rhost, strlen(c->rhost), c->rport,
			c->lhost, strlen(c->lhost), c->lport));
	}else if(strcmp(f[0], "announce") == 0){
		if(nf != 2 || c->state != Closed)
			goto Badarg;
		if(getfields(f[1], f, nelem(f), 0, "!") != 2)
			goto Badarg;
		if((port = ndbfindport(f[1])) < 0)
			goto Badarg;
		if(strcmp(f[0], "*") == 0)
			f[0] = "";
		free(c->lhost);
		c->lhost = estrdup9p(f[0]);
		c->lport = port;
		free(c->rhost);
		c->rhost = estrdup9p("::");
		c->rport = 0;
		c->state = Listen;
		sendmsg(pack(nil, "bsbsu", MSG_GLOBAL_REQUEST,
			"tcpip-forward", 13, 0,
			c->lhost, strlen(c->lhost), c->lport));
		respond(r, nil);
	}else{
	Badarg:
		respond(r, "bad or inappropriate tcp control message");
	}
	free(s);
}

static void
dataread(Req *r, Client *c)
{
	if(c->state < Established){
		respond(r, "not connected");
		return;
	}
	queuerreq(c, r);
	matchrmsgs(c);
}

static void
datawrite(Req *r, Client *c)
{
	if(c->state != Established){
		respond(r, "not connected");
		return;
	}
	if(r->ifcall.count == 0){
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
		return;
	}
	queuewreq(c, r);
	procwreqs(c);
}

static void
localread(Req *r, Client *c)
{
	char buf[128], *s;

	s = c->lhost;
	if(s == nil)
		s = "::";
	else if(*s == 0)
		s = "*";
	snprint(buf, sizeof buf, "%s!%d\n", s, c->lport);
	readstr(r, buf);
	respond(r, nil);
}

static void
remoteread(Req *r, Client *c)
{
	char buf[128], *s;

	s = c->rhost;
	if(s == nil)
		s = "::";
	snprint(buf, sizeof buf, "%s!%d\n", s, c->rport);
	readstr(r, buf);
	respond(r, nil);
}

static void
statusread(Req *r, Client *c)
{
	char *s;

	s = statestr[c->state];
	readstr(r, s);
	respond(r, nil);
}

static void
fsread(Req *r)
{
	char e[ERRMAX];
	ulong path;

	path = r->fid->qid.path;
	switch(TYPE(path)){
	default:
		snprint(e, sizeof e, "bug in fsread path=%lux", path);
		respond(r, e);
		break;

	case Qroot:
		dirread9p(r, rootgen, nil);
		respond(r, nil);
		break;

	case Qcs:
		csread(r);
		break;

	case Qtcp:
		dirread9p(r, tcpgen, nil);
		respond(r, nil);
		break;

	case Qn:
		dirread9p(r, clientgen, client[NUM(path)]);
		respond(r, nil);
		break;

	case Qctl:
		ctlread(r, client[NUM(path)]);
		break;

	case Qdata:
		dataread(r, client[NUM(path)]);
		break;

	case Qlocal:
		localread(r, client[NUM(path)]);
		break;

	case Qremote:
		remoteread(r, client[NUM(path)]);
		break;

	case Qstatus:
		statusread(r, client[NUM(path)]);
		break;
	}
}

static void
fswrite(Req *r)
{
	ulong path;
	char e[ERRMAX];

	path = r->fid->qid.path;
	switch(TYPE(path)){
	default:
		snprint(e, sizeof e, "bug in fswrite path=%lux", path);
		respond(r, e);
		break;

	case Qcs:
		cswrite(r);
		break;

	case Qctl:
		ctlwrite(r, client[NUM(path)]);
		break;

	case Qdata:
		datawrite(r, client[NUM(path)]);
		break;
	}
}

static void
fsopen(Req *r)
{
	static int need[4] = { 4, 2, 6, 1 };
	ulong path;
	int n;
	Tab *t;
	Cs *cs;

	/*
	 * lib9p already handles the blatantly obvious.
	 * we just have to enforce the permissions we have set.
	 */
	path = r->fid->qid.path;
	t = &tab[TYPE(path)];
	n = need[r->ifcall.mode&3];
	if((n&t->mode) != n){
		respond(r, "permission denied");
		return;
	}

	switch(TYPE(path)){
	case Qcs:
		cs = emalloc9p(sizeof(Cs));
		cs->fd = -1;
		r->fid->aux = cs;
		respond(r, nil);
		break;
	case Qlisten:
		if(client[NUM(path)]->state != Listen){
			respond(r, "no address set");
			break;
		}
		queuewreq(client[NUM(path)], r);
		break;
	case Qclone:
		n = newclient();
		path = PATH(Qctl, n);
		r->fid->qid.path = path;
		r->ofcall.qid.path = path;
		if(chatty9p)
			fprint(2, "open clone => path=%lux\n", path);
		t = &tab[Qctl];
		/* fall through */
	default:
		if(t-tab >= Qn)
			client[NUM(path)]->ref++;
		respond(r, nil);
		break;
	}
}

static void
fsflush(Req *r)
{
	int i;

	for(i=0; i<nclient; i++)
		if(findreq(client[i], r->oldreq))
			respond(r->oldreq, "interrupted");
	respond(r, nil);
}

static void
handlemsg(Msg *m)
{
	int chan, win, pkt, lport, rport, n, ln, rn;
	char *s, *lhost, *rhost;
	Client *c;

	switch(m->rp[0]){
	case MSG_CHANNEL_WINDOW_ADJUST:
		if(unpack(m, "_uu", &chan, &n) < 0)
			break;
		c = getclient(chan);
		if(c != nil && c->state == Established){
			c->sendwin += n;
			procwreqs(c);
		}
		break;
	case MSG_CHANNEL_DATA:
		if(unpack(m, "_us", &chan, &s, &n) < 0)
			break;
		c = getclient(chan);
		if(c != nil && c->state == Established){
			if(c->recvwin <= 0)
				break;
			c->recvwin -= n;
			m->rp = (uchar*)s;
			queuermsg(c, m);
			matchrmsgs(c);
			return;
		}
		break;
	case MSG_CHANNEL_EOF:
		if(unpack(m, "_u", &chan) < 0)
			break;
		c = getclient(chan);
		if(c != nil && c->state == Established){
			c->eof = 1;
			c->recvwin = 0;
			matchrmsgs(c);
		}
		break;
	case MSG_CHANNEL_CLOSE:
		if(unpack(m, "_u", &chan) < 0)
			break;
		c = getclient(chan);
		if(c == nil)
			break;
		switch(c->state){
		case Established:
			c->state = Finished;
			hangupclient(c, "connection closed");
			break;
		case Teardown:
			c->state = Closed;
			break;
		}
		break;
	case MSG_CHANNEL_OPEN_CONFIRMATION:
		if(unpack(m, "_uuuu", &chan, &n, &win, &pkt) < 0)
			break;
		if(chan == SESSIONCHAN){
			sessionopen++;
			break;
		}
		c = getclient(chan);
		if(c == nil || c->state != Dialing)
			break;
		if(pkt <= 0 || pkt > MaxPacket)
			pkt = MaxPacket;
		c->eof = 0;
		c->sendpkt = pkt;
		c->sendwin = win;
		c->servernum = n;
		if(c->wq == nil){
			teardownclient(c);
			break;
		}
		respond(c->wq, nil);
		c->wq = nil;
		c->state = Established;
		break;
	case MSG_CHANNEL_OPEN_FAILURE:
		if(unpack(m, "_u____s", &chan, &s, &n) < 0)
			break;
		s = smprint("%.*s", utfnlen(s, n), s);
		if(chan == SESSIONCHAN){
			sysfatal("ssh failed: %s", s);
			break;
		}
		c = getclient(chan);
		if(c != nil && c->state == Dialing){
			c->state = Closed;
			hangupclient(c, s);
		}
		free(s);
		break;
	case MSG_CHANNEL_OPEN:
		if(unpack(m, "_suuususu", &s, &n, &chan,
			&win, &pkt,
			&lhost, &ln, &lport,
			&rhost, &rn, &rport) < 0)
			break;
		if(n != 15 || strncmp(s, "forwarded-tcpip", 15) != 0){
			n = 3, s = "unknown open type";
		Reject:
			sendmsg(pack(nil, "buus", MSG_CHANNEL_OPEN_FAILURE,
				chan, n, s, strlen(s)));
			break;
		}
		lhost = smprint("%.*s", utfnlen(lhost, ln), lhost);
		rhost = smprint("%.*s", utfnlen(rhost, rn), rhost);
		c = acceptclient(lhost, lport, rhost, rport);
		if(c == nil){
			free(lhost);
			free(rhost);
			n = 2, s = "connection refused";
			goto Reject;
		}
		c->servernum = chan;
		c->recvwin = WinPackets*MaxPacket;
		c->recvacc = 0;
		c->eof = 0;
		c->sendpkt = pkt;
		c->sendwin = win;
		c->state = Established;

		sendmsg(pack(nil, "buuuu", MSG_CHANNEL_OPEN_CONFIRMATION,
			c->servernum, c->num, c->recvwin, MaxPacket));

		c->ref++;
		c->wq->fid->qid.path = PATH(Qctl, c->num);
		c->wq->ofcall.qid.path = c->wq->fid->qid.path;
		respond(c->wq, nil);
		c->wq = nil;
		break;
	}
	free(m);
}

void
fsnetproc(void*)
{
	ulong path;
	Alt a[4];
	Cs *cs;
	Fid *fid;
	Req *r;
	Msg *m;

	threadsetname("fsthread");

	a[0].op = CHANRCV;
	a[0].c = fsclunkchan;
	a[0].v = &fid;
	a[1].op = CHANRCV;
	a[1].c = fsreqchan;
	a[1].v = &r;
	a[2].op = CHANRCV;
	a[2].c = sshmsgchan;
	a[2].v = &m;
	a[3].op = CHANEND;

	for(;;){
		switch(alt(a)){
		case 0:
			path = fid->qid.path;
			switch(TYPE(path)){
			case Qcs:
				cs = fid->aux;
				if(cs){
					if(cs->fd >= 0)
						close(cs->fd);
					free(cs->resp);
					free(cs);
				}
				break;
			}
			if(fid->omode != -1 && TYPE(path) >= Qn)
				closeclient(client[NUM(path)]);
			sendp(fsclunkwaitchan, nil);
			break;
		case 1:
			switch(r->ifcall.type){
			case Tattach:
				fsattach(r);
				break;
			case Topen:
				fsopen(r);
				break;
			case Tread:
				fsread(r);
				break;
			case Twrite:
				fswrite(r);
				break;
			case Tstat:
				fsstat(r);
				break;
			case Tflush:
				fsflush(r);
				break;
			default:
				respond(r, "bug in fsthread");
				break;
			}
			sendp(fsreqwaitchan, 0);
			break;
		case 2:
			handlemsg(m);
			break;
		}
	}
}

static void
fssend(Req *r)
{
	sendp(fsreqchan, r);
	recvp(fsreqwaitchan);	/* avoids need to deal with spurious flushes */
}

static void
fsdestroyfid(Fid *fid)
{
	sendp(fsclunkchan, fid);
	recvp(fsclunkwaitchan);
}

static void
startup(Srv*)
{
	proccreate(fsnetproc, nil, 8*1024);
}

void
takedown(Srv*)
{
	threadexitsall("done");
}

Srv fs = 
{
.attach=	fssend,
.destroyfid=	fsdestroyfid,
.walk1=		fswalk1,
.open=		fssend,
.read=		fssend,
.write=		fssend,
.stat=		fssend,
.flush=		fssend,
.start=		startup,
.end=		takedown,
};

int pfd[2];
int sshargc;
char **sshargv;

void
startssh(void *)
{
	char *f;

	close(pfd[0]);
	dup(pfd[1], 0);
	dup(pfd[1], 1);
	close(pfd[1]);
	if(strncmp(sshargv[0], "./", 2) != 0)
		f = smprint("/bin/%s", sshargv[0]);
	else
		f = sshargv[0];
	procexec(nil, f, sshargv);
	sysfatal("exec: %r");
}

void
ssh(int argc, char *argv[])
{
	Alt a[4];
	Waitmsg *w;
	Msg *m;

	sshargc = argc + 2;
	sshargv = emalloc9p(sizeof(char *) * (sshargc + 1));
	sshargv[0] = "ssh";
	sshargv[1] = "-X";
	memcpy(sshargv + 2, argv, argc * sizeof(char *));

	if(pipe(pfd) < 0)
		sysfatal("pipe: %r");
	sshfd = pfd[0];
	procrfork(startssh, nil, 8*1024, RFFDG|RFNOTEG|RFNAMEG);
	close(pfd[1]);

	procrfork(sshreadproc, nil, 8*1024, RFFDG|RFNOTEG|RFNAMEG);

	sendmsg(pack(nil, "bsuuu", MSG_CHANNEL_OPEN,
		"session", 7,
		SESSIONCHAN,
		MaxPacket,
		MaxPacket));

	a[0].op = CHANRCV;
	a[0].c = threadwaitchan();
	a[0].v = &w;
	a[1].op = CHANRCV;
	a[1].c = sshmsgchan;
	a[1].v = &m;
	a[2].op = CHANEND;

	while(!sessionopen){
		switch(alt(a)){
		case 0:
			sysfatal("ssh failed: %s", w->msg);
		case 1:
			handlemsg(m);
		}
	}
}

void
usage(void)
{
	fprint(2, "usage: sshnet [-m mtpt] [-s service] [-- ssh-options] [user@]host\n");
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	fmtinstall('H', encodefmt);

	mtpt = "/net";
	service = nil;
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 's':
		service = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc == 0)
		usage();

	time0 = time(0);
	sshmsgchan = chancreate(sizeof(Msg*), 16);
	fsreqchan = chancreate(sizeof(Req*), 0);
	fsreqwaitchan = chancreate(sizeof(void*), 0);
	fsclunkchan = chancreate(sizeof(Fid*), 0);
	fsclunkwaitchan = chancreate(sizeof(void*), 0);

	ssh(argc, argv);

	threadpostmountsrv(&fs, service, mtpt, MBEFORE);

	threadexits(nil);
}
