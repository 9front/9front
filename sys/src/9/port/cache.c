#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

enum
{
	NHASH		= 128,
	NFILE		= 4093,		/* should be prime */
	MAXCACHE	= 8*1024*1024,

	MAPBITS		= 8*sizeof(ulong),
	NBITMAP		= (PGROUND(MAXCACHE)/BY2PG + MAPBITS-1) / MAPBITS,
};

/* devmnt.c: parallel read ahread implementation */
extern void mntrahinit(Mntrah *rah);
extern long mntrahread(Mntrah *rah, Chan *c, uchar *buf, long len, vlong off);

typedef struct Mntcache Mntcache;
struct Mntcache
{
	Qid		qid;
	int		dev;
	int		type;

	QLock;
	Proc		*locked;
	ulong		nlocked;

	Mntcache	*hash;
	Mntcache	*prev;
	Mntcache	*next;

	/* page bitmap of valid pages */
	ulong		bitmap[NBITMAP];

	/* read ahead state */
	Mntrah		rah;
};

typedef struct Cache Cache;
struct Cache
{
	Lock;
	Mntcache	*alloc;
	Mntcache	*head;
	Mntcache	*tail;
	Mntcache	*hash[NHASH];
};

Image fscache;

static Cache cache;

void
cinit(void)
{
	int i;
	Mntcache *m;

	m = xalloc(sizeof(Mntcache)*NFILE);
	if (m == nil)
		panic("cinit: no memory");

	cache.alloc = m;
	cache.head = m;

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

static uintptr
cacheaddr(Mntcache *m, ulong pn)
{
	uintptr da = pn * NFILE + (m - cache.alloc);
	return (da << PGSHIFT) | (da >> (sizeof(da)*8 - PGSHIFT));
}

static void
cnodata(Mntcache *m)
{
	memset(m->bitmap, 0, sizeof(m->bitmap));
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

/*
 * resursive Mntcache locking. Mntcache.rah is protected by the
 * same lock and we want to call cupdate() from mntrahread()
 * while holding the lock.
 */
static int
cancachelock(Mntcache *m)
{
	if(m->locked == up || canqlock(m)){
		m->locked = up;
		m->nlocked++;
		return 1;
	}
	return 0;
}
static void
cachelock(Mntcache *m)
{
	if(m->locked != up){
		qlock(m);
		assert(m->nlocked == 0);
		m->locked = up;
	}
	m->nlocked++;

}
static void
cacheunlock(Mntcache *m)
{
	assert(m->locked == up);
	if(--m->nlocked == 0){
		m->locked = nil;
		qunlock(m);
	}
}

/* return locked Mntcache if still valid else reset mcp */
static Mntcache*
ccache(Chan *c)
{
	Mntcache *m;

	m = c->mcp;
	if(m != nil) {
		cachelock(m);
		if(eqchantdqid(c, m->type, m->dev, m->qid, 0) && c->qid.type == m->qid.type)
			return m;
		c->mcp = nil;
		cacheunlock(m);
	}
	return nil;
}

int
copen(Chan *c)
{
	Mntcache *m, *f, **l;

	/* directories aren't cacheable */
	if(c->qid.type&QTDIR){
		c->mcp = nil;
		return 0;
	}

	lock(&cache);
	m = clookup(c, 0);
	if(m != nil){
		ctail(m);
		unlock(&cache);
		c->mcp = m;
		return 1;
	}
	m = clookup(c, 1);
	if(m == nil)
		m = cache.head;
	ctail(m);

	l = &cache.hash[m->qid.path%NHASH];
	for(f = *l; f != nil; f = f->hash) {
		if(f == m) {
			*l = m->hash;
			break;
		}
		l = &f->hash;
	}

	if(!cancachelock(m)){
		unlock(&cache);
		cachelock(m);
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
			cacheunlock(m);
			c->mcp = f;
			return 1;
		}
	}

	m->qid = c->qid;
	m->dev = c->dev;
	m->type = c->type;

	l = &cache.hash[c->qid.path%NHASH];
	m->hash = *l;
	*l = m;

	unlock(&cache);

	m->rah.vers = m->qid.vers;
	mntrahinit(&m->rah);
	cnodata(m);
	cacheunlock(m);
	c->mcp = m;
	return 0;
}

enum {
	VABITS	= 8*sizeof(uintptr) - 2*PGSHIFT,
	VAMASK	= (((uintptr)1 << VABITS)-1) << PGSHIFT,
};

static Page*
cpage(Mntcache *m, ulong pn, ulong *po, ulong *pe)
{
	ulong b;
	Page *p;

	b = 1 << (pn%MAPBITS);
	if((m->bitmap[pn/MAPBITS] & b) == 0)
		return nil;
	p = lookpage(&fscache, cacheaddr(m, pn));
	if(p == nil){
		m->bitmap[pn/MAPBITS] &= ~b;
		return nil;
	}
	*po = p->va & (BY2PG-1);
	*pe = 1 + (p->va >> (PGSHIFT+VABITS));
	assert(*po < *pe);
	return p;
}

static void
cpageset(Page *p, ulong po, ulong pe)
{
	assert(po < pe);
	p->va = po | (p->va & VAMASK) | ((uintptr)pe - 1) << (PGSHIFT+VABITS);
}

int
cread(Chan *c, uchar *buf, int len, vlong off)
{
	KMap *k;
	Page *p;
	Mntcache *m;
	int l, tot;
	ulong offset, pn, po, pe;

	if(len <= 0)
		return 0;

	m = ccache(c);
	if(m == nil)
		return 0;

	if(waserror()){
		cacheunlock(m);
		nexterror();
	}

	tot = 0;
	if(off >= MAXCACHE)
		goto Prefetch;

	offset = off;
	if(offset+len > MAXCACHE)
		len = MAXCACHE - offset;
	pn = offset / BY2PG;
	offset &= (BY2PG-1);

	while(len > 0){
		p = cpage(m, pn, &po, &pe);
		if(p == nil)
			break;
		if(offset < po || offset >= pe){
			putpage(p);
			break;
		}
		l = pe - offset;
		if(l > len)
			l = len;
		
		k = kmap(p);
		if(waserror()) {
			kunmap(k);
			putpage(p);
			nexterror();
		}
		memmove(buf, (uchar*)VA(k) + offset, l);
		kunmap(k);
		putpage(p);
		poperror();

		tot += l;
		buf += l;
		len -= l;

		offset += l;
		offset &= (BY2PG-1);
		if(offset != 0)
			break;

		pn++;
	}

Prefetch:
	if(len > 0){
		if(m->rah.vers != m->qid.vers){
			mntrahinit(&m->rah);
			m->rah.vers = m->qid.vers;
		}
		off += tot;
		tot += mntrahread(&m->rah, c, buf, len, off);
	}
	cacheunlock(m);
	poperror();

	return tot;
}

/* invalidate pages in page bitmap */
static void
invalidate(Mntcache *m, ulong offset, int len)
{
	ulong pn;

	for(pn = offset/BY2PG; len > 0; pn++, len -= BY2PG)
		m->bitmap[pn/MAPBITS] &= ~(1 << (pn%MAPBITS));
}

/* replace buf data from [off, off+len) in the cache or invalidate */
static void
cachedata(Mntcache *m, uchar *buf, int len, vlong off)
{
	int l;
	Page *p;
	KMap *k;
	ulong offset, pn, po, pe;

	if(off >= MAXCACHE || len <= 0){
		cacheunlock(m);
		return;
	}

	offset = off;
	if(offset+len > MAXCACHE)
		len = MAXCACHE - offset;
	pn = offset / BY2PG;
	offset &= (BY2PG-1);

	while(len > 0){
		l = BY2PG - offset;
		if(l > len)
			l = len;
		p = cpage(m, pn, &po, &pe);
		if(p != nil){
			if(offset > pe || (offset+l) < po){
				/* cached range not extendable, set new cached range */
				po = offset;
				pe = offset+l;
			} else {
				/* extend cached range */
				if(offset < po)
					po = offset;
				if((offset+l) > pe)
					pe = offset+l;
			}
		} else {
			if(needpages(nil)){
				invalidate(m, offset + pn*BY2PG, len);
				break;
			}
			p = newpage(0, nil, pn*BY2PG);
			p->daddr = cacheaddr(m, pn);
			cachedel(&fscache, p->daddr);
			cachepage(p, &fscache);
			m->bitmap[pn/MAPBITS] |= 1 << (pn%MAPBITS);

			po = offset;
			pe = offset+l;
		}
		cpageset(p, po, pe);

		k = kmap(p);
		if(waserror()) {
			kunmap(k);
			putpage(p);
			invalidate(m, offset + pn*BY2PG, len);
			cacheunlock(m);
			nexterror();
		}
		memmove((uchar*)VA(k) + offset, buf, l);
		poperror();
		kunmap(k);
		putpage(p);

		offset = 0;
		pn++;
		buf += l;
		len -= l;
	}
	cacheunlock(m);
}

void
cupdate(Chan *c, uchar *buf, int len, vlong off)
{
	Mntcache *m;

	m = ccache(c);
	if(m == nil)
		return;
	cachedata(m, buf, len, off);
}

void
cwrite(Chan* c, uchar *buf, int len, vlong off)
{
	Mntcache *m;

	m = ccache(c);
	if(m == nil)
		return;
	m->qid.vers++;
	c->qid.vers++;
	if(c->qid.type&QTAPPEND){
		cacheunlock(m);
		return;
	}
	cachedata(m, buf, len, off);
}

void
ctrunc(Chan *c)
{
	Mntcache *m;

	if(c->qid.type&QTDIR)
		return;

	if((c->flag&COPEN) == 0){
		lock(&cache);
		c->mcp = clookup(c, 0);
		unlock(&cache);
	}

	m = ccache(c);
	if(m == nil)
		return;
	mntrahinit(&m->rah);
	cnodata(m);
	cacheunlock(m);

	if((c->flag&COPEN) == 0)
		c->mcp = nil;
}

void
cclunk(Chan *c)
{
	Mntcache *m;

	m = ccache(c);
	if(m == nil)
		return;
	mntrahinit(&m->rah);
	cacheunlock(m);
	c->mcp = nil;
}
