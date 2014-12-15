#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

enum
{
	NHASH		= 128,
	MAXCACHE	= 1024*1024,
	NFILE		= 4096,
	NEXTENT		= 200,		/* extent allocation size */
};

typedef struct Extent Extent;
struct Extent
{
	uintptr	bid;
	ulong	start;
	int	len;
	Page	*cache;
	Extent	*next;
};

typedef struct Mntcache Mntcache;
struct Mntcache
{
	Qid	qid;
	int	dev;
	int	type;
	QLock;
	Extent	 *list;
	Mntcache *hash;
	Mntcache *prev;
	Mntcache *next;
};

typedef struct Cache Cache;
struct Cache
{
	Lock;
	uintptr		pgno;
	Mntcache	*head;
	Mntcache	*tail;
	Mntcache	*hash[NHASH];
};

typedef struct Ecache Ecache;
struct Ecache
{
	Lock;
	int	total;
	int	free;
	Extent*	head;
};

Image fscache;

static Cache cache;
static Ecache ecache;
static ulong maxcache = MAXCACHE;

static void
extentfree(Extent* e)
{
	lock(&ecache);
	e->next = ecache.head;
	ecache.head = e;
	ecache.free++;
	unlock(&ecache);
}

static Extent*
extentalloc(void)
{
	Extent *e;
	int i;

	lock(&ecache);
	if(ecache.head == nil){
		e = xalloc(NEXTENT*sizeof(Extent));
		if(e == nil){
			unlock(&ecache);
			return nil;
		}
		for(i = 0; i < NEXTENT; i++){
			e->next = ecache.head;
			ecache.head = e;
			e++;
		}
		ecache.total += NEXTENT;
		ecache.free += NEXTENT;
	}

	e = ecache.head;
	ecache.head = e->next;
	ecache.free--;
	unlock(&ecache);

	memset(e, 0, sizeof(Extent));

	return e;
}

void
cinit(void)
{
	int i;
	Mntcache *m;

	cache.head = xalloc(sizeof(Mntcache)*NFILE);
	m = cache.head;
	if (m == nil)
		panic("cinit: no memory");

	/* a better algorithm would be nice */
	if(conf.npage*BY2PG > 200*MB)
		maxcache = 10*MAXCACHE;

	for(i = 0; i < NFILE-1; i++) {
		m->next = m+1;
		m->prev = m-1;
		m++;
	}

	cache.tail = m;
	cache.tail->next = nil;
	cache.head->prev = nil;

	fscache.notext = 1;
}

static Page*
cpage(Extent *e)
{
	/* Easy consistency check */
	if(e->cache->daddr != e->bid)
		return nil;

	return lookpage(&fscache, e->bid);
}

static void
cnodata(Mntcache *m)
{
	Extent *e;

	/*
	 * Invalidate all extent data
	 * pagereclaim() will waste the pages
	 */
	while((e = m->list) != nil){
		m->list = e->next;
		extentfree(e);
	}
}

static void
ctail(Mntcache *m)
{
	/* Unlink and send to the tail */
	if(m->prev != nil)
		m->prev->next = m->next;
	else
		cache.head = m->next;
	if(m->next != nil)
		m->next->prev = m->prev;
	else
		cache.tail = m->prev;

	if(cache.tail != nil) {
		m->prev = cache.tail;
		cache.tail->next = m;
		m->next = nil;
		cache.tail = m;
	}
	else {
		cache.head = m;
		cache.tail = m;
		m->prev = nil;
		m->next = nil;
	}
}

/* called with cache locked */
static Mntcache*
clookup(Chan *c, int skipvers)
{
	Mntcache *m;

	for(m = cache.hash[c->qid.path%NHASH]; m != nil; m = m->hash)
		if(eqchantdqid(c, m->type, m->dev, m->qid, skipvers) && c->qid.type == m->qid.type)
			return m;

	return nil;
}

void
copen(Chan *c)
{
	Mntcache *m, *f, **l;

	/* directories aren't cacheable and append-only files confuse us */
	if(c->qid.type&(QTDIR|QTAPPEND)){
		c->mcp = nil;
		return;
	}

	lock(&cache);
	m = clookup(c, 1);
	if(m == nil)
		m = cache.head;
	else if(m->qid.vers == c->qid.vers) {
		ctail(m);
		unlock(&cache);
		c->mcp = m;
		return;
	}
	ctail(m);

	l = &cache.hash[m->qid.path%NHASH];
	for(f = *l; f != nil; f = f->hash) {
		if(f == m) {
			*l = m->hash;
			break;
		}
		l = &f->hash;
	}

	if(!canqlock(m)){
		unlock(&cache);
		qlock(m);
		lock(&cache);
		f = clookup(c, 0);
		if(f != nil) {
			/*
			 * someone got there first while cache lock
			 * was released and added a updated Mntcache
			 * for us. update LRU and use it.
			 */
			ctail(f);
			unlock(&cache);
			qunlock(m);
			c->mcp = f;
			return;
		}
	}

	m->qid = c->qid;
	m->dev = c->dev;
	m->type = c->type;

	l = &cache.hash[c->qid.path%NHASH];
	m->hash = *l;
	*l = m;
	unlock(&cache);
	cnodata(m);
	qunlock(m);
	c->mcp = m;
}

/* return locked Mntcache if still valid else reset mcp */
static Mntcache*
ccache(Chan *c)
{
	Mntcache *m;

	m = c->mcp;
	if(m != nil) {
		qlock(m);
		if(eqchantdqid(c, m->type, m->dev, m->qid, 0) && c->qid.type == m->qid.type)
			return m;
		c->mcp = nil;
		qunlock(m);
	}
	return nil;
}

int
cread(Chan *c, uchar *buf, int len, vlong off)
{
	KMap *k;
	Page *p;
	Mntcache *m;
	Extent *e, **t;
	int o, l, total;
	ulong offset;

	if(off >= maxcache || len <= 0)
		return 0;

	m = ccache(c);
	if(m == nil)
		return 0;

	offset = off;
	t = &m->list;
	for(e = *t; e != nil; e = e->next) {
		if(offset >= e->start && offset < e->start+e->len)
			break;
		t = &e->next;
	}

	if(e == nil) {
		qunlock(m);
		return 0;
	}

	total = 0;
	while(len > 0) {
		p = cpage(e);
		if(p == nil) {
			*t = e->next;
			extentfree(e);
			break;
		}

		o = offset - e->start;
		l = len;
		if(l > e->len-o)
			l = e->len-o;

		k = kmap(p);
		if(waserror()) {
			kunmap(k);
			putpage(p);
			qunlock(m);
			nexterror();
		}

		memmove(buf, (uchar*)VA(k) + o, l);

		poperror();
		kunmap(k);

		putpage(p);

		buf += l;
		len -= l;
		offset += l;
		total += l;
		t = &e->next;
		e = e->next;
		if(e == nil || e->start != offset)
			break;
	}

	qunlock(m);
	return total;
}

static Extent*
cchain(uchar *buf, ulong offset, int len, Extent **tail)
{
	int l;
	Page *p;
	KMap *k;
	Extent *e, *start, **t;

	start = nil;
	*tail = nil;
	t = &start;
	while(len > 0) {
		e = extentalloc();
		if(e == nil)
			break;

		p = auxpage();
		if(p == nil) {
			extentfree(e);
			break;
		}
		l = len;
		if(l > BY2PG)
			l = BY2PG;

		e->cache = p;
		e->start = offset;
		e->len = l;

		lock(&cache);
		e->bid = cache.pgno;
		cache.pgno += BY2PG;
		/* wrap the counter; low bits are unused by pghash but checked by lookpage */
		if((cache.pgno & ~(BY2PG-1)) == 0){
			if(cache.pgno == BY2PG-1){
				print("cache wrapped\n");
				cache.pgno = 0;
			}else
				cache.pgno++;
		}
		unlock(&cache);

		p->daddr = e->bid;
		k = kmap(p);
		if(waserror()) {		/* buf may be virtual */
			kunmap(k);
			nexterror();
		}
		memmove((void*)VA(k), buf, l);
		poperror();
		kunmap(k);

		cachepage(p, &fscache);
		putpage(p);

		buf += l;
		offset += l;
		len -= l;

		*t = e;
		*tail = e;
		t = &e->next;
	}

	return start;
}

static int
cpgmove(Extent *e, uchar *buf, int boff, int len)
{
	Page *p;
	KMap *k;

	p = cpage(e);
	if(p == nil)
		return 0;

	k = kmap(p);
	if(waserror()) {		/* Since buf may be virtual */
		kunmap(k);
		nexterror();
	}

	memmove((uchar*)VA(k)+boff, buf, len);

	poperror();
	kunmap(k);
	putpage(p);

	return 1;
}

void
cupdate(Chan *c, uchar *buf, int len, vlong off)
{
	int o;
	Mntcache *m;
	Extent *tail;
	Extent *e, *f, *p;
	ulong offset, eblock, ee;

	if(off >= maxcache || len <= 0)
		return;

	m = ccache(c);
	if(m == nil)
		return;

	/*
	 * Find the insertion point
	 */
	offset = off;
	p = nil;
	for(f = m->list; f != nil; f = f->next) {
		if(f->start > offset)
			break;
		p = f;
	}

	/* trim if there is a successor */
	eblock = offset+len;
	if(f != nil && eblock > f->start) {
		len -= (eblock - f->start);
		if(len <= 0)
			goto out;
	}

	if(p == nil) {		/* at the head */
		e = cchain(buf, offset, len, &tail);
		if(e != nil) {
			m->list = e;
			tail->next = f;
		}
		goto out;
	}

	/* trim to the predecessor */
	ee = p->start+p->len;
	if(offset < ee) {
		o = ee - offset;
		len -= o;
		if(len <= 0)
			goto out;
		buf += o;
		offset += o;
	}

	/* try and pack data into the predecessor */
	if(offset == ee && p->len < BY2PG) {
		o = len;
		if(o > BY2PG - p->len)
			o = BY2PG - p->len;
		if(cpgmove(p, buf, p->len, o)) {
			p->len += o;
			buf += o;
			len -= o;
			offset += o;
			if(len <= 0) {
				if(f != nil && p->start + p->len > f->start)
					print("CACHE: p->start=%uld p->len=%d f->start=%uld\n",
						p->start, p->len, f->start);
				goto out;
			}
		}
	}

	e = cchain(buf, offset, len, &tail);
	if(e != nil) {
		p->next = e;
		tail->next = f;
	}
out:
	qunlock(m);
}

void
cwrite(Chan* c, uchar *buf, int len, vlong off)
{
	int o, eo;
	Mntcache *m;
	Extent *p, *f, *e, *tail;
	ulong offset, eblock, ee;

	if(off >= maxcache || len <= 0)
		return;

	m = ccache(c);
	if(m == nil)
		return;

	offset = off;
	m->qid.vers++;
	c->qid.vers++;

	p = nil;
	for(f = m->list; f != nil; f = f->next) {
		if(f->start >= offset)
			break;
		p = f;
	}

	if(p != nil) {
		ee = p->start+p->len;
		eo = offset - p->start;
		/* pack in predecessor if there is space */
		if(offset <= ee && eo < BY2PG) {
			o = len;
			if(o > BY2PG - eo)
				o = BY2PG - eo;
			if(cpgmove(p, buf, eo, o)) {
				if(eo+o > p->len)
					p->len = eo+o;
				buf += o;
				len -= o;
				offset += o;
			}
		}
	}

	/* free the overlap -- it's a rare case */
	eblock = offset+len;
	while(f != nil && f->start < eblock) {
		e = f->next;
		extentfree(f);
		f = e;
	}

	/* link the block (if any) into the middle */
	e = cchain(buf, offset, len, &tail);
	if(e != nil) {
		tail->next = f;
		f = e;
	}

	if(p == nil)
		m->list = f;
	else
		p->next = f;
	qunlock(m);
}
