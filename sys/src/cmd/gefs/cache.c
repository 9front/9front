#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static void
lrudel(Blk *b)
{
	if(b == fs->chead)
		fs->chead = b->cnext;
	if(b == fs->ctail)
		fs->ctail = b->cprev;
	if(b->cnext != nil)
		b->cnext->cprev = b->cprev;
	if(b->cprev != nil)
		b->cprev->cnext = b->cnext;
	b->cnext = nil;
	b->cprev = nil;		
}

void
lrutop(Blk *b)
{
	qlock(&fs->lrulk);
	/*
	 * Someone got in first and did a
	 * cache lookup; we no longer want
	 * to put this into the LRU, because
	 * its now in use.
	 */
	bassert(b, b->magic == Magic);
	bassert(b, checkflag(b, 0, Bstatic));
	if(agetl(&b->ref) != 0){
		qunlock(&fs->lrulk);
		return;
	}
	lrudel(b);
	if(fs->chead != nil)
		fs->chead->cprev = b;
	if(fs->ctail == nil)
		fs->ctail = b;
	b->cnext = fs->chead;
	fs->chead = b;
	rwakeup(&fs->lrurz);
	qunlock(&fs->lrulk);
}

void
lrubot(Blk *b)
{
	qlock(&fs->lrulk);
	/*
	 * Someone got in first and did a
	 * cache lookup; we no longer want
	 * to put this into the LRU, because
	 * its now in use.
	 */
	bassert(b, b->magic == Magic);
	bassert(b, checkflag(b, 0, Bstatic));
	if(agetl(&b->ref) != 0){
		qunlock(&fs->lrulk);
		return;
	}
	lrudel(b);
	if(fs->ctail != nil)
		fs->ctail->cnext = b;
	if(fs->chead == nil)
		fs->chead = b;
	b->cprev = fs->ctail;
	fs->ctail = b;
	rwakeup(&fs->lrurz);
	qunlock(&fs->lrulk);
}

void
cacheins(Blk *b)
{
	u32int h;

	bassert(b, b->magic == Magic);
	h = ihash(b->bp.addr) % fs->cmax;
	qlock(&fs->lrulk);
	traceb("cache", b->bp);
	bassert(b, checkflag(b, 0, Bstatic|Bcached));
	setflag(b, Bcached, 0);
	bassert(b, b->hnext == nil);
	b->cached = getcallerpc(&b);
	b->hnext = fs->bcache[h];
	fs->bcache[h] = b;
	qunlock(&fs->lrulk);
}

static void
cachedel_lk(vlong addr)
{
	Blk *b, **p;
	u32int h;

	if(addr == -1)
		return;

	Bptr bp = {addr, -1, -1};
	tracex("uncache", bp, -1, getcallerpc(&addr));
	h = ihash(addr) % fs->cmax;
	p = &fs->bcache[h];
	for(b = *p; b != nil; b = b->hnext){
		if(b->bp.addr == addr){
			/* FIXME: Until we clean up snap.c, we can have dirty blocks in cache */
			bassert(b, checkflag(b, Bcached, Bstatic)); //Bdirty));
			*p = b->hnext;
			b->uncached = getcallerpc(&addr);
			b->hnext = nil;
			setflag(b, 0, Bcached);
			break;
		}
		p = &b->hnext;
	}
}
void
cachedel(vlong addr)
{
	qlock(&fs->lrulk);
	Bptr bp = {addr, -1, -1};
	tracex("uncachelk", bp, -1, getcallerpc(&addr));
	cachedel_lk(addr);
	qunlock(&fs->lrulk);
}

Blk*
cacheget(vlong addr)
{
	u32int h;
	Blk *b;

	h = ihash(addr) % fs->cmax;
	qlock(&fs->lrulk);
	for(b = fs->bcache[h]; b != nil; b = b->hnext){
		if(b->bp.addr == addr){
			holdblk(b);
			lrudel(b);
			b->lasthold = getcallerpc(&addr);
			break;
		}
	}
	qunlock(&fs->lrulk);

	return b;
}

/*
 * Pulls the block from the bottom of the LRU for reuse.
 */
Blk*
cachepluck(void)
{
	Blk *b;

	qlock(&fs->lrulk);
	while(fs->ctail == nil)
		rsleep(&fs->lrurz);

	b = fs->ctail;
	bassert(b, b->magic == Magic);
	bassert(b, agetl(&b->ref) == 0);
	if(checkflag(b, Bcached, 0))
		cachedel_lk(b->bp.addr);
	bassert(b, checkflag(b, 0, Bcached));
	lrudel(b);
	aswapl(&b->flag, 0);
	b->lasthold = 0;
	b->lastdrop = 0;
	b->freed = 0;
	b->hnext = nil;
	qunlock(&fs->lrulk);

	return  holdblk(b);
}
