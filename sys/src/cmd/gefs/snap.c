#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static void
dlflush(Dlist *dl)
{
	char kvbuf[512];
	Msg m;

	if(dl->ins == nil)
		return;
	traceb("dlflush", dl->ins->bp);
	dl->hd = dl->ins->bp;
	if(dl->tl.addr == dl->hd.addr)
		dl->tl = dl->hd;
	enqueue(dl->ins);
	dropblk(dl->ins);
	dl->ins = nil;
	/* special case: the snap dlist has gen -1, skip it */
	if(dl->gen != -1){
		m.op = Oinsert;
		dlist2kv(dl, &m, kvbuf, sizeof(kvbuf));
		btupsert(&fs->snap, &m, 1);
	}
}

static void
dlcachedel(Dlist *dl, int hdel)
{
	uint h;
	Dlist *d, **p;

	h = ihash(dl->gen) ^ ihash(dl->bgen);
	if(hdel){
		p = &fs->dlcache[h % fs->dlcmax];
		for(d = *p; d != nil; d = d->chain){
			if(d->gen == dl->gen && d->bgen == dl->bgen)
				break;
			p = &d->chain;
		}
		if(d != nil)
			*p  = d->chain;
	}
	if(dl == fs->dlhead)
		fs->dlhead = dl->cnext;
	if(dl == fs->dltail)
		fs->dltail = dl->cprev;
	if(dl->cnext != nil)
		dl->cnext->cprev = dl->cprev;
	if(dl->cprev != nil)
		dl->cprev->cnext = dl->cnext;
	dl->cnext = nil;
	dl->cprev = nil;
}

static Dlist*
dlcacheget(vlong gen, vlong bgen)
{
	Dlist *dl;
	uint h;

	h = ihash(gen) ^ ihash(bgen);
	for(dl = fs->dlcache[h % fs->dlcmax]; dl != nil; dl = dl->chain)
		if(dl->gen == gen && dl->bgen == bgen)
			break;
	if(dl != nil)
		dlcachedel(dl, 0);
	return dl;
}

static Dlist*
getdl(vlong gen, vlong bgen)
{
	char kbuf[Dlksz], kvbuf[Dlkvpsz];
	Dlist *dl, **p;
	uint h;
	Msg m;
	Kvp kv;
	Key k;

	if((dl = dlcacheget(gen, bgen)) != nil)
		return dl;
	dl = emalloc(sizeof(Dlist), 1);
	fs->dlcount++;
	if(waserror()){
		fs->dlcount--;
		free(dl);
		nexterror();
	}
	kbuf[0] = Kdlist;
	PACK64(kbuf+1, gen);
	PACK64(kbuf+9, bgen);
	k.k = kbuf;
	k.nk = sizeof(kbuf);

	/* load up existing dlist */
	if(btlookup(&fs->snap, &k, &kv, kvbuf, sizeof(kvbuf))){
		kv2dlist(&kv, dl);
		goto Found;
	}

	/* create a new one if it didn't exist */
	dl->gen = gen;
	dl->bgen = bgen;
	dl->hd.addr = -1;
	dl->tl.addr = -1;
	dl->ins = nil;

	m.op = Oinsert;
	dlist2kv(dl, &m, kvbuf, sizeof(kvbuf));
	btupsert(&fs->snap, &m, 1);
Found:
	poperror();
	h = ihash(gen) ^ ihash(bgen);
	p = &fs->dlcache[h % fs->dlcmax];
	dl->chain = *p;
	*p = dl;
	return dl;
}

void
putdl(Dlist *dl)
{
	Dlist *dt;

	if(dl->gen == -1)
		return;
	dlcachedel(dl, 0);
	while(fs->dlcount >= fs->dlcmax && (dt = fs->dltail) != nil){
		fs->dlcount--;
		dlcachedel(dt, 1);
		dlflush(dt);
		assert(dt->ins == nil);
		free(dt);
	}

	dl->cprev = nil;
	dl->cnext = fs->dlhead;
	if(fs->dltail == nil)
		fs->dltail = dl;
	if(fs->dlhead != nil)
		fs->dlhead->cprev = dl;
	fs->dlhead = dl;
}

void
freedl(Dlist *dl, int docontents)
{
	char buf[Kvmax];
	Arena *a;
	Qent qe;
	Bptr bp;
	Msg m;
	Blk *b;
	char *p;

	bp = dl->hd;
	if(dl->gen != -1){
		m.op = Odelete;
		dlist2kv(dl, &m, buf, sizeof(buf));
		btupsert(&fs->snap, &m, 1);
	}
	while(bp.addr != -1){
		b = getblk(bp, 0);
		/*
		 * Because these deadlists are dead-dead at this point,
		 * they'll never be read from again; we can avoid worrying
		 * about deferred reclamation, and queue them up to be freed
		 * directly, which means we don't need to worry about watiing
		 * for a quiescent state, and the associated out-of-block
		 * deadlocks that come with it.
		 */
		if(docontents){
			for(p = b->data; p != b->data+b->logsz; p += 8){
				qe.op = Qfree;
				qe.bp.addr = UNPACK64(p);
				qe.bp.hash = -1;
				qe.bp.gen = -1;
				qe.b = nil;
				a = getarena(qe.bp.addr);
				qput(a->sync, qe);
				traceb("dlclear", qe.bp);
			}
		}
		bp = b->logp;
		qe.op = Qfree;
		qe.bp = b->bp;
		qe.b = nil;
		a = getarena(qe.bp.addr);
		qput(a->sync, qe);
		traceb("dlfreeb", qe.bp);
		dropblk(b);
	}
}

static void
mergedl(vlong merge, vlong gen, vlong bgen)
{
	char buf[2][Kvmax];
	Dlist *d, *m;
	Msg msg[2];
	Blk *b;

	d = nil;
	m = nil;
	if(waserror()){
		putdl(m);
		putdl(d);
		nexterror();
	}
	d = getdl(merge, bgen);
	m = getdl(gen, bgen);
	assert(d != m);
	/*
	 * If the dest dlist didn't exist,
	 * just move the merge dlist over
	 * and be done with it, otherwise
	 * chain onto the existing dlist
	 * tail.
	 */
	if(m->hd.addr != -1){
		if(d->hd.addr == -1){
			assert(d->ins == nil);
			d->hd = m->hd;
			d->tl = m->tl;
			d->ins = m->ins;
			m->ins = nil;
		}else{
			if(m->ins != nil){
				enqueue(m->ins);
				dropblk(m->ins);
				m->ins = nil;
			}
			b = getblk(d->tl, 0);
			b->logp = m->hd;
			d->tl = m->tl;
			assert(d->hd.addr != m->hd.addr);
			finalize(b);
			syncblk(b);
			dropblk(b);
		}
	}
	msg[0].op = Odelete;
	dlist2kv(m, &msg[0], buf[0], sizeof(buf[0]));
	msg[1].op = Oinsert;
	dlist2kv(d, &msg[1], buf[1], sizeof(buf[1]));
	btupsert(&fs->snap, msg, 2);
	putdl(m);
	putdl(d);
	poperror();
}

static void
reclaimblocks(vlong gen, vlong succ, vlong prev)
{
	char pfx[9];
	Dlist dl;
	Scan s;

	pfx[0] = Kdlist;
	PACK64(pfx+1, gen);
	btnewscan(&s, pfx, sizeof(pfx));
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		kv2dlist(&s.kv, &dl);
		if(waserror()){
			btexit(&s);
			nexterror();
		}
		if(succ != -1 && dl.bgen <= prev)
			mergedl(succ, dl.gen, dl.bgen);
		else if(dl.bgen <= prev)
			mergedl(prev, dl.gen, dl.bgen);
		else
			freedl(&dl, 1);
		poperror();
	}
	btexit(&s);
	if(succ != -1){
		pfx[0] = Kdlist;
		PACK64(pfx+1, succ);
		btnewscan(&s, pfx, sizeof(pfx));
		btenter(&fs->snap, &s);
		while(1){
			if(!btnext(&s, &s.kv))
				break;
			kv2dlist(&s.kv, &dl);
			if(dl.bgen <= prev)
				continue;
			if(waserror()){
				btexit(&s);
				nexterror();
			}
			freedl(&dl, 1);
			poperror();
		}
		btexit(&s);
	}
}

/*
 * Removes a label from a snapshot, allowing
 * it to be reclaimed if it is not a direct
 * predecessor of more than one other snapshot.
 *
 * If it has one successor and no label, then
 * it will be merged with that successor.
 *
 * Must be called with mntlock held for r
 */
void
delsnap(Tree *t, vlong succ, char *name)
{
	char *p, buf[4][Kvmax];
	int nm, deltree;
	Mount *mnt;
	Tree *r;
	Msg m[4];

	nm = 0;
	deltree = 0;
	if(name != nil){
		if(strcmp(name, "dump") == 0
		|| strcmp(name, "empty") == 0
		|| strcmp(name, "adm") == 0)
			error(Ename);

		m[nm].op = Odelete;
		m[nm].k = buf[nm];
		p = packlbl(buf[nm], sizeof(buf[nm]), name);
		m[nm].nk = p - m[nm].k;
		m[nm].v = nil;
		m[nm].nv = 0;
		t->nlbl--;
		nm++;
	}
 
	if(t->nlbl == 0 && t->nref <= 1){
		deltree = 1;
		m[nm].op = Orelink;
		retag2kv(t->pred, succ, 0, 0, &m[nm], buf[nm], sizeof(buf[nm]));
		nm++;
		if(t->succ != -1){
			m[nm].op = Oreprev;
			retag2kv(t->succ, t->pred, 0, 0, &m[nm], buf[nm], sizeof(buf[nm]));
			nm++;
		}
		m[nm].op = Odelete;
		m[nm].k = buf[nm];
		p = packsnap(buf[nm], sizeof(buf[nm]), t->gen);
		m[nm].nk = p - m[nm].k;
		m[nm].v = nil;
		m[nm].nv = 0;
		nm++;
	}else{
		m[nm].op = Oinsert;
		tree2kv(t, &m[nm], buf[nm], sizeof(buf[nm]));
		nm++;
	}
	assert(nm <= nelem(m));
	dlsync();
	btupsert(&fs->snap, m, nm);
	if(deltree){
		reclaimblocks(t->gen, succ, t->pred);
		for(mnt = agetp(&fs->mounts); mnt != nil; mnt = mnt->next){
			r = agetp(&mnt->root);
			if(r->gen == t->succ)
				r->pred = t->pred;
			if(r->gen == t->pred)
				r->succ = t->succ;
		}
	}
}

/*
 * Attaches a label to a tree, incrementing
 * its reference count. This labelled snapshot
 * will show up in the dump.
 */
void
tagsnap(Tree *t, char *name, int flg)
{
	char buf[3][Kvmax];
	Msg m[3];
	Tree *n;
	int i;

	if(strcmp(name, "dump") == 0
	|| strcmp(name, "empty") == 0
	|| strcmp(name, "adm") == 0)
		error(Ename);

	i = 0;
	n = nil;
	if(flg & Lmut){
		n = emalloc(sizeof(Tree), 1);
		if(waserror()){
			free(n);
			nexterror();
		}
		aswapl(&n->memref, 1);
		n->dirty = 0;
		n->nlbl = 1;
		n->nref = 0;
		n->ht = t->ht;
		n->bp = t->bp;
		n->succ = -1;
		/*
		 * Because we can have blocks in-flight with gen==memgen,
		 * which both sides of the fork can free, we need to make
		 * sure that we don't deadlist them in the new snapshot.
		 *
		 * As a result, we need to use memgen, and not gen, in
		 * order to prevent the potential for a double free.
		 */
		n->pred = t->gen;
		n->base = t->memgen;
		n->gen = fs->nextgen++;
		n->memgen = fs->nextgen++;

		t->nref++;
		m[i].op = Orelink;
		retag2kv(t->gen, t->succ, 0, 1, &m[i], buf[i], sizeof(buf[i]));
		i++;
		m[i].op = Oinsert;
		lbl2kv(name, n->gen, flg, &m[i], buf[i], sizeof(buf[i]));
		i++;
		m[i].op = Oinsert;
		tree2kv(n, &m[i], buf[i], sizeof(buf[i]));
		i++;
		poperror();
	}else{
		t->nlbl++;

		m[i].op = Orelink;
		retag2kv(t->gen, t->succ, 1, 0, &m[i], buf[i], sizeof(buf[i]));
		i++;

		t->pred = t->gen;
		m[i].op = Oinsert;
		lbl2kv(name, t->gen, flg, &m[i], buf[i], sizeof(buf[i]));
		i++;
	}
	btupsert(&fs->snap, m, i);
	free(n);
}

/*
 * Updates a snapshot; keeps the generation the same if possible,
 * otherwise moves to a new generation. A snapshot may only stay
 * at the same generation as long as it is at the tip of a snapshot
 * list; once it's observable by a derived snapshot it must be
 * immutable.
 */
Tree*
updatesnap(Tree *o, char *lbl, int flg)
{
	char buf[4][Kvmax];
	Msg m[4];
	Tree *t;
	int i;

	if(!o->dirty)
		return o;

	tracex("updatesnap", o->bp, o->memgen, getcallerpc(&o));
	/* update the old kvp */
	o->nlbl--;
	o->nref++;

	/* create the new one */

	t = emalloc(sizeof(Tree), 1);
	if(waserror()){
		free(t);
		nexterror();
	}
	aswapl(&t->memref, 1);
	t->dirty = 0;

	t->nlbl = 1;
	t->nref = 0;
	t->ht = o->ht;
	t->bp = o->bp;
	t->succ = -1;
	t->base = o->base;
	t->gen = o->memgen;
	t->memgen = fs->nextgen++;

	i = 0;
	m[i].op = Orelink;
	if(o->nlbl == 0 && o->nref == 1){
		t->pred = o->pred;
		retag2kv(t->pred, t->gen, 0, 0, &m[i], buf[i], sizeof(buf[i]));
	}else{
		t->pred = o->gen;
		retag2kv(t->pred, t->gen, -1, 1, &m[i], buf[i], sizeof(buf[i]));
	}
	i++;

	m[i].op = Oinsert;
	tree2kv(t, &m[i], buf[i], sizeof(buf[i]));
	i++;
	m[i].op = Oinsert;
	lbl2kv(lbl, t->gen, flg, &m[i], buf[i], sizeof(buf[i]));
	i++;
	btupsert(&fs->snap, m, i);

	/* only update the dirty status after we sync */
	o->dirty = 0;

	/* this was the last ref to the snap */
	if(o->nlbl == 0 && o->nref == 1)
		delsnap(o, t->gen, nil);
	closesnap(o);
	poperror();
	return t;
}

/*
 * open snapshot by label, returning a tree.
 */
Tree*
opensnap(char *label, int *flg)
{
	char *p, buf[Kvmax];
	Tree *t;
	vlong gen;
	Kvp kv;
	Key k;

	/* Klabel{"name"} => Ksnap{id} */
	if((p = packlbl(buf, sizeof(buf), label)) == nil)
		return nil;
	k.k = buf;
	k.nk = p - buf;
	if(!btlookup(&fs->snap, &k, &kv, buf, sizeof(buf)))
		return nil;
	assert(kv.nv == 1+8+4);
	gen = UNPACK64(kv.v + 1);
	if(flg != nil)
		*flg = UNPACK32(kv.v + 1+8);

	t = mallocz(sizeof(Tree), 1);
	if(waserror()){
		free(t);
		nexterror();
	}
	p = packsnap(buf, sizeof(buf), gen);
	k.k = buf;
	k.nk = p - buf;
	if(!btlookup(&fs->snap, &k, &kv, buf, sizeof(buf)))
		broke(Efs);
	unpacktree(t, kv.v, kv.nv);
	aswapl(&t->memref, 1);
	t->memgen = fs->nextgen++;
	poperror();
	return t;
}

/*
 * close snapshot, flushing and freeing in-memory
 * representation.
 */
void
closesnap(Tree *t)
{
	if(t == nil || aincl(&t->memref, -1) != 0)
		return;
	limbo(DFtree, t);
}

void
dlsync(void)
{
	Dlist *dl, *n;

	tracem("dlsync");
	dlflush(&fs->snapdl);
	for(dl = fs->dlhead; dl != nil; dl = n){
		n = dl->cnext;
		dlflush(dl);
	}
}

/*
 * Marks a block as killed by the tree
 * t, which means that it will be free
 * for use after t is reclaimed.
 *
 * t must be an active snapshot with
 * no successors.
 */
void
killblk(Tree *t, Bptr bp)
{
	Dlist *dl;
	Blk *b;
	char *p;

	/* 
	 * When we have a forked snap, blocks allocated before the fork
	 * are the responsibility of the other chain; in this chain, we
	 * leak it and let the last reference in the other chain clean up
	 */
	if(t == &fs->snap)
		dl = &fs->snapdl;
	else if(bp.gen > t->base)
		dl = getdl(t->memgen, bp.gen);
	else
		return;
	if(waserror()){
		putdl(dl);
		nexterror();
	}
	if(dl->ins == nil || Logspc - dl->ins->logsz < Logslop){
		b = newblk(&fs->snap, Tdlist);
		if(dl->ins != nil){
			enqueue(dl->ins);
			dropblk(dl->ins);
		}
		if(dl->tl.addr == -1)
			dl->tl = b->bp;
		b->logp = dl->hd;
		dl->hd = b->bp;
		dl->ins = b;
		cacheins(b);
	}
	p = dl->ins->data + dl->ins->logsz;
	dl->ins->logsz += 8;
	setflag(dl->ins, Bdirty, 0);
	PACK64(p, bp.addr);
	poperror();
	putdl(dl);
}
