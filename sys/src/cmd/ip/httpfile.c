/* contributed by 20h@r-36.net, September 2005 */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>

enum
{
	Blocksize = 64*1024,
	Stacksize = 8192,
};

char *user, *url, *file;
char webconn[64];
int webctlfd = -1;

vlong size;
int usetls;
int debug;
int ncache;
int mcache;

void
usage(void)
{
	fprint(2, "usage: httpfile [-Dd] [-c count] [-f file] [-m mtpt] [-s srvname] url\n");
	exits("usage");
}

enum
{
	Qroot,
	Qfile,
};

#define PATH(type, n)		((type)|((n)<<8))
#define TYPE(path)			((int)(path) & 0xFF)
#define NUM(path)			((uint)(path)>>8)

Channel *reqchan;
Channel *httpchan;
Channel *finishchan;
ulong time0;

typedef struct Block Block;
struct Block
{
	uchar *p;
	vlong off;
	vlong len;
	Block *link;
	long lastuse;
	Req *rq;
	Req **erq;
};

typedef struct Blocklist Blocklist;
struct Blocklist
{
	Block *first;
	Block **end;
};

Blocklist cache;
Blocklist inprogress;

void
queuereq(Block *b, Req *r)
{
	if(b->rq==nil)
		b->erq = &b->rq;
	*b->erq = r;
	r->aux = nil;
	b->erq = (Req**)&r->aux;
}

void
addblock(Blocklist *l, Block *b)
{
	if(debug)
		print("adding: %p %lld\n", b, b->off);

	if(l->first == nil)
		l->end = &l->first;
	b->lastuse = time(0);
	b->link = nil;
	*l->end = b;
	l->end = &b->link;
}

void
delreq(Block *b, Req *r)
{
	Req **l;

	for(l = &b->rq; *l; l = (Req**)&(*l)->aux){
		if(*l == r){
			*l = r->aux;
			if(*l == nil)
				b->erq = l;
			r->aux = nil;
			respond(r, "interrupted");
			return;
		}
	}
}

void
evictblock(Blocklist *cache)
{
	Block **l, **oldest, *b;

	oldest = nil;
	for(l=&cache->first; (b=*l) != nil; l=&b->link){
		if(b->rq != nil)	/* dont touch block when still requests queued */
			continue;
		if(oldest == nil || (*oldest)->lastuse > b->lastuse)
			oldest = l;
	}

	if(oldest == nil || *oldest == nil || (*oldest)->rq != nil)
		return;

	b = *oldest;
	if((*oldest = b->link) == nil)
		cache->end = oldest;

	free(b->p);
	free(b);
	ncache--;
}

Block *
findblock(Blocklist *s, vlong off)
{
	Block *b;

	for(b = s->first; b != nil; b = b->link){
		if(off >= b->off && off < b->off + b->len){
			if(debug)
				print("found: %lld -> %lld\n", off, b->off);
			b->lastuse = time(0);
			return b;
		}
	}
	return nil;
}

void
readfrom(Req *r, Block *b)
{
	int d, n;

	n = r->ifcall.count;
	d = r->ifcall.offset - b->off;
	if(d + n > b->len)
		n = b->len - d;
	if(debug)
		print("Reading from: %p %d %d\n", b->p, d, n);
	memmove(r->ofcall.data, b->p + d, n);
	r->ofcall.count = n;

	respond(r, nil);
}

void
hangupclient(Srv*)
{
	if(debug)
		print("Hangup.\n");

	threadexitsall("done");
}

static int
readfile(int fd, char *buf, int nbuf)
{
	int r, n;

	for(n = 0; n < nbuf; n += r)
		if((r = read(fd, buf + n, nbuf - n)) <= 0)
			break;
	return n;
}

static int
readstring(int fd, char *buf, int nbuf)
{
	int n;

	if((n = readfile(fd, buf, nbuf-1)) < 0){
		buf[0] = '\0';
		return -1;
	}
	if(n > 0 && buf[n-1] == '\n')
		n--;
	buf[n] = '\0';
	return n;
}

uchar*
getrange(Block *b)
{
	char buf[128];
	int fd, cfd;
	uchar *data;

	if(debug)
		print("getrange: %lld %lld\n", b->off, b->len);

	if(fprint(webctlfd, "url %s\n", url) < 0)
		return nil;
	if(fprint(webctlfd, "request GET\n") < 0)
		return nil;
	if(fprint(webctlfd, "headers Range: bytes=%lld-%lld\n", b->off, b->off+b->len-1) < 0)
		return nil;

	/* start the request */
	snprint(buf, sizeof(buf), "%s/body", webconn);
	if((fd = open(buf, OREAD)) < 0)
		return nil;

	/* verify content-range response header */ 
	snprint(buf, sizeof(buf), "%s/contentrange", webconn);
	if((cfd = open(buf, OREAD)) < 0){
		close(fd);
		return nil;
	}

	werrstr("bad contentrange header");
	if(readstring(cfd, buf, sizeof(buf)) <= 0){
Badrange:
		close(cfd);
		close(fd);
		return nil;
	}
	if(cistrncmp(buf, "bytes ", 6) != 0)
		goto Badrange;
	if(strtoll(buf + 6, nil, 10) != b->off)
		goto Badrange;
	close(cfd);

	/* read body data */
	data = emalloc9p(b->len);
	werrstr("body data truncated");
	if(readfile(fd, (char*)data, b->len) != b->len){
		close(fd);
		free(data);
		return nil;
	}
	close(fd);
	b->p = data;
	return data;
}

void
httpfilereadproc(void*)
{
	Block *b;

	threadsetname("httpfilereadproc %s", url);

	for(;;){
		b = recvp(httpchan);
		if(b == nil)
			continue;
		if(getrange(b) == nil)
			sysfatal("getrange: %r");
		sendp(finishchan, b);
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
	nil,		0444,
};

static void
fillstat(Dir *d, uvlong path)
{
	Tab *t;

	memset(d, 0, sizeof(*d));
	d->uid = estrdup9p(user);
	d->gid = estrdup9p(user);
	d->qid.path = path;
	d->atime = d->mtime = time0;
	t = &tab[TYPE(path)];
	d->name = estrdup9p(t->name);
	d->length = size;
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
	i += Qroot + 1;
	if(i <= Qfile){
		fillstat(d, i);
		return 0;
	}
	return -1;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	int i;
	ulong path;

	path = fid->qid.path;
	if(!(fid->qid.type & QTDIR))
		return "walk in non-directory";

	if(strcmp(name, "..") == 0){
		switch(TYPE(path)){
		case Qroot:
			return nil;
		default:
			return "bug in fswalk1";
		}
	}

	i = TYPE(path) + 1;
	while(i < nelem(tab)){
		if(strcmp(name, tab[i].name) == 0){
			qid->path = PATH(i, NUM(path));
			qid->type = tab[i].mode>>24;
			return nil;
		}
		if(tab[i].mode & DMDIR)
			break;
		i++;
	}
	return "directory entry not found";
}

vlong
getfilesize(void)
{
	char buf[128];
	int fd, cfd;

	if(fprint(webctlfd, "url %s\n", url) < 0)
		return -1;
	if(fprint(webctlfd, "request HEAD\n") < 0)
		return -1;
	snprint(buf, sizeof(buf), "%s/body", webconn);
	if((fd = open(buf, OREAD)) < 0)
		return -1;
	snprint(buf, sizeof(buf), "%s/contentlength", webconn);
	cfd = open(buf, OREAD);
	close(fd);
	if(cfd < 0)
		return -1;
	if(readstring(cfd, buf, sizeof(buf)) <= 0){
		close(cfd);
		return -1;
	}
	close(cfd);
	return strtoll(buf, nil, 10);
}

void
fileread(Req *r)
{
	Block *b;

	if(r->ifcall.offset >= size){
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}

	if((b = findblock(&cache, r->ifcall.offset)) != nil){
		readfrom(r, b);
		return;
	}
	if((b = findblock(&inprogress, r->ifcall.offset)) == nil){
		b = emalloc9p(sizeof(Block));
		b->off = r->ifcall.offset - (r->ifcall.offset % Blocksize);
		b->len = Blocksize;
		if(b->off + b->len > size)
			b->len = size - b->off;
		addblock(&inprogress, b);
		if(inprogress.first == b)
			sendp(httpchan, b);
	}
	queuereq(b, r);
}

static void
fsopen(Req *r)
{
	if(r->ifcall.mode != OREAD){
		respond(r, "permission denied");
		return;
	}
	respond(r, nil);
}

void
finishthread(void*)
{
	Block *b;
	Req *r;

	threadsetname("finishthread");

	for(;;){
		b = recvp(finishchan);
		assert(b == inprogress.first);
		inprogress.first = b->link;
		if(++ncache >= mcache)
			evictblock(&cache);
		addblock(&cache, b);
		while((r = b->rq) != nil){
			b->rq = r->aux;
			r->aux = nil;
			readfrom(r, b);
		}
		if(inprogress.first != nil)
			sendp(httpchan, inprogress.first);
	}
}

void
fsnetproc(void*)
{
	Req *r, *o;
	Block *b;

	threadcreate(finishthread, nil, 8192);

	threadsetname("fsnetproc");

	for(;;){
		r = recvp(reqchan);
		switch(r->ifcall.type){
		case Tflush:
			o = r->oldreq;
			if(o->ifcall.type == Tread){
				b = findblock(&inprogress, o->ifcall.offset);
				if(b != nil)
					delreq(b, o);
			}
			respond(r, nil);
			break;
		case Tread:
			fileread(r);
			break;
		default:
			respond(r, "bug in fsthread");
			break;
		}
	}
}

static void
fsflush(Req *r)
{
	sendp(reqchan, r);
}

static void
fsread(Req *r)
{
	char e[ERRMAX];
	ulong path;

	path = r->fid->qid.path;
	switch(TYPE(path)){
	case Qroot:
		dirread9p(r, rootgen, nil);
		respond(r, nil);
		break;
	case Qfile:
		sendp(reqchan, r);
		break;
	default:
		snprint(e, sizeof(e), "bug in fsread path=%lux", path);
		respond(r, e);
		break;
	}
}

Srv fs = 
{
.attach=		fsattach,
.walk1=		fswalk1,
.open=		fsopen,
.read=		fsread,
.stat=		fsstat,
.flush=		fsflush,
.end=		hangupclient,
};

void
threadmain(int argc, char **argv)
{
	char *mtpt, *srvname, *p;

	mtpt = nil;
	srvname = nil;
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'd':
		debug++;
		break;
	case 's':
		srvname = EARGF(usage());
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 'c':
		mcache = atoi(EARGF(usage()));
		break;
	case 'f':
		file = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	if(srvname == nil && mtpt == nil)
		mtpt = ".";

	if(argc < 1)
		usage();
	if(mcache <= 0)
		mcache = 32;

	time0 = time(0);
	url = estrdup9p(argv[0]);
	if(file == nil){
		file = strrchr(url, '/');
		if(file == nil || file[1] == '\0')
			file = "index";
		else
			file++;
	}

	snprint(webconn, sizeof(webconn), "/mnt/web/clone");
	if((webctlfd = open(webconn, ORDWR)) < 0)
		sysfatal("open: %r");
	p = strrchr(webconn, '/')+1;
	if(readstring(webctlfd, p, webconn+sizeof(webconn)-p) <= 0)
		sysfatal("read: %r");

	tab[Qfile].name = file;
	user = getuser();
	size = getfilesize();
	if(size < 0)
		sysfatal("getfilesize: %r");

	reqchan = chancreate(sizeof(Req*), 0);
	httpchan = chancreate(sizeof(Block*), 0);
	finishchan = chancreate(sizeof(Block*), 0);

	procrfork(fsnetproc, nil, Stacksize, RFNAMEG|RFNOTEG);
	procrfork(httpfilereadproc, nil, Stacksize, RFNAMEG|RFNOTEG);

	threadpostmountsrv(&fs, srvname, mtpt, MBEFORE);
	threadexits(0);
}
