#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <auth.h>
#include <fcall.h>
#include <9p.h>

#include "usb.h"

enum
{
	Qroot,
	Qstore,
	Qobj,
	Qthumb,
};

enum {
	/* flags */
	DataSend			=	0x00010000,
	DataRecv			=	0x00020000,
	OutParam			=	0x00040000,

	/* rpc codes */
	OpenSession		=	0x1002,
	CloseSession		=	0x1003,
	GetStorageIds		=	0x1004,
	GetStorageInfo		=	0x1005,
	GetObjectHandles	=	0x1007,
	GetObjectInfo		=	0x1008,
	GetObject			=	0x1009,
	GetThumb		=	0x100A,
	DeleteObject		=	0x100B,
};

typedef struct Ptprpc Ptprpc;
struct Ptprpc
{
	uchar	length[4];
	uchar	type[2];
	uchar	code[2];
	uchar	transid[4];
	union {
		uchar	p[5][4];
		uchar	d[500];
	};
};

typedef struct Node Node;
struct Node
{
	Dir		d;

	Node	*parent;
	Node	*next;
	Node	*child;

	int		store;
	int		handle;
	int		format;

	void		*data;
	int		ndata;
};

int debug;

enum {
	In,
	Out,
};
static Dev *usbep[2];

static ulong time0;
static int maxpacket = 64;
static int sessionId = 0;
static int transId = 0;

static Node **nodes;
static int nnodes;

static char *uname;

#define PATH(type, n)		((uvlong)(type)|((uvlong)(n)<<4))
#define TYPE(path)			((int)((path)&0xF))
#define NUM(path)			((int)((path)>>4))

static void
hexdump(char *prefix, uchar *p, int n)
{
	char *s;
	int i;
	int m;

	m = 12;
	s = emalloc9p(1+((n+1)*((m*6)+7)));
	s[0] = '\0';
	for(i=0; i<n; i++){
		int printable;
		char x[8];
		if((i % m)==0){
			sprint(x, "\n%.4x: ", i);
			strcat(s, x);
		}
		printable = (p[i] >= 32 && p[i]<=127);
		sprint(x, "%.2x %c  ", (int)p[i],  printable ? p[i] : '.');
		strcat(s, x);
	}
	fprint(2, "%20-s: %6d bytes %s\n", prefix, n, s);
	free(s);
}

static int
isinterrupt(void)
{
	char err[ERRMAX];
	rerrstr(err, sizeof(err));
	return !!(strstr(err, "interrupted") || strstr(err, "request timed out"));
}

static int
usbread(Dev *ep, void *data, int len)
{
	int n, try;

	n = 0;
	for(try = 0; try < 4; try++){
		n = read(ep->dfd, data, len);
		if(n == 0)
			continue;
		if(n >= 0 || !isinterrupt())
			break;
	}
	return n;
}

static int
usbwrite(Dev *ep, void *data, int len)
{
	int n, try;

	n = 0;
	for(try = 0; try < 4; try++){
		n = write(ep->dfd, data, len);
		if(n == 0)
			continue;
		if(n >= 0 || !isinterrupt())
			break;
	}
	return n;
}

static char *
ptperrstr(int code)
{
	static char *a[] = {
		"undefined",
		nil ,
		"general error" ,
		"session not open" ,
		 "invalid transaction id" ,
		 "operation not supported" ,
		 "parameter not supported" ,
		 "incomplete transfer" ,
		 "invalid storage id" ,
		 "invalid object handle" ,
		 "device prop not supported" ,
		 "invalid object format code" ,
		 "storage full" ,
		 "object write protected" ,
		 "store read only" ,
		 "access denied" ,
		 "no thumbnail present" ,
		 "self test failed" ,
		 "partial deletion" ,
		 "store not available" ,
		 "specification by format unsupported" ,
		 "no valid object info" ,
		 "invalid code format" ,
		 "unknown vendor code",
		"capture already terminated",
		"device busy",
		"invalid parent object",
		"invalid device prop format",
		"invalid device prop value",
		"invalid parameter",
		"session already opend",
		"transaction canceld",
		"specification of destination unsupported"
	};

	code -= 0x2000;
	if(code < 0)
		return nil;
	if(code >= nelem(a))
		return "invalid error number";
	return a[code];
}

static int
ptpcheckerr(Ptprpc *rpc, int type, int transid, int length)
{
	char *s;

	if(length < 4+2+2+4){
		werrstr("short response: %d < %d", length, 4+2+2+4);
		return -1;
	}
	if(GET4(rpc->length) < length){
		werrstr("unexpected response length 0x%x < 0x%x", GET4(rpc->length), length);
		return -1;
	}
	if(GET4(rpc->transid) != transid){
		werrstr("unexpected transaction id 0x%x != 0x%x", GET4(rpc->transid), transid);
		return -1;
	}
	if(s = ptperrstr(GET2(rpc->code))){
		werrstr("%s", s);
		return -GET2(rpc->code);
	}
	if(GET2(rpc->type) != type){
		werrstr("unexpected response type 0x%x != 0x%x", GET2(rpc->type), type);
		return -1;
	}
	return 0;
}

static int
ptprpc(int code, int flags, ...)
{
	Ptprpc rpc;
	va_list a;
	int np, n, t, i, l;
	uchar *b, *p, *e;

	np = flags & 0xF;
	n = 4+2+2+4+4*np;
	t = transId++;

	PUT4(rpc.length, n);
	PUT2(rpc.type, 1);
	PUT2(rpc.code, code);
	PUT4(rpc.transid, t);

	va_start(a, flags);
	for(i=0; i<np; i++){
		int x = va_arg(a, int);
		PUT4(rpc.p[i], x);
	}
	if(debug)
		hexdump("req>", (uchar*)&rpc, n);
	if(usbwrite(usbep[Out], &rpc, n) < 0)
		return -1;

	if(flags & DataSend){
		void *sdata;
		int sdatalen;

		sdata = va_arg(a, void*);
		sdatalen = va_arg(a, int);

		b = (uchar*)sdata;
		p = b;
		e = b + sdatalen;

		l = 4+2+2+4+sdatalen;
		PUT4(rpc.length, l);
		PUT2(rpc.type, 2);

		if((n = sdatalen) > sizeof(rpc.d))
			n = sizeof(rpc.d);
		memmove(rpc.d, p, n);
		p += n;
		n += (4+2+2+4);

		if(debug)
			hexdump("data>", (uchar*)&rpc, n);
		if(usbwrite(usbep[Out], &rpc, n) < 0)
			return -1;
		while(p < e){	
			if((n = usbwrite(usbep[Out], p, e-p)) < 0)
				break;
			p += n;
		}
	}

	if(flags & DataRecv){
		void **prdata;
		int *prdatalen;

		prdata = va_arg(a, void**);
		prdatalen = va_arg(a, int*);

		*prdata = nil;
		*prdatalen = 0;

		if((n = usbread(usbep[In], &rpc, sizeof(rpc))) < 0)
			return -1;
		
		if(debug)
			hexdump("data<", (uchar*)&rpc, n);
		if(ptpcheckerr(&rpc, 2, t, n))
			goto Err;

		l = GET4(rpc.length);
		if((l < 4+2+2+4) || (l < n)){
			werrstr("invalid recvdata length");
			return -1;
		}

		l -= (4+2+2+4);
		n -= (4+2+2+4);

		b = emalloc9p(l);
		p = b;
		e = b+l;
		memmove(p, rpc.d, n);
		p += n;

		while(p < e){
			if((n = usbread(usbep[In], p, e-p)) < 0){
				free(b);
				return -1;
			}
			p += n;
		}
		*prdata = b;
		*prdatalen =  e-b;
	}

	if((n = usbread(usbep[In], &rpc, sizeof(rpc))) < 0)
		return -1;
	if(debug)
		hexdump("resp<", (uchar*)&rpc, n);

Err:
	if(ptpcheckerr(&rpc, 3, t, n) < 0){
		werrstr("ptp %x: %r", code);
		return -1;
	}

	if(flags & OutParam){
		int *pp;

		for(i=0; i<nelem(rpc.p); i++){
			if((pp = va_arg(a, int*)) == nil)
				break;
			*pp = GET4(rpc.p[i]);
		}
	}
	va_end(a);
	return 0;
}

static int*
ptparray4(uchar *d, uchar *e)
{
	int *a, i, n;

	if(d + 4 > e)
		return nil;
	n = GET4(d);
	d += 4;
	if(d + n*4 > e)
		return nil;
	a = emalloc9p((1+n) * sizeof(int));
	a[0] = n;
	for(i=0; i<n; i++){
		a[i+1] = GET4(d);
		d += 4;
	}
	return a;
}

static char*
ptpstring2(uchar *d, uchar *e)
{
	int n, i;
	char *s, *p;

	if(d+1 > e)
		return nil;
	n = *d;
	d++;
	if(d + n*2 > e)
		return nil;
	p = s = emalloc9p((n+1)*UTFmax);
	for(i=0; i<n; i++){
		Rune r;

		r = GET2(d);
		d += 2;
		if(r == 0)
			break;
		p += runetochar(p, &r);
	}
	*p = 0;
	return s;
}

static void
cleardir(Dir *d)
{
	free(d->name);
	free(d->uid);
	free(d->gid);
	free(d->muid);
	memset(d, 0, sizeof(*d));
}

static void
copydir(Dir *d, Dir *s)
{
	memmove(d, s, sizeof(*d));
	if(d->name)
		d->name = estrdup9p(d->name);
	if(d->uid)
		d->uid = estrdup9p(d->uid);
	if(d->gid)
		d->gid = estrdup9p(d->gid);
	if(d->muid)
		d->muid = estrdup9p(d->muid);
}

static Node*
getnode(uvlong path)
{
	int i, j;
	Node *x;
	uchar *p;
	int np;
	char *s;

	j = -1;
	for(i=0; i<nnodes; i++){
		if((x = nodes[i]) == nil){
			j = i;
			continue;
		}
		if(x->d.qid.path == path)
			return x;
	}

	x = emalloc9p(sizeof(*x));

	memset(x, 0, sizeof(*x));

	x->d.qid.path = path;
	x->d.uid = estrdup9p(uname);
	x->d.gid = estrdup9p(uname);
	x->d.atime = x->d.mtime = time0;

	switch(TYPE(path)){
	case Qroot:
		x->d.qid.type = QTDIR;
		x->d.mode = DMDIR|0555;
		x->d.name = estrdup9p("/");
		break;

	case Qstore:
		x->store = NUM(path);
		x->handle = 0xffffffff;
		x->d.qid.type = QTDIR;
		x->d.mode = DMDIR|0555;
		x->d.name = emalloc9p(10);
		sprint(x->d.name, "%x", x->store);
		break;

	case Qobj:
	case Qthumb:
		x->handle = NUM(path);
		if(ptprpc(GetObjectInfo, 1|DataRecv, x->handle, &p, &np) < 0)
			goto err;
		if(debug)
			hexdump("objectinfo", p, np);
		if(np < 52){
			werrstr("bad objectinfo");
			goto err;
		}
		if((x->d.name = ptpstring2(p+52, p+np)) == nil){
			werrstr("bad objectinfo");
			goto err;
		}
		x->store = GET4(p);
		x->format = GET2(p+4);
		if(x->format == 0x3001 && GET2(p+42) == 1){
			x->d.qid.type = QTDIR;
			x->d.mode = DMDIR|0555;
		} else {
			x->d.mode = 0444;
			if(TYPE(path) == Qthumb){
				char *t;

				t = emalloc9p(8 + strlen(x->d.name));
				sprint(t, "thumb_%s", x->d.name);
				free(x->d.name);
				x->d.name = t;

				x->d.length = GET4(p+14);
			} else {
				x->d.length = GET4(p+8);
			}
		}
		if(s = ptpstring2(p+(53+p[52]*2), p+np)){
			if(strlen(s) >= 15){
				Tm t;

				// 0123 45 67 8 9A BC DF
				// 2008 12 26 T 00 21 18
				memset(&t, 0, sizeof(t));

				s[0x10] = 0;
				t.sec = atoi(s+0xD);
				s[0xD] = 0;
				t.min = atoi(s+0xB);
				s[0xB] = 0;
				t.hour = atoi(s+0x9);
				s[0x8] = 0;
				t.mday = atoi(s+0x6);
				s[0x6] = 0;
				t.mon = atoi(s+0x4) - 1;
				s[0x4] = 0;
				t.year = atoi(s) - 1900;

				x->d.atime = x->d.mtime = tm2sec(&t);
			}
			free(s);
		}
		free(p);
		break;
	}

	if(j < 0){
		if(nnodes % 64 == 0)
			nodes = erealloc9p(nodes, sizeof(nodes[0]) * (nnodes + 64));
		j = nnodes++;
	}
	return nodes[j] = x;

err:
	cleardir(&x->d);
	free(x);
	return nil;
}

static void
freenode(Node *nod)
{
	int i;

	/* remove the node from the tree */
	for(i=0; i<nnodes; i++){
		if(nod == nodes[i]){
			nodes[i] = nil;
			break;
		}
	}
	cleardir(&nod->d);
	free(nod->data);
	free(nod);
}

static int
readchilds(Node *nod)
{
	int i;
	int *a;
	uchar *p;
	int np;
	Node *x, **xx;

	xx = &nod->child;
	switch(TYPE(nod->d.qid.path)){
	case Qroot:
		if(ptprpc(GetStorageIds, 0|DataRecv, &p, &np) < 0)
			return -1;
		a = ptparray4(p, p+np);
		free(p);
		for(i=0; a && i<a[0]; i++){
			if(x = getnode(PATH(Qstore, a[i+1]))){
				x->parent = nod;
				*xx = x;
				xx = &x->next;
			}
		}		
		free(a);
		break;

	case Qstore:
	case Qobj:
		if(ptprpc(GetObjectHandles, 3|DataRecv, nod->store, 0, nod->handle, &p, &np) < 0)
			return -1;
		a = ptparray4(p, p+np);
		free(p);
		for(i=0; a && i<a[0]; i++){
			if(x = getnode(PATH(Qobj, a[i+1]))){
				x->parent = nod;
				*xx = x;
				xx = &x->next;

				/* skip thumb when not image format */
				if((x->format & 0xFF00) != 0x3800)
					continue;
			}
			if(x = getnode(PATH(Qthumb, a[i+1]))){
				x->parent = nod;
				*xx = x;
				xx = &x->next;
			}
		}
		free(a);
		break;
	}
	*xx = nil;

	return 0;
}

static void
fsattach(Req *r)
{
	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, "invalid attach specifier");
		return;
	}
	if(uname == nil)
		uname = estrdup9p(r->ifcall.uname);
	r->fid->qid.path = PATH(Qroot, 0);
	r->fid->qid.type = QTDIR;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static void
fsstat(Req *r)
{
	Node *nod;

	if((nod = getnode(r->fid->qid.path)) == nil){
		responderror(r);
		return;
	}
	copydir(&r->d, &nod->d);
	respond(r, nil);
}

static int
nodegen(int i, Dir *d, void *aux)
{
	Node *nod = aux;

	for(nod=nod->child; nod && i; nod=nod->next, i--)
		;
	if(i==0 && nod){
		copydir(d, &nod->d);
		return 0;
	}
	return -1;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	Node *nod;
	uvlong path;
	static char buf[ERRMAX];

	path = fid->qid.path;
	if(!(fid->qid.type&QTDIR))
		return "walk in non-directory";

	if((nod = getnode(path)) == nil)
		goto err;

	if(strcmp(name, "..") == 0){
		if(nod = nod->parent){
			*qid = nod->d.qid;
			fid->qid = *qid;
		}
		return nil;
	}

	if(readchilds(nod) < 0)
		goto err;

	for(nod=nod->child; nod; nod=nod->next){
		if(strcmp(nod->d.name, name) == 0){
			*qid = nod->d.qid;
			fid->qid = *qid;
			return nil;
		}
	}
	return "directory entry not found";

err:
	rerrstr(buf, sizeof(buf));
	return buf;
}

static void
fsread(Req *r)
{
	Node *nod;
	uvlong path;

	path = r->fid->qid.path;

	if((nod = getnode(path)) == nil)
		goto err;

	if(nod->d.qid.type & QTDIR){
		if(readchilds(nod) < 0)
			goto err;

		dirread9p(r, nodegen, nod);
		respond(r, nil);
		return;
	}

	switch(TYPE(path)){
	default:
		werrstr("bug in fsread path=%llux", path);
		break;

	case Qobj:
	case Qthumb:
		if(nod->data == nil){
			uchar *p;
			int np;

			if(TYPE(path)==Qthumb){
				if(ptprpc(GetThumb, 1|DataRecv, nod->handle, &p, &np) < 0)
					goto err;
			} else {
				if(ptprpc(GetObject, 1|DataRecv, nod->handle, &p, &np) < 0)
					goto err;
			}
			nod->data = p;
			nod->ndata = np;
		}
		readbuf(r, nod->data, nod->ndata);
		respond(r, nil);
		return;
	}

err:
	responderror(r);
}

static void
fsremove(Req *r)
{
	Node *nod;
	uvlong path;

	path = r->fid->qid.path;

	if((nod = getnode(path)) == nil)
		goto err;

	switch(TYPE(path)){
	default:
		werrstr("bug in fsremove path=%llux", path);
		break;

	case Qobj:
		if(ptprpc(DeleteObject, 2, nod->handle, 0) < 0)
			goto err;

	case Qthumb:
		freenode(nod);
		respond(r, nil);
		return;
	}

err:
	responderror(r);
}

static void
fsopen(Req *r)
{
	respond(r, nil);
}

static void
fsflush(Req *r)
{
	respond(r, nil);
}

static void
fsdestroyfid(Fid *fid)
{
	Node *nod;
	uvlong path;

	path = fid->qid.path;
	switch(TYPE(path)){
	case Qobj:
	case Qthumb:
		if(nod = getnode(path)){
			free(nod->data);
			nod->data = nil;
			nod->ndata = 0;
		}
		break;
	}
}

static void
fsend(Srv *)
{
	ptprpc(CloseSession, 0);
}

static int
findendpoints(Dev *d, int *epin, int *epout)
{
	int i;
	Ep *ep;
	Usbdev *ud;

	ud = d->usb;
	*epin = *epout = -1;
	for(i=0; i<nelem(ud->ep); i++){
		if((ep = ud->ep[i]) == nil)
			continue;
		if(ep->type != Ebulk)
			continue;
		if(ep->dir == Eboth || ep->dir == Ein)
			if(*epin == -1)
				*epin =  ep->id;
		if(ep->dir == Eboth || ep->dir == Eout)
			if(*epout == -1)
				*epout = ep->id;
		if(*epin >= 0 && *epout >= 0)
			return 0;
	}
	return -1;
}

static int
inote(void *, char *msg)
{
	if(strstr(msg, "interrupt"))
		return 1;
	return 0;
}

Srv fs = 
{
.attach=		fsattach,
.destroyfid=	fsdestroyfid,
.walk1=		fswalk1,
.open=		fsopen,
.read=		fsread,
.remove=		fsremove,
.stat=		fsstat,
.flush=		fsflush,
.end=		fsend,
};

static void
usage(void)
{
	fprint(2, "usage: %s [-dD] devid\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	int epin, epout;
	char name[64], desc[64];
	Dev *d;

	ARGBEGIN {
	case 'd':
		debug = 1;
		usbdebug++;
		break;
	case 'D':
		chatty9p++;
		break;
	default:
		usage();
	} ARGEND;

	if(argc == 0)
		usage();
	if((d = getdev(atoi(*argv))) == nil)
		sysfatal("opendev: %r");
	if(findendpoints(d, &epin, &epout)  < 0)
		sysfatal("findendpoints: %r");

	usbep[In] = openep(d, epin);
	if(epin == epout){
		incref(usbep[In]);
		usbep[Out] = usbep[In];
		opendevdata(usbep[In], ORDWR);
	} else {
		usbep[Out] = openep(d, epout);
		opendevdata(usbep[In], OREAD);
		opendevdata(usbep[Out], OWRITE);
	}
	if(usbep[In]->dfd < 0 || usbep[Out]->dfd < 0)
		sysfatal("open endpoints: %r");

	sessionId = getpid();
	if(ptprpc(OpenSession, 1, sessionId) < 0)
		return;

	atnotify(inote, 1);
	time0 = time(0);

	snprint(name, sizeof name, "sdU%d.0", d->id);
	snprint(desc, sizeof desc, "%d.ptp", d->id);
	threadpostsharesrv(&fs, nil, name, desc);

	threadexits(0);
}
