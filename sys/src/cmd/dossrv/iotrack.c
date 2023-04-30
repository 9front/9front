#include <u.h>
#include <libc.h>
#include "iotrack.h"
#include "dat.h"
#include "fns.h"

static MLock	freelock;
static Iosect *	freelist;

#define	HIOB		31	/* a prime */
#define	NIOBUF		80

static Iotrack	hiob[HIOB+1];		/* hash buckets + lru list */
static Iotrack	iobuf[NIOBUF];		/* the real ones */

#define	UNLINK(p, nx, pr)	((p)->pr->nx = (p)->nx, (p)->nx->pr = (p)->pr)

#define	LINK(h, p, nx, pr)	((p)->nx = (h)->nx, (p)->pr = (h), \
				 (h)->nx->pr = (p), (h)->nx = (p))

#define	HTOFRONT(h, p)	((h)->hnext != (p) && (UNLINK(p,hnext,hprev), LINK(h,p,hnext,hprev)))

#define	TOFRONT(h, p)	((h)->next  != (p) && (UNLINK(p, next, prev), LINK(h,p, next, prev)))

static Iotrack *getiotrack(Xfs*, vlong);
static Iosect *getiosect(Xfs*, vlong, int);
static void purgetrack(Iotrack*);

Iosect *
getsect(Xfs *xf, vlong addr)
{
	return getiosect(xf, addr, 1);
}

Iosect *
getosect(Xfs *xf, vlong addr)
{
	return getiosect(xf, addr, 0);
}

static Iosect *
getiosect(Xfs *xf, vlong addr, int rflag)
{
	vlong taddr;
	int toff;
	Iotrack *t;
	Iosect *p;

	if(addr < 0)
		return nil;
	toff = addr % xf->sect2trk;
	taddr = addr - toff;
	t = getiotrack(xf, taddr);
	if(t == nil)
		return nil;
	if(rflag && (t->flags&BSTALE)){
		if(tread(t) < 0){
			unmlock(&t->lock);
			return nil;
		}
		t->flags &= ~BSTALE;
	}
	p = t->tp[toff];
	if(p == nil){
		mlock(&freelock);
		p = freelist;
		if(p != nil)
			freelist = p->next;
		else {
			p = malloc(sizeof(Iosect));
			if(p == nil) {
				unmlock(&freelock);
				unmlock(&t->lock);
				return nil;
			}
		}
		p->next = nil;
		unmlock(&freelock);

		p->flags = t->flags&BSTALE;
		p->lock.key = 0;
		p->iobuf = ((uchar*)&t->tp[xf->sect2trk]) + toff*xf->sectsize;
		p->t = t;

		t->tp[toff] = p;
	}
	t->ref++;
	unmlock(&t->lock);
	mlock(&p->lock);
	return p;
}

void
putsect(Iosect *p)
{
	Iotrack *t;

	if(canmlock(&p->lock))
		panic("putsect");
	t = p->t;
	mlock(&t->lock);
	t->flags |= p->flags;
	p->flags = 0;
	t->ref--;
	if(t->flags & BIMM){
		if(t->flags & BMOD)
			twrite(t);
		t->flags &= ~(BMOD|BIMM);
	}
	unmlock(&t->lock);
	unmlock(&p->lock);
}

static Iotrack *
getiotrack(Xfs *xf, vlong addr)
{
	Iotrack *hp, *p;
	Iotrack *mp = &hiob[HIOB];
	long h;
/*
 *	chat("iotrack %d,%d...", dev, addr);
 */
	h = (xf->dev<<24) ^ (long)addr;
	if(h < 0)
		h = ~h;
	h %= HIOB;
	hp = &hiob[h];

loop:

/*
 * look for it in the active list
 */
	mlock(&hp->lock);
	for(p=hp->hnext; p != hp; p=p->hnext){
		if(p->addr != addr || p->xf != xf)
			continue;
		unmlock(&hp->lock);
		mlock(&p->lock);
		if(p->addr == addr && p->xf == xf)
			goto out;
		unmlock(&p->lock);
		goto loop;
	}
	unmlock(&hp->lock);
/*
 * not found
 * take oldest unref'd entry
 */
	mlock(&mp->lock);
	for(p=mp->prev; p != mp; p=p->prev)
		if(p->ref == 0 && canmlock(&p->lock)){
			if(p->ref == 0)
				break;
			unmlock(&p->lock);
		}
	unmlock(&mp->lock);
	if(p == mp){
		fprint(2, "iotrack all ref'd\n");
		goto loop;
	}
	if(p->flags & BMOD){
		twrite(p);
		p->flags &= ~(BMOD|BIMM);
		unmlock(&p->lock);
		goto loop;
	}
	purgetrack(p);
	p->tp = malloc(xf->sect2trk*sizeof(Iosect*) + xf->sect2trk*xf->sectsize);
	if(p->tp == nil){
		unmlock(&p->lock);
		return nil;
	}
	memset(p->tp, 0, xf->sect2trk*sizeof(Iosect*));
	p->xf = xf;
	p->addr = addr;
	p->flags = BSTALE;
out:
	mlock(&hp->lock);
	HTOFRONT(hp, p);
	unmlock(&hp->lock);
	mlock(&mp->lock);
	TOFRONT(mp, p);
	unmlock(&mp->lock);
	return p;
}

static void
purgetrack(Iotrack *t)
{
	if(t->tp != nil){
		Xfs *xf = t->xf;
		int i, ref = xf->sect2trk;

		for(i=0; i<xf->sect2trk; i++){
			Iosect *p = t->tp[i];
			if(p == nil){
				--ref;
				continue;
			}
			if(canmlock(&p->lock)){
				t->tp[i] = nil;

				mlock(&freelock);
				p->next = freelist;
				freelist = p;
				unmlock(&freelock);
				--ref;
			}
		}
		if(t->ref != ref)
			panic("purgetrack");
		if(ref != 0)
			return;

		free(t->tp);
	}
	assert(t->ref == 0);
	t->tp = nil;
	t->xf = nil;
	t->addr = -1;
	t->flags = 0;
}

int
twrite(Iotrack *t)
{
	Xfs *xf = t->xf;

	chat("[twrite %lld+%lld...", t->addr, xf->offset);
	if(t->flags & BSTALE){
		int i, ref = 0;
		for(i=0; i<xf->sect2trk; i++)
			if(t->tp[i] != nil)
				++ref;
		if(ref < xf->sect2trk){
			if(tread(t) < 0){
				chat("error]");
				return -1;
			}
		}else
			t->flags &= ~BSTALE;
	}
	if(devwrite(xf, t->addr, (uchar*)&t->tp[xf->sect2trk], xf->sect2trk*xf->sectsize) < 0){
		chat("error]");
		return -1;
	}
	chat(" done]");
	return 0;
}

int
tread(Iotrack *t)
{
	Xfs *xf = t->xf;
	int i, ref = 0;

	chat("[tread %lld+%lld...", t->addr, xf->offset);
	for(i=0; i<xf->sect2trk; i++)
		if(t->tp[i] != nil)
			++ref;
	if(ref == 0){
		if(devread(xf, t->addr, (uchar*)&t->tp[xf->sect2trk], xf->sect2trk*xf->sectsize) < 0){
			chat("error]");
			return -1;
		}
	} else {
		for(i=0; i<xf->sect2trk; i++){
			if(t->tp[i] != nil)
				continue;
			if(devread(xf, t->addr + i, ((uchar*)&t->tp[xf->sect2trk]) + i*xf->sectsize, xf->sectsize) < 0){
				chat("error]");
				return -1;
			}
			chat("%d ", i);
		}
	}
	t->flags &= ~BSTALE;
	chat("done]");
	return 0;
}

void
purgebuf(Xfs *xf)
{
	Iotrack *p;

	for(p=&iobuf[0]; p<&iobuf[NIOBUF]; p++){
		if(p->xf != xf)
			continue;
		mlock(&p->lock);
		if(p->xf == xf){
			if(p->flags & BMOD)
				twrite(p);
			p->flags = BSTALE;
			purgetrack(p);
		}
		unmlock(&p->lock);
	}
}

void
sync(void)
{
	Iotrack *p;

	for(p=&iobuf[0]; p<&iobuf[NIOBUF]; p++){
		if(!(p->flags & BMOD))
			continue;
		mlock(&p->lock);
		if(p->flags & BMOD){
			twrite(p);
			p->flags &= ~(BMOD|BIMM);
		}
		unmlock(&p->lock);
	}
}

void
iotrack_init(void)
{
	Iotrack *mp, *p;

	for (mp=&hiob[0]; mp<&hiob[HIOB]; mp++)
		mp->hprev = mp->hnext = mp;
	mp->prev = mp->next = mp;

	for (p=&iobuf[0]; p<&iobuf[NIOBUF]; p++) {
		p->hprev = p->hnext = p;
		p->prev = p->next = p;
		TOFRONT(mp, p);
		purgetrack(p);
	}
}
