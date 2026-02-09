#include <u.h>
#include <libc.h>
#include <bio.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static vlong	blkalloc_lk(Arena*, int);
static vlong	blkalloc(int, uint, int);
static void	blkdealloc_lk(Arena*, vlong);
static Blk*	initblk(Blk*, vlong, vlong, int);
static void	readblk(Blk*, Bptr, int);

int
checkflag(Blk *b, int set, int clr)
{
	long v;

	v = agetl(&b->flag);
	return (v & (set|clr)) == set;
}

void
setflag(Blk *b, int set, int clr)
{
	long ov, nv;

	while(1){
		ov = agetl(&b->flag);
		nv = (ov & ~clr) | set;
		if(acasl(&b->flag, ov, nv))
			break;
	}
}

void
syncblk(Blk *b)
{
	bassert(b, checkflag(b, Bfinal, 0));
	bassert(b, b->bp.addr >= 0);
	tracex("syncblk", b->bp, b->type, -1);
	if(pwrite(fs->fd, b->buf, Blksz, b->bp.addr) == -1)
		broke("%B %s: %r", b->bp, Eio);
	setflag(b, 0, Bdirty);
}

static void
readblk(Blk *b, Bptr bp, int flg)
{
	vlong off, xh, ck, rem, n;
	char *p;

	off = bp.addr;
	rem = Blksz;
	while(rem != 0){
		n = pread(fs->fd, b->buf, rem, off);
		if(n <= 0)
			error("%s: %r", Eio);
		off += n;
		rem -= n;
	}
	b->cnext = nil;
	b->cprev = nil;
	b->hnext = nil;

	b->bp.addr = bp.addr;
	b->bp.hash = -1;
	b->bp.gen = -1;

	b->nval = 0;
	b->valsz = 0;
	b->nbuf = 0;
	b->bufsz = 0;
	b->logsz = 0;

	p = b->buf + 2;
	b->type = (flg&GBraw) ? Tdat : UNPACK16(b->buf+0);
	switch(b->type){
	default:
		error("invalid block type %d @%llx", b->type, bp);
		break;
	case Tdat:
	case Tsuper:
		b->data = b->buf;
		break;
	case Tarena:
		b->data = p;
		break;
	case Tdlist:
	case Tlog:
		b->logsz = UNPACK16(p);		p += 2;
		b->logh = UNPACK64(p);		p += 8;
		b->logp = unpackbp(p, Ptrsz);	p += Ptrsz;
		bassert(b, p - b->buf == Loghdsz);
		b->data = p;
		break;
	case Tpivot:
		b->nval = UNPACK16(p);		p += 2;
		b->valsz = UNPACK16(p);		p += 2;
		b->nbuf = UNPACK16(p);		p += 2;
		b->bufsz = UNPACK16(p);		p += 2;
		bassert(b, p - b->buf == Pivhdsz);
		b->data = p;
		break;
	case Tleaf:
		b->nval = UNPACK16(p);		p += 2;
		b->valsz = UNPACK16(p);		p += 2;
		bassert(b, p - b->buf == Leafhdsz);
		b->data = p;
		break;
	}
	if(b->type == Tlog || b->type == Tdlist){
		xh = b->logh;
		ck = bufhash(b->data, b->logsz);
	}else{
		xh = bp.hash;
		ck = blkhash(b);
	}
	if((flg&GBnochk) == 0 && ck != xh)
		error("%s: %ullx %llux != %llux", Ecorrupt, bp.addr, xh, ck);
	bassert(b, b->magic == Magic);
}

static Arena*
pickarena(uint ty, uint hint, int tries)
{
	uint n, r;

	r = aincl(&fs->roundrobin, 1)/2048;
	if(ty == Tdat)
		n = hint % (fs->narena - 1) + r + 1;
	else
		n = r;
	return &fs->arenas[(n + tries) % fs->narena];
}

Arena*
getarena(vlong b)
{
	int hi, lo, mid;
	vlong alo, ahi;
	Arena *a;

	lo = 0;
	hi = fs->narena;
	if(b == fs->sb0->bp.addr)
		return &fs->arenas[0];
	if(b == fs->sb1->bp.addr)
		return &fs->arenas[hi-1];
	while(1){
		mid = (hi + lo)/2;
		a = &fs->arenas[mid];
		alo = a->h0->bp.addr;
		ahi = alo + a->size + 2*Blksz;
		if(b < alo)
			hi = mid-1;
		else if(b > ahi)
			lo = mid+1;
		else
			return a;
	}
}


static void
freerange(Avltree *t, vlong off, vlong len)
{
	Arange *r, *s;

	assert(len % Blksz == 0);
	if((r = calloc(1, sizeof(Arange))) == nil)
		error(Enomem);
	r->off = off;
	r->len = len;
	assert(avllookup(t, r, 0) == nil);
	avlinsert(t, r);

Again:
	s = (Arange*)avlprev(r);
	if(s != nil){
		if(s->off+s->len == r->off){
			avldelete(t, r);
			s->len = s->len + r->len;
			free(r);
			r = s;
			goto Again;
		}
		assert(s->off+s->len < r->off);
	}
	s = (Arange*)avlnext(r);
	if(s != nil){
		if(r->off+r->len == s->off){
			avldelete(t, r);
			s->off = r->off;
			s->len = s->len + r->len;
			free(r);
			r = s;
			goto Again;
		}
		assert(r->off+r->len < s->off);
	}
}

static void
grabrange(Avltree *t, vlong off, vlong len)
{
	Arange *r, *s, q;
	vlong l;

	assert(len % Blksz == 0);
	q.off = off;
	q.len = len;
	r = (Arange*)avllookup(t, &q.Avl, -1);
	if(r == nil || off + len > r->off + r->len)
		abort();

	if(off == r->off){
		r->off += len;
		r->len -= len;
	}else if(off + len == r->off + r->len){
		r->len -= len;
	}else if(off > r->off && off+len < r->off + r->len){
		s = emalloc(sizeof(Arange), 0);
		l = r->len;
		s->off = off + len;
		r->len = off - r->off;
		s->len = l - r->len - len;
		avlinsert(t, s);
	}else
		abort();

	if(r->len == 0){
		avldelete(t, r);
		free(r);
	}
}

static Blk*
mklogblk(Arena *a, vlong o)
{
	Blk *lb;

	lb = a->logbuf[0];
	if(lb == a->logtl)
		lb = a->logbuf[1];
	bassert(lb, agetl(&lb->ref) == 1);
	aswapl(&lb->flag, Bstatic);
	initblk(lb, o, -1, Tlog);
	traceb("logblk" , lb->bp);
	lb->lasthold0 = lb->lasthold;
	lb = holdblk(lb);
	lb->lasthold = getcallerpc(&a);
	return lb;
}

/*
 * Logs an allocation. Must be called
 * with arena lock held. Duplicates some
 * of the work in allocblk to prevent
 * recursion.
 */
static void
logappend(Arena *a, vlong off, vlong len, int op)
{
	vlong o, start, end;
	Blk *lb;
	char *p;

	assert((off & 0xff) == 0);
	assert(op == LogAlloc || op == LogFree || op == LogSync);
	if(op != LogSync){
		start = a->h0->bp.addr;
		end = start + a->size + 2*Blksz;
		assert(off >= start);
		assert(off < end);
	}
	lb = a->logtl;
	bassert(lb, agetl(&lb->ref) > 0);
	bassert(lb, lb->type == Tlog);
	bassert(lb, lb->logsz >= 0);

	if(checkflag(lb, 0, Bdirty))
		setflag(lb, Bdirty, Bfinal);

	/*
	 * move to the next block when we have
	 * too little room in the log:
	 * We're appending up to 16 bytes as
	 * part of the operation, followed by
	 * 16 bytes of new log entry allocation
	 * and chaining.
	 */
	if(lb->logsz >= Logspc - Logslop){
		o = blkalloc_lk(a, 0);
		if(o == -1)
			error(Efull);
		p = lb->data + lb->logsz;
		PACK64(p, o|LogAlloc1);
		lb->logsz += 8;
		lb->logp = (Bptr){o, -1, -1};
		lb = mklogblk(a, o);
	}
	if(len == Blksz){
		if(op == LogAlloc)
			op = LogAlloc1;
		else if(op == LogFree)
			op = LogFree1;
	}
	off |= op;
	p = lb->data + lb->logsz;
	PACK64(p, off);
	lb->logsz += 8;
	if(op >= Log2wide){
		PACK64(p+8, len);
		lb->logsz += 8;
	}
	if(lb != a->logtl) {
		finalize(lb);
		syncblk(lb);

		finalize(a->logtl);
		syncblk(a->logtl);
		dropblk(a->logtl);
		a->logtl = lb;
		a->nlog++;
	}
}

void
loadlog(Arena *a, Bptr bp)
{
	vlong ent, off, len, gen;
	int op, i, n;
	char *d;
	Blk *b;


	dprint("loadlog %B\n", bp);
	traceb("loadlog", bp);
	b = a->logbuf[0];
	while(1){
		bassert(b, checkflag(b, Bstatic, Bcached));
		holdblk(b);
		readblk(b, bp, 0);
		dprint("\tload %B chain %B\n", bp, b->logp);
		a->nlog++;
		for(i = 0; i < b->logsz; i += n){
			d = b->data + i;
			ent = UNPACK64(d);
			op = ent & 0xff;
			off = ent & ~0xff;
			n = (op >= Log2wide) ? 16 : 8;
			switch(op){
			case LogSync:
				gen = ent >> 8;
				dprint("\tlog@%x: sync %lld\n", i, gen);
				if(gen >= agetv(&fs->qgen)){
					if(a->logtl == nil){
						b->logp = Zb;
						b->logsz = i;
						a->logtl = b;
						cachedel(b->bp.addr);
						setflag(b, Bdirty, 0);
						return;
					}
					dropblk(b);
					return;
				}
				break;
	
			case LogAlloc:
			case LogAlloc1:
				len = (op >= Log2wide) ? UNPACK64(d+8) : Blksz;
				dprint("\tlog@%x alloc: %llx+%llx\n", i, off, len);
				grabrange(a->free, off & ~0xff, len);
				a->used += len;
				break;
			case LogFree:
			case LogFree1:
				len = (op >= Log2wide) ? UNPACK64(d+8) : Blksz;
				dprint("\tlog@%x free: %llx+%llx\n", i, off, len);
				freerange(a->free, off & ~0xff, len);
				a->used -= len;
				break;
			default:
				dprint("\tlog@%x: log op %d\n", i, op);
				abort();
				break;
			}
		}
		if(b->logp.addr == -1){
			a->logtl = b;
			return;
		}
		bp = b->logp;
		dropblk(b);
	}
}

void
flushlog(Arena *a)
{
	if(checkflag(a->logtl, 0, Bdirty|Bstatic))
		return;
	finalize(a->logtl);
	syncblk(a->logtl);
}

void
compresslog(Arena *a)
{
	int i, nr, nblks, nlog;
	vlong sz, *blks;
	Blk *b;
	Arange *r;
	char *p;

	flushlog(a);
	/*
	 * Prepare what we're writing back.
	 * Arenas must be sized so that we can
	 * keep the merged log in memory for
	 * a rewrite.
	 */
	sz = 0;
	nr = 0;
	nlog = 0;
	for(r = (Arange*)avlmin(a->free); r != nil; r = (Arange*)avlnext(r)){
		sz += 16;
		nr++;
	}

	/*
	 * Make a pessimistic estimate of the number of blocks
	 * needed to store the ranges, as well as the blocks
	 * used to store the range allocations.
	 *
	 * This does modify the tree, but it's safe because
	 * we can only be removing entries from the tree, not
	 * splitting or inserting new ones.
	 */
	nblks = (sz+Logspc)/(Logspc - Logslop) + 16*nr/(Logspc-Logslop) + 1;
	if((blks = calloc(nblks, sizeof(vlong))) == nil)
		error(Enomem);
	if(waserror()){
		free(blks);
		nexterror();
	}
	for(i = 0; i < nblks; i++){
		blks[i] = blkalloc_lk(a, 1);
		if(blks[i] == -1)
			error(Efull);
	}

	/* fill up the log with the ranges from the tree */
	i = 0;
	b = mklogblk(a, blks[i++]);
	for(r = (Arange*)avlmin(a->free); r != nil; r = (Arange*)avlnext(r)){
		if(b->logsz >= Logspc - Logslop){
			b->logp = (Bptr){blks[i], -1, -1};
			finalize(b);
			syncblk(b);
			dropblk(b);
			nlog++;
			b = mklogblk(a, blks[i++]);
		}
		p = b->data + b->logsz;
		PACK64(p+0, r->off|LogFree);
		PACK64(p+8, r->len);
		b->logsz += 16;
	}
	finalize(b);
	syncblk(b);

	/*
	 * now we have a valid freelist, and we can start
	 * appending stuff to it. Clean up the eagerly
	 * allocated extra blocks.
	 *
	 * Note that we need to drop the reference to the
	 * old logtl before we free the old blocks, because
	 * deallocating a block may require another block.
	 */
	dropblk(a->logtl);
	a->loghd = (Bptr){blks[0], -1, -1};
	a->logtl = b;	/* written back by sync() later */
	a->nlog = nlog;
	a->lastlogsz = nlog;

	/* May add blocks to new log */
	for(; i < nblks; i++)
		blkdealloc_lk(a, blks[i]);
	poperror();
	free(blks);
}

int
logbarrier(Arena *a, vlong gen)
{
	logappend(a, gen<<8, 0, LogSync);
	return 0;
}

/*
 * Allocate from an arena, with lock
 * held. May be called multiple times
 * per operation, to alloc space for
 * the alloc log.
 */
static vlong
blkalloc_lk(Arena *a, int seq)
{
	Arange *r;
	vlong b;

	if(seq)
		r = (Arange*)avlmin(a->free);
	else
		r = (Arange*)avlmax(a->free);
	if(!usereserve && a->size - a->used <= a->reserve)
		return -1;
	if(r == nil)
		broke(Estuffed);

	/*
	 * A bit of sleight of hand here:
	 * while we're changing the sorting
	 * key, but we know it won't change
	 * the sort order because the tree
	 * covers disjoint ranges
	 */
	if(seq){
		b = r->off;
		r->len -= Blksz;
		r->off += Blksz;
	}else{
		r->len -= Blksz;
		b = r->off + r->len;
	}
	if(r->len == 0){
		avldelete(a->free, r);
		free(r);
	}
	a->used += Blksz;
	tracex("blkalloc" , Zb, b, getcallerpc(&a));
	return b;
}

static void
blkdealloc_lk(Arena *a, vlong b)
{
	cachedel(b);
	logappend(a, b, Blksz, LogFree);
	freerange(a->free, b, Blksz);
	a->used -= Blksz;
}

static vlong
blkalloc(int ty, uint hint, int seq)
{
	Arena *a;
	vlong b;
	int tries;

	tries = 0;
Again:
	a = pickarena(ty, hint, tries);
	/*
	 * Loop through the arena up to 2 times.
	 * The first pass tries to find an arena
	 * that has space and is not in use, the
	 * second waits until an arena is free.
	 */
	if(tries == 2*fs->narena)
		error(Efull);
	tries++;
	if(tries < fs->narena){
		if(canqlock(a) == 0)
			goto Again;
	}else
		qlock(a);
	if(waserror()){
		qunlock(a);
		nexterror();
	}
	b = blkalloc_lk(a, seq);
	if(b == -1){
		qunlock(a);
		poperror();
		goto Again;
	}
	logappend(a, b, Blksz, LogAlloc);
	qunlock(a);
	poperror();
	return b;
}

static Blk*
initblk(Blk *b, vlong bp, vlong gen, int ty)
{
	Blk *ob;

	b->type = ty;
	b->bp.addr = bp;
	b->bp.hash = -1;
	b->bp.gen = gen;
	ob = cacheget(bp);
	if(ob != nil)
		fatal("double alloc: %#p %B %#p %B", b, b->bp, ob, ob->bp);
	switch(ty){
	case Tdat:
		b->data = b->buf;
		break;
	case Tarena:
		b->data = b->buf+2;
		break;
	case Tdlist:
	case Tlog:
		b->logsz = 0;
		b->logp = (Bptr){-1, -1, -1};
		b->data = b->buf + Loghdsz;
		break;
	case Tpivot:
		b->data = b->buf + Pivhdsz;
		break;
	case Tleaf:
		b->data = b->buf + Leafhdsz;
		break;
	}
	setflag(b, Bdirty, 0);
	b->nval = 0;
	b->valsz = 0;
	b->nbuf = 0;
	b->bufsz = 0;
	b->logsz = 0;
	b->alloced = getcallerpc(&b);

	return b;
}

Blk*
newdblk(Tree *t, vlong hint, int seq)
{
	vlong bp;
	Blk *b;

	bp = blkalloc(Tdat, hint, seq);
	b = cachepluck();
	initblk(b, bp, t->memgen, Tdat);
	b->alloced = getcallerpc(&t);
	return b;

}

Blk*
newblk(Tree *t, int ty)
{
	vlong bp;
	Blk *b;

	bp = blkalloc(ty, 0, 0);
	b = cachepluck();
	initblk(b, bp, t->memgen, ty);
	b->alloced = getcallerpc(&t);
	return b;
}

Blk*
dupblk(Tree *t, Blk *b)
{
	Blk *r;

	if((r = newblk(t, b->type)) == nil)
		return nil;

	tracex("dup" , b->bp, b->type, t->gen);
	r->bp.hash = -1;
	r->nval = b->nval;
	r->valsz = b->valsz;
	r->nbuf = b->nbuf;
	r->bufsz = b->bufsz;
	r->logsz = b->logsz;
	r->alloced = getcallerpc(&t);
	memcpy(r->buf, b->buf, sizeof(r->buf));
	return r;
}

void
finalize(Blk *b)
{
	if(b->type != Tdat)
		PACK16(b->buf, b->type);

	switch(b->type){
	default:
		abort();
		break;
	case Tpivot:
		PACK16(b->buf+2, b->nval);
		PACK16(b->buf+4, b->valsz);
		PACK16(b->buf+6, b->nbuf);
		PACK16(b->buf+8, b->bufsz);
		break;
	case Tleaf:
		PACK16(b->buf+2, b->nval);
		PACK16(b->buf+4, b->valsz);
		break;
	case Tdlist:
	case Tlog:
		b->logh = bufhash(b->data, b->logsz);
		PACK16(b->buf+2, b->logsz);
		PACK64(b->buf+4, b->logh);
		packbp(b->buf+12, Ptrsz, &b->logp);
		break;
	case Tdat:
	case Tarena:
	case Tsuper:
		break;
	}

	b->bp.hash = blkhash(b);
	setflag(b, Bdirty|Bfinal, 0);
}

Blk*
getblk(Bptr bp, int flg)
{
	Blk *b;
	int i;

	i = ihash(bp.addr) % nelem(fs->blklk);
	qlock(&fs->blklk[i]);
	if(waserror()){
		qunlock(&fs->blklk[i]);
		if(flg&GBtry)
			return nil;
		else
			aincl(&fs->rdonly, 1);
		nexterror();
	}
	if((b = cacheget(bp.addr)) != nil){
		bassert(b, checkflag(b, 0, Bfreed));
		bassert(b, b->bp.gen == bp.gen);
	} else {
		b = cachepluck();
		b->alloced = getcallerpc(&bp);
		readblk(b, bp, flg);
		b->bp.gen = bp.gen;
		cacheins(b);
	}
	b->lasthold = getcallerpc(&bp);
	qunlock(&fs->blklk[i]);
	poperror();

	return b;
}


Blk*
holdblk(Blk *b)
{
	aincl(&b->ref, 1);
	b->lasthold = getcallerpc(&b);
	return b;
}

void
dropblk(Blk *b)
{
	if(b == nil)
		return;
	b->lastdrop = getcallerpc(&b);
	if(aincl(&b->ref, -1) != 0)
		return;
	/*
	 * freed blocks go to the LRU bottom
	 * for early reuse.
	 */
	if(checkflag(b, Bfreed, 0))
		lrubot(b);
	else
		lrutop(b);
}

ushort
blkfill(Blk *b)
{
	switch(b->type){
	case Tpivot:
		return 2*b->nbuf + b->bufsz +  2*b->nval + b->valsz;
	case Tleaf:
		return 2*b->nval + b->valsz;
	default:
		fprint(2, "invalid block @%lld\n", b->bp.addr);
		abort();
	}
}

void
limbo(int op, Limbo *l)
{
	Limbo *p;
	ulong ge;

	l->op = op;
	while(1){
		ge = agetl(&fs->epoch);
		p = agetp(&fs->limbo[ge]);
		l->next = p;
		if(acasp(&fs->limbo[ge], p, l)){
			aincl(&fs->nlimbo, 1);
			break;
		}
	}
}

void
freeblk(Tree *t, Blk *b)
{
	if(t == &fs->snap || (t != nil && b->bp.gen < t->memgen)){
		tracex("killb", b->bp, getcallerpc(&t), -1);
		killblk(t, b->bp);
		return;
	}
	b->freed = getcallerpc(&t);
	tracex("freeb", b->bp, getcallerpc(&t), -1);
	setflag(b, Blimbo, 0);
	holdblk(b);
	bassert(b, agetl(&b->ref) > 1);
	limbo(DFblk, b);
}

void
freebp(Tree *t, Bptr bp)
{
	Bfree *f;

	if(t == &fs->snap || (t != nil && bp.gen < t->memgen)){
		tracex("killb", bp, getcallerpc(&t), -1);
		killblk(t, bp);
		return;
	}
	tracex("freeb", bp, getcallerpc(&t), -1);

	qlock(&fs->bfreelk);
	while(fs->bfree == nil)
		rsleep(&fs->bfreerz);
	f = fs->bfree;
	fs->bfree = (Bfree*)f->next;
	qunlock(&fs->bfreelk);

	f->bp = bp;
	limbo(DFbp, f);
}

void
epochstart(int tid)
{
	ulong ge;

	assert(tid >= 0);
	ge = agetl(&fs->epoch);
	aswapl(&fs->lepoch[tid], ge | Eactive);
}

void
epochend(int tid)
{
	ulong le;

	assert(tid >= 0);
	le = agetl(&fs->lepoch[tid]);
	aswapl(&fs->lepoch[tid], le &~ Eactive);
}

void
epochwait(void)
{
	int i, delay;
	ulong e, ge;

	delay = 0;
Again:
	ge = agetl(&fs->epoch);
	for(i = 0; i < agetl(&fs->nworker); i++){
		e = agetl(&fs->lepoch[i]);
		if((e & Eactive) && e != (ge | Eactive)){
			if(delay == 300)
				fprint(2, "stalled epoch %lx [worker %d]\n", e, i);
			sleep(delay++);
			goto Again;
		}
	}
}

void
epochclean(void)
{
	ulong c, e, ge;
	Limbo *p, *n;
	Blk *b;
	Bfree *f;
	Arena *a;
	Qent qe;
	int i;

	c = agetl(&fs->nlimbo);
	ge = agetl(&fs->epoch);
	for(i = 0; i < agetl(&fs->nworker); i++){
		e = agetl(&fs->lepoch[i]);
		if((e & Eactive) && e != (ge | Eactive)){
			if(c < fs->cmax/4)
				return;
			epochwait();
		}
	}
	epochwait();
	p = aswapp(&fs->limbo[(ge+1)%3], nil);
	aswapl(&fs->epoch, (ge+1)%3);

	for(; p != nil; p = n){
		n = p->next;
		switch(p->op){
		case DFtree:
			free(p);
			break;
		case DFmnt:
			free(p);
			break;
		case DFbp:
			f = (Bfree*)p;
			a = getarena(f->bp.addr);
			if((b = cacheget(f->bp.addr)) != nil){
				setflag(b, Bfreed, Bdirty|Blimbo);
				dropblk(b);
			}
			qe.op = Qfree;
			qe.bp = f->bp;
			qe.b = nil;
			qput(a->sync, qe);
			qlock(&fs->bfreelk);
			f->next = fs->bfree;
			fs->bfree = f;
			rwakeup(&fs->bfreerz);
			qunlock(&fs->bfreelk);
			break;
		case DFblk:
			b = (Blk*)p;
			qe.op = Qfree;
			qe.bp = b->bp;
			qe.b = nil;
			setflag(b, Bfreed, Bdirty|Blimbo);
			a = getarena(b->bp.addr);
			dropblk(b);
			qput(a->sync, qe);
			break;
		default:
			abort();
		}
		aincl(&fs->nlimbo, -1);
	}
}

void
enqueue(Blk *b)
{
	Arena *a;
	Qent qe;

	bassert(b, checkflag(b, Bdirty, Bqueued|Bstatic));
	bassert(b, b->bp.addr >= 0);
	finalize(b);
	if(checkflag(b, 0, Bcached)){
		cacheins(b);
		b->cached = getcallerpc(&b);
	}
	holdblk(b);

	b->enqueued = getcallerpc(&b);
	traceb("queueb", b->bp);
	a = getarena(b->bp.addr);
	qe.op = Qwrite;
	qe.bp = b->bp;
	qe.b = b;
	qput(a->sync, qe);
}

void
qinit(Syncq *q)
{
	q->fullrz.l = &q->lk;
	q->emptyrz.l = &q->lk;
	q->nheap = 0;
	q->heapsz = fs->cmax;
	q->heap = emalloc(q->heapsz*sizeof(Qent), 1);
}

static int
qcmp(Qent *a, Qent *b)
{
	if(a->qgen != b->qgen)
		return (a->qgen < b->qgen) ? -1 : 1;
	if(a->op != b->op)
		return (a->op < b->op) ? -1 : 1;
	if(a->bp.addr != b->bp.addr)
		return (a->bp.addr < b->bp.addr) ? -1 : 1;
	return 0;
}

void
qput(Syncq *q, Qent qe)
{
	int i;

	if(qe.op == Qfree || qe.op == Qwrite)
		assert((qe.bp.addr & (Blksz-1)) == 0);
	else if(qe.op == Qfence)
		assert(fs->syncing > 0);
	else
		abort();
	if(qe.b != nil)
		bassert(qe.b, agetl(&qe.b->ref) > 0);
	qlock(&q->lk);
	qe.qgen = agetv(&fs->qgen);
	while(q->nheap == q->heapsz)
		rsleep(&q->fullrz);
	for(i = q->nheap; i > 0; i = (i-1)/2){
		if(qcmp(&qe, &q->heap[(i-1)/2]) == 1)
			break;
		q->heap[i] = q->heap[(i-1)/2];
	}
	q->heap[i] = qe;
	q->nheap++;
	rwakeup(&q->emptyrz);
	qunlock(&q->lk);
}

static Qent
qpop(Syncq *q)
{
	int i, l, r, m;
	Qent e, t;

	qlock(&q->lk);
	while(q->nheap == 0)
		rsleep(&q->emptyrz);
	e = q->heap[0];
	if(--q->nheap == 0)
		goto Out;

	i = 0;
	q->heap[0] = q->heap[q->nheap];
	while(1){
		m = i;
		l = 2*i+1;
		r = 2*i+2;
		if(l < q->nheap && qcmp(&q->heap[m], &q->heap[l]) == 1)
			m = l;
		if(r < q->nheap && qcmp(&q->heap[m], &q->heap[r]) == 1)
			m = r;
		if(m == i)
			break;
		t = q->heap[m];
		q->heap[m] = q->heap[i];
		q->heap[i] = t;
		i = m;
	}
Out:
	rwakeup(&q->fullrz);
	qunlock(&q->lk);
	if(e.b != nil){
		setflag(e.b, 0, Bqueued);
		e.b->queued = 0;
	}
	return e;
}

void
runsync(int, void *p)
{
	Arena *a;
	Syncq *q;
	Qent qe;

	q = p;
	if(waserror()){
		aincl(&fs->rdonly, 1);
		fprint(2, "error syncing: %s\n", errmsg());
		return;
	}
	while(1){
		qe = qpop(q);
		switch(qe.op){
		case Qfree:
			tracex("qfreeb", qe.bp, qe.qgen, -1);
			/*
			 * we shouldn't have a block in a free op,
			 * the frees go into the queue just to ensure
			 * write/reuse ordering.
			 */
			assert(qe.b == nil);
			a = getarena(qe.bp.addr);
			qlock(a);
			blkdealloc_lk(a, qe.bp.addr);
			qunlock(a);
			break;
		case Qfence:
			tracev("qfence", qe.qgen);
			qlock(&fs->synclk);
			if(--fs->syncing == 0)
				rwakeupall(&fs->syncrz);
			qunlock(&fs->synclk);
			break;
		case Qwrite:
			tracex("qsyncb", qe.bp, qe.qgen, -1);
			if(checkflag(qe.b, Bfreed, Bstatic) == 0)
				syncblk(qe.b);
			dropblk(qe.b);
			break;
		default:
			abort();
		}
		assert(estacksz() == 1);
	}
}
