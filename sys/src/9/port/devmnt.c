#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

/*
 * References are managed as follows:
 * The channel to the server - a network connection or pipe - has one
 * reference for every Chan open on the server.  The server channel has
 * c->mux set to the Mnt used for muxing control to that server.  Mnts
 * have no reference count; they go away when c goes away.
 * Each channel derived from the mount point has mchan set to c,
 * and increfs/decrefs mchan to manage references on the server
 * connection.
 */

#define MAXRPC (IOHDRSZ+32768)	/* maximum size of any RPC */
#define MAXRPC0 (IOHDRSZ+8192)	/* maximum size of Tversion/Rversion pair */

struct Mntrpc
{
	Chan*	c;		/* Channel for whom we are working */
	Mntrpc*	list;		/* Free/pending list */
	Fcall	request;	/* Outgoing file system protocol message */
	Fcall 	reply;		/* Incoming reply */
	Mnt*	m;		/* Mount device during rpc */
	Rendez*	z;		/* Place to hang out */
	Block*	w;		/* copy of write rpc for cache */
	Block*	b;		/* reply blocks */
	Mntrpc*	flushed;	/* message this one flushes */
	char	done;		/* Rpc completed */
};

enum
{
	TAGSHIFT = 5,
	TAGMASK = (1<<TAGSHIFT)-1,
	NMASK = (64*1024)>>TAGSHIFT,
};

static struct Mntalloc
{
	Lock;
	Mnt*	list;		/* Mount devices in use */
	Mnt*	mntfree;	/* Free list */
	Mntrpc*	rpcfree;
	ulong	nrpcfree;
	ulong	nrpcused;
	ulong	id;
	u32int	tagmask[NMASK];
} mntalloc;

static Chan*	mntchan(void);
static Mnt*	mntchk(Chan*);
static void	mntdirfix(uchar*, Chan*);
static Mntrpc*	mntflushalloc(Mntrpc*);
static Mntrpc*	mntflushfree(Mnt*, Mntrpc*);
static void	mntfree(Mntrpc*);
static void	mntgate(Mnt*);
static void	mntqrm(Mnt*, Mntrpc*);
static Mntrpc*	mntralloc(Chan*);
static long	mntrdwr(int, Chan*, void*, long, vlong);
static int	mntrpcread(Mnt*, Mntrpc*);
static void	mountio(Mnt*, Mntrpc*);
static void	mountmux(Mnt*, Mntrpc*);
static void	mountrpc(Mnt*, Mntrpc*);
static int	rpcattn(void*);

#define cachedchan(c) (((c)->flag & CCACHE) != 0 && (c)->mcp != nil)

char	Esbadstat[] = "invalid directory entry received from server";
char	Enoversion[] = "version not established for mount channel";


static void
mntreset(void)
{
	mntalloc.id = 1;
	mntalloc.tagmask[0] = 1;			/* don't allow 0 as a tag */
	mntalloc.tagmask[NMASK-1] = 0x80000000;		/* don't allow NOTAG */
	fmtinstall('F', fcallfmt);
	fmtinstall('D', dirfmt);
/* We can't install %M since eipfmt does and is used in the kernel [sape] */

	cinit();
}

/*
 * Version is not multiplexed: message sent only once per connection.
 */
int
mntversion(Chan *c, char *version, int msize, int returnlen)
{
	Fcall f;
	uchar *msg;
	Mnt *m;
	char *v;
	Queue *q;
	long k, l;
	uvlong oo;
	char buf[128];

	eqlock(&c->umqlock);	/* make sure no one else does this until we've established ourselves */
	if(waserror()){
		qunlock(&c->umqlock);
		nexterror();
	}

	/* defaults */
	if(msize == 0)
		msize = MAXRPC;
	if(msize > c->iounit && c->iounit != 0)
		msize = c->iounit;
	v = version;
	if(v == nil || v[0] == '\0')
		v = VERSION9P;

	/* validity */
	if(msize < 0)
		error("bad iounit in version call");
	if(strncmp(v, VERSION9P, strlen(VERSION9P)) != 0)
		error("bad 9P version specification");

	m = c->mux;

	if(m != nil){
		qunlock(&c->umqlock);
		poperror();

		strecpy(buf, buf+sizeof buf, m->version);
		k = strlen(buf);
		if(strncmp(buf, v, k) != 0){
			snprint(buf, sizeof buf, "incompatible 9P versions %s %s", m->version, v);
			error(buf);
		}
		if(returnlen > 0){
			if(returnlen < k)
				error(Eshort);
			memmove(version, buf, k);
		}
		return k;
	}

	f.type = Tversion;
	f.tag = NOTAG;
	f.msize = msize;
	f.version = v;
	msg = malloc(MAXRPC0);
	if(msg == nil)
		exhausted("version memory");
	if(waserror()){
		free(msg);
		nexterror();
	}
	k = convS2M(&f, msg, MAXRPC0);
	if(k == 0)
		error("bad fversion conversion on send");

	lock(c);
	oo = c->offset;
	c->offset += k;
	unlock(c);

	l = devtab[c->type]->write(c, msg, k, oo);
	if(l < k){
		lock(c);
		c->offset -= k - l;
		unlock(c);
		error("short write in fversion");
	}

	/* message sent; receive and decode reply */
	for(k = 0; k < BIT32SZ || (k < GBIT32(msg) && k < MAXRPC0); k += l){
		l = devtab[c->type]->read(c, msg+k, MAXRPC0-k, c->offset);
		if(l <= 0)
			error("EOF receiving fversion reply");
		lock(c);
		c->offset += l;
		unlock(c);
	}

	l = convM2S(msg, k, &f);
	if(l != k)
		error("bad fversion conversion on reply");
	if(f.type != Rversion){
		if(f.type == Rerror)
			error(f.ename);
		error("unexpected reply type in fversion");
	}
	if(f.msize > msize)
		error("server tries to increase msize in fversion");
	if(f.msize<256 || f.msize>1024*1024)
		error("nonsense value of msize in fversion");
	k = strlen(f.version);
	if(strncmp(f.version, v, k) != 0)
		error("bad 9P version returned from server");
	if(returnlen > 0 && returnlen < k)
		error(Eshort);

	v = nil;
	kstrdup(&v, f.version);
	q = qopen(10*MAXRPC, 0, nil, nil);
	if(q == nil){
		free(v);
		exhausted("mount queues");
	}

	/* now build Mnt associated with this connection */
	lock(&mntalloc);
	m = mntalloc.mntfree;
	if(m != nil)
		mntalloc.mntfree = m->list;
	else {
		unlock(&mntalloc);
		m = malloc(sizeof(Mnt));
		if(m == nil) {
			qfree(q);
			free(v);
			exhausted("mount devices");
		}
		lock(&mntalloc);
	}
	m->list = mntalloc.list;
	mntalloc.list = m;
	m->version = v;
	m->id = mntalloc.id++;
	m->q = q;
	m->msize = f.msize;
	unlock(&mntalloc);

	if(returnlen > 0)
		memmove(version, f.version, k);	/* length was checked above */

	poperror();	/* msg */
	free(msg);

	lock(m);
	m->queue = nil;
	m->rip = nil;

	c->flag |= CMSG;
	c->mux = m;
	m->c = c;
	unlock(m);

	poperror();	/* c */
	qunlock(&c->umqlock);

	return k;
}

Chan*
mntauth(Chan *c, char *spec)
{
	Mnt *m;
	Mntrpc *r;

	m = c->mux;
	if(m == nil){
		mntversion(c, nil, 0, 0);
		m = c->mux;
		if(m == nil)
			error(Enoversion);
	}

	c = mntchan();
	if(waserror()) {
		/* Close must not be called since it will
		 * call mnt recursively
		 */
		chanfree(c);
		nexterror();
	}

	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}

	r->request.type = Tauth;
	r->request.afid = c->fid;
	r->request.uname = up->user;
	r->request.aname = spec;
	mountrpc(m, r);

	c->qid = r->reply.aqid;
	c->mchan = m->c;
	incref(m->c);
	c->mqid = c->qid;
	c->mode = ORDWR;
	c->iounit = m->msize-IOHDRSZ;

	poperror();	/* r */
	mntfree(r);

	poperror();	/* c */

	return c;

}

Chan*
mntattach(Chan *c, Chan *ac, char *spec, int flags)
{
	Mnt *m;
	Mntrpc *r;

	if(ac != nil && ac->mchan != c)
		error(Ebadusefd);

	m = c->mux;
	if(m == nil){
		mntversion(c, nil, 0, 0);
		m = c->mux;
		if(m == nil)
			error(Enoversion);
	}

	c = mntchan();
	if(waserror()) {
		/* Close must not be called since it will
		 * call mnt recursively
		 */
		chanfree(c);
		nexterror();
	}

	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Tattach;
	r->request.fid = c->fid;
	if(ac == nil)
		r->request.afid = NOFID;
	else
		r->request.afid = ac->fid;
	r->request.uname = up->user;
	r->request.aname = spec;
	mountrpc(m, r);

	c->qid = r->reply.qid;
	c->mchan = m->c;
	incref(m->c);
	c->mqid = c->qid;

	poperror();	/* r */
	mntfree(r);

	poperror();	/* c */

	if(flags&MCACHE)
		c->flag |= CCACHE;
	return c;
}

static Chan*
noattach(char *)
{
	error(Enoattach);
}

static Chan*
mntchan(void)
{
	Chan *c;

	c = devattach('M', 0);
	lock(&mntalloc);
	c->dev = mntalloc.id++;
	unlock(&mntalloc);

	if(c->mchan != nil)
		panic("mntchan non-zero %p", c->mchan);
	return c;
}

static Walkqid*
mntwalk(Chan *c, Chan *nc, char **name, int nname)
{
	int i, alloc;
	Mnt *m;
	Mntrpc *r;
	Walkqid *wq;

	if(nc != nil)
		print("mntwalk: nc != nil\n");
	if(nname > MAXWELEM)
		error("devmnt: too many name elements");
	alloc = 0;
	wq = smalloc(sizeof(Walkqid)+(nname-1)*sizeof(Qid));
	if(waserror()){
		if(alloc && wq->clone!=nil)
			cclose(wq->clone);
		free(wq);
		return nil;
	}

	alloc = 0;
	m = mntchk(c);
	r = mntralloc(c);
	if(nc == nil){
		nc = devclone(c);
		/*
		 * Until the other side accepts this fid, we can't mntclose it.
		 * Therefore set type to 0 for now; rootclose is known to be safe.
		 */
		nc->type = 0;
		nc->flag |= (c->flag & CCACHE);
		alloc = 1;
	}
	wq->clone = nc;

	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Twalk;
	r->request.fid = c->fid;
	r->request.newfid = nc->fid;
	r->request.nwname = nname;
	memmove(r->request.wname, name, nname*sizeof(char*));

	mountrpc(m, r);

	if(r->reply.nwqid > nname)
		error("too many QIDs returned by walk");
	if(r->reply.nwqid < nname){
		if(alloc)
			cclose(nc);
		wq->clone = nil;
		if(r->reply.nwqid == 0){
			free(wq);
			wq = nil;
			goto Return;
		}
	}

	/* move new fid onto mnt device and update its qid */
	if(wq->clone != nil){
		if(wq->clone != c){
			wq->clone->type = c->type;
			wq->clone->mchan = c->mchan;
			incref(c->mchan);
		}
		if(r->reply.nwqid > 0)
			wq->clone->qid = r->reply.wqid[r->reply.nwqid-1];
	}
	wq->nqid = r->reply.nwqid;
	for(i=0; i<wq->nqid; i++)
		wq->qid[i] = r->reply.wqid[i];

    Return:
	poperror();
	mntfree(r);
	poperror();
	return wq;
}

static int
mntstat(Chan *c, uchar *dp, int n)
{
	Mnt *m;
	Mntrpc *r;

	if(n < BIT16SZ)
		error(Eshortstat);
	m = mntchk(c);
	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Tstat;
	r->request.fid = c->fid;
	mountrpc(m, r);

	if(r->reply.nstat > n){
		n = BIT16SZ;
		PBIT16((uchar*)dp, r->reply.nstat-2);
	}else{
		n = r->reply.nstat;
		memmove(dp, r->reply.stat, n);
		validstat(dp, n);
		mntdirfix(dp, c);
	}
	poperror();
	mntfree(r);
	return n;
}

static Chan*
mntopencreate(int type, Chan *c, char *name, int omode, ulong perm)
{
	Mnt *m;
	Mntrpc *r;

	m = mntchk(c);
	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = type;
	r->request.fid = c->fid;
	r->request.mode = omode;
	if(type == Tcreate){
		r->request.perm = perm;
		r->request.name = name;
	}
	mountrpc(m, r);

	c->qid = r->reply.qid;
	c->offset = 0;
	c->mode = openmode(omode);
	c->iounit = r->reply.iounit;
	if(c->iounit == 0 || c->iounit > m->msize-IOHDRSZ)
		c->iounit = m->msize-IOHDRSZ;
	c->flag |= COPEN;
	poperror();
	mntfree(r);

	if(c->flag & CCACHE){
		if(copen(c))
		if(type == Tcreate || (omode&OTRUNC) != 0)
			ctrunc(c);
	}

	return c;
}

static Chan*
mntopen(Chan *c, int omode)
{
	return mntopencreate(Topen, c, nil, omode, 0);
}

static Chan*
mntcreate(Chan *c, char *name, int omode, ulong perm)
{
	return mntopencreate(Tcreate, c, name, omode, perm);
}

static void
mntclunk(Chan *c, int t)
{
	Mnt *m;
	Mntrpc *r;

	cclunk(c);
	m = mntchk(c);
	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = t;
	r->request.fid = c->fid;
	mountrpc(m, r);
	mntfree(r);
	poperror();
}

void
muxclose(Mnt *m)
{
	Mnt *f, **l;
	Mntrpc *r;

	while((r = m->queue) != nil){
		m->queue = r->list;
		mntfree(r);
	}
	m->id = 0;
	free(m->version);
	m->version = nil;
	qfree(m->q);
	m->q = nil;

	lock(&mntalloc);
	l = &mntalloc.list;
	for(f = *l; f != nil; f = f->list) {
		if(f == m) {
			*l = m->list;
			break;
		}
		l = &f->list;
	}
	m->list = mntalloc.mntfree;
	mntalloc.mntfree = m;
	unlock(&mntalloc);
}

static void
mntclose(Chan *c)
{
	mntclunk(c, Tclunk);
}

static void
mntremove(Chan *c)
{
	mntclunk(c, Tremove);
}

static int
mntwstat(Chan *c, uchar *dp, int n)
{
	Mnt *m;
	Mntrpc *r;

	m = mntchk(c);
	r = mntralloc(c);
	if(waserror()) {
		mntfree(r);
		nexterror();
	}
	r->request.type = Twstat;
	r->request.fid = c->fid;
	r->request.nstat = n;
	r->request.stat = dp;
	mountrpc(m, r);
	poperror();
	mntfree(r);

	if(c->flag & CCACHE)
	if(GBIT64(&dp[STATFIXLEN-4*BIT16SZ-BIT64SZ]) != ~0ULL)
		ctrunc(c);

	return n;
}

static long
mntread(Chan *c, void *buf, long n, vlong off)
{
	uchar *p, *e;
	int dirlen;

	p = buf;
	n = mntrdwr(Tread, c, p, n, off);
	if(c->qid.type & QTDIR) {
		for(e = &p[n]; p+BIT16SZ < e; p += dirlen){
			dirlen = BIT16SZ+GBIT16(p);
			if(p+dirlen > e)
				break;
			validstat(p, dirlen);
			mntdirfix(p, c);
		}
		if(p != e)
			error(Esbadstat);
	}
	return n;
}

static long
mntwrite(Chan *c, void *buf, long n, vlong off)
{
	return mntrdwr(Twrite, c, buf, n, off);
}

static void
mntcache(Mntrpc *r)
{
	ulong n, m;
	vlong off;
	Block *b;
	Chan *c;

	c = r->c;
	if(!cachedchan(c))
		return;
	off = r->request.offset;
	switch(r->reply.type){
	case Rread:
		m = r->reply.count;
		if(m > r->request.count)
			m = r->request.count;
		for(b = r->b; m > 0 && b != nil; m -= n, b = b->next) {
			n = BLEN(b);
			if(m < n)
				n = m;
			cupdate(c, b->rp, n, off);
			off += n;
		}
		break;
	case Rwrite:
		b = r->w;
		if(convM2S(b->rp, BLEN(b), &r->request) == 0)
			panic("convM2S");
		m = r->reply.count;
		if(m > r->request.count)
			m = r->request.count;
		cwrite(c, (uchar*)r->request.data, m, off);
		break;
	}
}

static long
mntrdwr(int type, Chan *c, void *buf, long n, vlong off)
{
	Mnt *m;
 	Mntrpc *r;
	char *uba;
	ulong cnt, nr, nreq;

	m = mntchk(c);
	uba = buf;
	cnt = 0;

	for(;;) {
		nreq = n;
		if(nreq > c->iounit)
			nreq = c->iounit;

		if(type == Tread && cachedchan(c)) {
			nr = cread(c, (uchar*)uba, nreq, off);
			if(nr > 0) {
				nreq = nr;
				goto Next;
			}
		}

		r = mntralloc(c);
		if(waserror()) {
			mntfree(r);
			nexterror();
		}
		r->request.type = type;
		r->request.fid = c->fid;
		r->request.offset = off;
		r->request.data = uba;
		r->request.count = nreq;
		mountrpc(m, r);
		mntcache(r);
		nr = r->reply.count;
		if(nr > nreq)
			nr = nreq;
		if(type == Tread)
			nr = readblist(r->b, (uchar*)uba, nr, 0);
		mntfree(r);
		poperror();

	Next:
		off += nr;
		uba += nr;
		cnt += nr;
		n -= nr;
		if(nr != nreq || n == 0 || up->nnote)
			break;
	}
	return cnt;
}

static int
mntprocwork(void *a)
{
	Mntproc *p = a;
	return p->f != nil;
}

static void
mntproc(void *a)
{
	Mntproc *p = a;
	Chan *c;
	Mnt *m;

	while(waserror())
		;

	m = p->m;
	for(;;){
		tsleep(p, mntprocwork, p, 500);

		lock(m);
		if(p->f == nil){
			p->m = nil;
			unlock(m);
			pexit("no work", 1);
		}
		c = p->r->c;
		unlock(m);

		(*p->f)(p->r, p->a);

		lock(m);
		p->r = nil;
		p->a = nil;
		p->f = nil;
		unlock(m);

		cclose(c);
	}
}

static int
mntdefer(void (*f)(Mntrpc*, void*), Mntrpc *r, void *a)
{
	Mntproc *p;
	Mnt *m;
	int i;

	m = mntchk(r->c);
	lock(m);
	for(i = 0; i < nelem(m->defered); i++){
		p = &m->defered[i];
		if(p->f != nil)
			continue;

		incref(r->c);
		r->m = m;
		p->r = r;
		p->a = a;
		p->f = f;

		if(p->m == nil){
			p->m = m;
			unlock(m);
			kproc("mntproc", mntproc, p);
		} else {
			unlock(m);
			wakeup(p);
		}
		return 1;
	}
	unlock(m);
	return 0;
}

static void
rahproc(Mntrpc *r, void *a)
{
	Mntrah *rah = a;

	if(!waserror()){
		mountrpc(r->m, r);
		poperror();
	}
	r->done = 2;
	wakeup(rah);
}

static int
rahdone(void *v)
{
	Mntrpc *r = v;
	return r->done == 2;
}

static Mntrpc*
rahfindrpc(Mntrah *rah, vlong off)
{
	Mntrpc *r;
	int i, n;
	vlong o;

	for(i=0; i<nelem(rah->r); i++){
		if((r = rah->r[i]) == nil)
			continue;
		n = r->request.count;
		o = r->request.offset;
		if(off >= o && off < o+n)
			return r;
	}
	return nil;
}

void
mntrahinit(Mntrah *rah)
{
	Mntrpc *r;
	int i;

	while(waserror())
		;

	for(i=0; i<nelem(rah->r); i++){
		if((r = rah->r[i]) != nil){
			while(!rahdone(r))
				sleep(rah, rahdone, r);
			rah->r[i] = nil;
			mntfree(r);
		}
	}
	rah->i = 0;

	rah->off = 0;
	rah->seq = 0;

	poperror();
}

long
mntrahread(Mntrah *rah, Chan *c, uchar *buf, long len, vlong off)
{
	Mntrpc *r, **rr;
	vlong o, w, e;
	long n, tot;

	if(len <= 0)
		return 0;
	if(off != rah->off){
		rah->off = off;
		if(rahfindrpc(rah, off) == nil)
			rah->seq = 0;
	}
	rah->off += len;
	rah->seq += len;
	if(rah->seq >= 2*c->iounit){
		w = (off / c->iounit) * c->iounit;
		e = w + rah->seq;
		for(o = w; o < e; o += c->iounit){
			if(rahfindrpc(rah, o) != nil)
				continue;

			rr = &rah->r[rah->i % nelem(rah->r)];
			if((r = *rr) != nil){
				if(!rahdone(r) || (r->request.offset >= w && r->request.offset < e))
					break;
				*rr = nil;
				mntfree(r);
			}

			r = mntralloc(c);
			r->request.type = Tread;
			r->request.fid = c->fid;
			r->request.offset = o;
			r->request.count = c->iounit;
			if(!mntdefer(rahproc, r, rah)){
				mntfree(r);
				break;
			}
			*rr = r;
			rah->i++;
		}
	}

	tot = 0;
	while(len > 0 && (r = rahfindrpc(rah, off)) != nil){
		while(!rahdone(r))
			sleep(rah, rahdone, r);

		switch(r->reply.type){
		default:
			error(Emountrpc);
		case Rflush:
			error(Eintr);
		case Rerror:
			error(r->reply.ename);
		case Rread:
			break;
		}
		mntcache(r);
		n = r->request.count;
		o = r->request.offset;
		if(r->reply.count < n)
			n = r->reply.count;
		n -= (off - o);
		if(n <= 0)
			break;
		if(len < n)
			n = len;
		n = readblist(r->b, buf, n, off - o);
		buf += n;
		off += n;
		tot += n;
		len -= n;
	}
	if(tot > 0){
		rah->off -= len;
		rah->seq -= len;
	}

	return tot;
}

static void
mountrpc(Mnt *m, Mntrpc *r)
{
	int t;

	r->reply.tag = 0;
	r->reply.type = Tmax;	/* can't ever be a valid message type */

	mountio(m, r);

	t = r->reply.type;
	switch(t) {
	case Rerror:
		error(r->reply.ename);
	case Rflush:
		error(Eintr);
	default:
		if(t == r->request.type+1)
			break;
		print("mnt: proc %s %lud: mismatch from %s %s rep %#p tag %d fid %d T%d R%d rp %d\n",
			up->text, up->pid, chanpath(m->c), chanpath(r->c),
			r, r->request.tag, r->request.fid, r->request.type,
			r->reply.type, r->reply.tag);
		error(Emountrpc);
	}
}

static void
mountio(Mnt *m, Mntrpc *r)
{
	Block *b;
	int n;

	while(waserror()) {
		if(m->rip == up)
			mntgate(m);
		if(strcmp(up->errstr, Eintr) != 0 || waserror()){
			r = mntflushfree(m, r);
			switch(r->request.type){
			case Tremove:
			case Tclunk:
				/* botch, abandon fid */ 
				if(strcmp(up->errstr, Ehungup) != 0)
					r->c->fid = 0;
			}
			nexterror();
		}
		r = mntflushalloc(r);
		poperror();
	}

	lock(m);
	r->z = &up->sleep;
	r->m = m;
	r->list = m->queue;
	m->queue = r;
	unlock(m);

	/* Transmit a file system rpc */
	n = sizeS2M(&r->request);
	b = allocb(n);
	if(waserror()){
		freeb(b);
		nexterror();
	}
	n = convS2M(&r->request, b->wp, n);
	if(n <= 0 || n > m->msize) {
		print("mountio: proc %s %lud: convS2M returned %d for tag %d fid %d T%d\n",
			up->text, up->pid, n, r->request.tag, r->request.fid, r->request.type);
		error(Emountrpc);
	}
	b->wp += n;
	if(r->request.type == Twrite && cachedchan(r->c))
		r->w = copyblock(b, n);
	poperror();
	devtab[m->c->type]->bwrite(m->c, b, 0);

	/* Gate readers onto the mount point one at a time */
	for(;;) {
		lock(m);
		if(m->rip == nil)
			break;
		unlock(m);
		sleep(r->z, rpcattn, r);
		if(r->done) {
			poperror();
			mntflushfree(m, r);
			return;
		}
	}
	m->rip = up;
	unlock(m);
	while(r->done == 0) {
		if(mntrpcread(m, r) < 0)
			error(Emountrpc);
		mountmux(m, r);
	}
	mntgate(m);
	poperror();
	mntflushfree(m, r);
}

static int
doread(Mnt *m, int len)
{
	Block *b;

	while(qlen(m->q) < len){
		b = devtab[m->c->type]->bread(m->c, m->msize, 0);
		if(b == nil || qaddlist(m->q, b) == 0)
			return -1;
	}
	return 0;
}

static int
mntrpcread(Mnt *m, Mntrpc *r)
{
	int i, t, len, hlen;
	Block *b, **l, *nb;

	r->reply.type = 0;
	r->reply.tag = 0;

	/* read at least length, type, and tag and pullup to a single block */
	if(doread(m, BIT32SZ+BIT8SZ+BIT16SZ) < 0)
		return -1;
	nb = pullupqueue(m->q, BIT32SZ+BIT8SZ+BIT16SZ);

	/* read in the rest of the message, avoid ridiculous (for now) message sizes */
	len = GBIT32(nb->rp);
	if(len < BIT32SZ+BIT8SZ+BIT16SZ || len > m->msize){
		qflush(m->q);
		return -1;
	}
	if(doread(m, len) < 0)
		return -1;

	/* pullup the header (i.e. everything except data) */
	t = nb->rp[BIT32SZ];
	switch(t){
	case Rread:
		hlen = BIT32SZ+BIT8SZ+BIT16SZ+BIT32SZ;
		break;
	default:
		hlen = len;
		break;
	}
	nb = pullupqueue(m->q, hlen);

	if(convM2S(nb->rp, len, &r->reply) <= 0){
		/* bad message, dump it */
		print("mntrpcread: convM2S failed\n");
		qdiscard(m->q, len);
		return -1;
	}

	/* hang the data off of the fcall struct */
	l = &r->b;
	*l = nil;
	do {
		b = qremove(m->q);
		if(hlen > 0){
			b->rp += hlen;
			len -= hlen;
			hlen = 0;
		}
		i = BLEN(b);
		if(i <= len){
			len -= i;
			*l = b;
			l = &(b->next);
		} else {
			/* split block and put unused bit back */
			nb = allocb(i-len);
			memmove(nb->wp, b->rp+len, i-len);
			b->wp = b->rp+len;
			nb->wp += i-len;
			qputback(m->q, nb);
			*l = b;
			return 0;
		}
	}while(len > 0);

	return 0;
}

static void
mntgate(Mnt *m)
{
	Mntrpc *q;

	lock(m);
	m->rip = nil;
	for(q = m->queue; q != nil; q = q->list) {
		if(q->done == 0)
		if(wakeup(q->z))
			break;
	}
	unlock(m);
}

static void
mountmux(Mnt *m, Mntrpc *r)
{
	Mntrpc **l, *q;
	Rendez *z;

	lock(m);
	l = &m->queue;
	for(q = *l; q != nil; q = q->list) {
		/* look for a reply to a message */
		if(q->request.tag == r->reply.tag) {
			*l = q->list;
			if(q == r) {
				q->done = 1;
				unlock(m);
				return;
			}
			/*
			 * Completed someone else.
			 * Trade pointers to receive buffer.
			 */
			q->reply = r->reply;
			q->b = r->b;
			r->b = nil;
			z = q->z;
			coherence();
			q->done = 1;
			wakeup(z);
			unlock(m);
			return;
		}
		l = &q->list;
	}
	unlock(m);
	print("mnt: unexpected reply from %s tag %ud; type %d\n",
		chanpath(m->c), r->reply.tag, r->reply.type);
}

/*
 * Create a new flush request and chain the previous
 * requests from it
 */
static Mntrpc*
mntflushalloc(Mntrpc *r)
{
	Mntrpc *fr;

	fr = mntralloc(r->c);
	fr->request.type = Tflush;
	if(r->request.type == Tflush)
		fr->request.oldtag = r->request.oldtag;
	else
		fr->request.oldtag = r->request.tag;
	fr->flushed = r;

	return fr;
}

/*
 *  Free a chain of flushes.  Remove each unanswered
 *  flush and the original message from the unanswered
 *  request queue.  Mark the original message as done
 *  and if it hasn't been answered set the reply to to
 *  Rflush. Return the original rpc.
 */
static Mntrpc*
mntflushfree(Mnt *m, Mntrpc *r)
{
	Mntrpc *fr;

	while(r != nil){
		fr = r->flushed;
		if(!r->done){
			r->reply.type = Rflush;
			mntqrm(m, r);
		}
		if(fr == nil)
			break;
		mntfree(r);
		r = fr;
	}
	return r;
}

static int
alloctag(void)
{
	int i, j;
	u32int v;

	for(i = 0; i < NMASK; i++){
		v = mntalloc.tagmask[i];
		if(v == -1)
			continue;
		for(j = 0; (v & 1) != 0; j++)
			v >>= 1;
		mntalloc.tagmask[i] |= 1<<j;
		return i<<TAGSHIFT | j;
	}
	panic("no friggin tags left");
}

static void
freetag(int t)
{
	mntalloc.tagmask[t>>TAGSHIFT] &= ~(1<<(t&TAGMASK));
}

static Mntrpc*
mntralloc(Chan *c)
{
	Mntrpc *new;

	if(mntalloc.nrpcfree == 0) {
	Alloc:
		new = malloc(sizeof(Mntrpc));
		if(new == nil)
			exhausted("mount rpc header");
		lock(&mntalloc);
		new->request.tag = alloctag();
	} else {
		lock(&mntalloc);
		new = mntalloc.rpcfree;
		if(new == nil) {
			unlock(&mntalloc);
			goto Alloc;
		}
		mntalloc.rpcfree = new->list;
		mntalloc.nrpcfree--;
	}
	mntalloc.nrpcused++;
	unlock(&mntalloc);
	new->c = c;
	new->done = 0;
	new->flushed = nil;
	new->b = nil;
	new->w = nil;
	return new;
}

static void
mntfree(Mntrpc *r)
{
	freeb(r->w);
	freeblist(r->b);
	lock(&mntalloc);
	mntalloc.nrpcused--;
	if(mntalloc.nrpcfree < 32) {
		r->list = mntalloc.rpcfree;
		mntalloc.rpcfree = r;
		mntalloc.nrpcfree++;
		unlock(&mntalloc);
		return;
	}
	freetag(r->request.tag);
	unlock(&mntalloc);
	free(r);
}

static void
mntqrm(Mnt *m, Mntrpc *r)
{
	Mntrpc **l, *f;

	lock(m);
	r->done = 1;

	l = &m->queue;
	for(f = *l; f != nil; f = f->list) {
		if(f == r) {
			*l = r->list;
			break;
		}
		l = &f->list;
	}
	unlock(m);
}

static Mnt*
mntchk(Chan *c)
{
	Mnt *m;

	/* This routine is mostly vestiges of prior lives; now it's just sanity checking */
	if(c->mchan == nil)
		panic("mntchk 1: nil mchan c %s", chanpath(c));

	m = c->mchan->mux;
	if(m == nil)
		print("mntchk 2: nil mux c %s c->mchan %s \n", chanpath(c), chanpath(c->mchan));

	/*
	 * Was it closed and reused (was error(Eshutdown); now, it cannot happen)
	 */
	if(m->id == 0 || m->id >= c->dev)
		panic("mntchk 3: can't happen");

	return m;
}

/*
 * Rewrite channel type and dev for in-flight data to
 * reflect local values.  These entries are known to be
 * the first two in the Dir encoding after the count.
 */
static void
mntdirfix(uchar *dirbuf, Chan *c)
{
	uint r;

	r = devtab[c->type]->dc;
	dirbuf += BIT16SZ;	/* skip count */
	PBIT16(dirbuf, r);
	dirbuf += BIT16SZ;
	PBIT32(dirbuf, c->dev);
}

static int
rpcattn(void *v)
{
	Mntrpc *r;

	r = v;
	return r->done || r->m->rip == nil;
}

Dev mntdevtab = {
	'M',
	"mnt",

	mntreset,
	devinit,
	devshutdown,
	noattach,
	mntwalk,
	mntstat,
	mntopen,
	mntcreate,
	mntclose,
	mntread,
	devbread,
	mntwrite,
	devbwrite,
	mntremove,
	mntwstat,
};
