#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

/* Terminated so we can use them directly in C */
char*
unpackstr(char *p, char *e, char **s)
{
	int n;

	assert(e - p >= 3);
	n = UNPACK16(p);
	if(e - p < n + 3 || p[n+2] != 0)
		broke(Efs);
	*s = p+2;
	return p+3+n;
}

/* Terminated so we can use them directly in C */
char*
packstr(char *p, char *e, char *s)
{
	int n;

	n = strlen(s);
	assert(e - p >= n+3);
	PACK16(p, n);		p += 2;
	memmove(p, s, n);	p += n;
	*p = 0;			p += 1;
	return p;
}
		
void
dir2kv(vlong up, Xdir *d, Kvp *kv, char *buf, int nbuf)
{
	char *ek, *ev, *eb;

	ek = packdkey(buf, nbuf, up, d->name);
	kv->k = buf;
	kv->nk = ek - buf;
	eb = buf + nbuf;
	ev = packdval(ek, eb - ek, d);
	kv->v = ek;
	kv->nv = ev - ek;
}

char*
packdkey(char *p, int sz, vlong up, char *name)
{
	char *ep;

	ep = p + sz;
	PACK8(p, Kent);	p += 1;
	PACK64(p, up);	p += 8;
	if(name != nil)
		p = packstr(p, ep, name);
	return p;
}

char*
unpackdkey(char *p, int sz, vlong *up)
{
	char key, *ep, *name;

	ep = p + sz;
	assert(sz > 9);
	key = UNPACK8(p);	p += 1;
	*up = UNPACK64(p);	p += 8;
	assert(key == Kent);
	p = unpackstr(p, ep, &name);
	assert(p <= ep);
	return name;
}

char*
packsuper(char *p, int sz, vlong up)
{
	char *ep;

	ep = p+sz;
	PACK8(p, Kup);	p += 1;
	PACK64(p, up);	p += 8;
	assert(p <= ep);
	return p;
}

char*
packdval(char *p, int sz, Xdir *d)
{
	char *e;

	e = p + sz;
	PACK64(p, d->flag);	p += 8;
	PACK64(p, d->qid.path);	p += 8;
	PACK32(p, d->qid.vers);	p += 4;
	PACK8(p, d->qid.type);	p += 1;
	PACK32(p, d->mode);	p += 4;
	PACK64(p, d->atime);	p += 8;
	PACK64(p, d->mtime);	p += 8;
	PACK64(p, d->length);	p += 8;
	PACK32(p, d->uid);	p += 4;
	PACK32(p, d->gid);	p += 4;
	PACK32(p, d->muid);	p += 4;
	assert(p <= e);
	return p;
}

void
kv2dir(Kvp *kv, Xdir *d)
{
	char *v;

	memset(d, 0, sizeof(Xdir));
	if(kv->nk < 9 || kv->nv != Xdirsz)
		broke(Efs);

	unpackstr(kv->k + 9, kv->k + kv->nk, &d->name);

	v = kv->v;
	d->flag 	= UNPACK64(v);	v += 8;
	d->qid.path	= UNPACK64(v);	v += 8;
	d->qid.vers	= UNPACK32(v);	v += 4;
	d->qid.type	= UNPACK8(v);	v += 1;
	d->mode		= UNPACK32(v);	v += 4;
	d->atime	= UNPACK64(v);	v += 8;
	d->mtime	= UNPACK64(v);	v += 8;
	d->length	= UNPACK64(v);	v += 8;
	d->uid		= UNPACK32(v);	v += 4;
	d->gid		= UNPACK32(v);	v += 4;
	d->muid		= UNPACK32(v);	v += 4;
}

int
dir2statbuf(Xdir *d, char *buf, int nbuf)
{
	int sz, nn, nu, ng, nm;
	vlong atime, mtime;
	User *u, *g, *m;
	char *p;

	rlock(&fs->userlk);
	if((u = uid2user(d->uid)) == nil)
		u = uid2user(noneid);
	if((g = uid2user(d->gid)) == nil)
		g = uid2user(nogroupid);
	if((m = uid2user(d->muid)) == nil)
		m = uid2user(noneid);
	if(u == nil || g == nil || m == nil){
		runlock(&fs->userlk);
		error(Eperm);
	}

	p = buf;
	nn = strlen(d->name);
	nu = strlen(u->name);
	ng = strlen(g->name);
	nm = strlen(m->name);
	atime = (d->atime+Nsec/2)/Nsec;
	mtime = (d->mtime+Nsec/2)/Nsec;
	sz = STATFIXLEN + nn + nu + ng + nm;
	if(sz > nbuf){
		runlock(&fs->userlk);
		return -1;
	}
	
	PBIT16(p, sz-2);		p += 2;
	PBIT16(p, -1 /*type*/);		p += 2;
	PBIT32(p, -1 /*dev*/);		p += 4;
	PBIT8(p, d->qid.type);		p += 1;
	PBIT32(p, d->qid.vers);		p += 4;
	PBIT64(p, d->qid.path);		p += 8;
	PBIT32(p, d->mode);		p += 4;
	PBIT32(p, atime);		p += 4;
	PBIT32(p, mtime);		p += 4;
	PBIT64(p, d->length);		p += 8;

	PBIT16(p, nn);			p += 2;
	memcpy(p, d->name, nn);		p += nn;
	PBIT16(p, nu);			p += 2;
	memcpy(p, u->name, nu);		p += nu;
	PBIT16(p, ng);			p += 2;
	memcpy(p, g->name, ng);		p += ng;
	PBIT16(p, nm);			p += 2;
	memcpy(p, m->name, nm);		p += nm;
	assert(p - buf == sz);
	runlock(&fs->userlk);
	return sz;
}

int
kv2statbuf(Kvp *kv, char *buf, int nbuf)
{
	Xdir d;

	kv2dir(kv, &d);
	return dir2statbuf(&d, buf, nbuf);
}

void
kv2qid(Kvp *kv, Qid *q)
{
	char *v, *e;

	v = kv->v;
	e = v + kv->nv;
	q->path = UNPACK64(v);	v += 8;
	q->vers = UNPACK64(v);	v += 8;
	assert(v <= e);
}

void
kv2dlist(Kvp *kv, Dlist *dl)
{
	char *p, *e;

	p = kv->k;
	e = p + kv->nk;
	p++;
	dl->gen = UNPACK64(p);	p += 8;
	dl->bgen = UNPACK64(p);	p += 8;
	assert(p <= e);
	
	p = kv->v;
	e = p + kv->nv;
	dl->hd = unpackbp(p, e-p);	p += Ptrsz;
	dl->tl = unpackbp(p, e-p);	p += Ptrsz;
	assert(p <= e);
}

void
dlist2kv(Dlist *dl, Kvp *kv, char *buf, int nbuf)
{
	char *p, *e;

	assert(nbuf >= Dlkvpsz);
	p = buf;
	e = buf+nbuf;

	kv->k = p;
	*p++ = Kdlist;
	PACK64(p, dl->gen);	p += 8;
	PACK64(p, dl->bgen);	p += 8;
	kv->nk = (p - kv->k);
	
	kv->v = p;
	p = packbp(p, e-p, &dl->hd);
	p = packbp(p, e-p, &dl->tl);
	kv->nv = (p - kv->v);
}

void
tree2kv(Tree *t, Kvp *kv, char *buf, int nbuf)
{
	char *p, *e;

	p = buf;
	e = buf+nbuf;

	kv->k = p;
	if((p = packsnap(p, e-p, t->gen)) == nil)
		abort();
	kv->nk = p - kv->k;

	kv->v = p;
	if((p = packtree(p, e-p, t)) == nil)
		abort();
	kv->nv = p - kv->v;
}

void
retag2kv(vlong gen, vlong link, int dlbl, int dref, Kvp *kv, char *buf, int nbuf)
{
	char *p;

	assert(nbuf >= 8+1+1);
	kv->k = buf;
	if((p = packsnap(buf, nbuf, gen)) == nil)
		abort();
	kv->nk = p - buf;

	kv->v = p;
	PACK64(p, link);	p += 8;
	*p = dlbl;		p += 1;
	*p = dref;		p += 1;
	kv->nv = p - kv->v;
}

void
lbl2kv(char *lbl, vlong gen, uint flg, Kvp *kv, char *buf, int nbuf)
{
	char *p;
	int n;

	n = strlen(lbl);
	assert(nbuf >= 1+n + 1+8+4);

	p = buf;
	kv->k = p;
	p[0] = Klabel;		p += 1;
	memcpy(p, lbl, n);	p += n;
	kv->nk = p - kv->k;

	kv->v = p;
	p[0] = Ksnap;		p += 1;
	PACK64(p, gen);		p += 8;
	PACK32(p, flg);		p += 4;
	kv->nv = p - kv->v;
}

char*
packlbl(char *p, int sz, char *name)
{
	int n;

	n = strlen(name);
	assert(sz >= n+1);
	p[0] = Klabel;		p += 1;
	memcpy(p, name, n);	p += n;
	return p;
}

char*
packsnap(char *p, int sz, vlong id)
{
	assert(sz >= Snapsz);
	p[0] = Ksnap;		p += 1;
	PACK64(p, id);		p += 8;
	return p;
}

char*
packbp(char *p, int sz, Bptr *bp)
{
	assert(sz >= Ptrsz);
	PACK64(p, bp->addr);	p += 8;
	PACK64(p, bp->hash);	p += 8;
	PACK64(p, bp->gen);	p += 8;
	return p;
}

Bptr
unpackbp(char *p, int sz)
{
	Bptr bp;

	assert(sz >= Ptrsz);
	bp.addr = UNPACK64(p);	p += 8;
	bp.hash = UNPACK64(p);	p += 8;
	bp.gen = UNPACK64(p);
	return bp;
}

Tree*
unpacktree(Tree *t, char *p, int sz)
{
	assert(sz >= Treesz);
	memset(t, 0, sizeof(Tree));
	t->nref = UNPACK32(p);		p += 4;
	t->nlbl = UNPACK32(p);		p += 4;
	t->ht = UNPACK32(p);		p += 4;
	t->flag = UNPACK32(p);		p += 4;
	t->gen = UNPACK64(p);		p += 8;
	t->pred = UNPACK64(p);		p += 8;
	t->succ = UNPACK64(p);		p += 8;
	t->base = UNPACK64(p);		p += 8;
	t->bp.addr = UNPACK64(p);	p += 8;
	t->bp.hash = UNPACK64(p);	p += 8;
	t->bp.gen = UNPACK64(p);	//p += 8;

	return t;
}

char*
packtree(char *p, int sz, Tree *t)
{
	assert(sz >= Treesz);
	PACK32(p, t->nref);	p += 4;
	PACK32(p, t->nlbl);	p += 4;
	PACK32(p, t->ht);	p += 4;
	PACK32(p, t->flag);	p += 4;
	PACK64(p, t->gen);	p += 8;
	PACK64(p, t->pred);	p += 8;
	PACK64(p, t->succ);	p += 8;
	PACK64(p, t->base);	p += 8;
	PACK64(p, t->bp.addr);	p += 8;
	PACK64(p, t->bp.hash);	p += 8;
	PACK64(p, t->bp.gen);	p += 8;
	return p;
}

char*
packarena(char *p, int sz, Arena *a)
{
	char *e;

	assert(sz >= Arenasz);
	e = p + Arenasz;
	PACK64(p, a->loghd.addr);	p += 8;	/* freelist addr */
	PACK64(p, a->loghd.hash);	p += 8;	/* freelist hash */
	PACK64(p, a->size);		p += 8;	/* arena size */
	PACK64(p, a->used);		p += 8;	/* arena used */
	assert(p <= e);
	return p;
}

char*
unpackarena(Arena *a, char *p, int sz)
{
	char *e;

	assert(sz >= Arenasz);
	memset(a, 0, sizeof(*a));

	e = p + Arenasz;
	a->loghd.addr = UNPACK64(p);	p += 8;
	a->loghd.hash = UNPACK64(p);	p += 8;
	a->loghd.gen = -1;		p += 0;
	a->size = UNPACK64(p);		p += 8;
	a->used = UNPACK64(p);		p += 8;
	a->logtl = nil;

	assert(p <= e);
	return p;
}

char*
packsb(char *p0, int sz, Gefs *fi)
{
	vlong nextqid, nextgen, qgen;
	uvlong h;
	char *p;
	int i;

	assert(sz == Blksz);
	assert(fi->narena < 512);
	p = p0;
	nextqid = fi->nextqid;
	nextgen = fi->nextgen;
	qgen = agetv(&fi->qgen);
	memcpy(p, "gefs9.00", 8);	p += 8;
	PACK32(p, Blksz);		p += 4;
	PACK32(p, Bufspc);		p += 4;
	PACK32(p, fi->narena);		p += 4;
	PACK32(p, fi->snap.ht);		p += 4;
	PACK64(p, fi->snap.bp.addr);	p += 8;
	PACK64(p, fi->snap.bp.hash);	p += 8;
	PACK64(p, fi->snapdl.hd.addr);	p += 8;
	PACK64(p, fi->snapdl.hd.hash);	p += 8;
	PACK64(p, fi->snapdl.tl.addr);	p += 8;
	PACK64(p, fi->snapdl.tl.hash);	p += 8;
	PACK64(p, fi->flag);		p += 8;
	PACK64(p, nextqid);		p += 8;
	PACK64(p, nextgen);		p += 8;
	PACK64(p, qgen);		p += 8;
	for(i = 0; i < fi->narena; i++){
		PACK64(p, fi->arenabp[i].addr);	p += 8;
		PACK64(p, fi->arenabp[i].hash);	p += 8;
	}
	h = bufhash(p0, p - p0);
	PACK64(p, h);			p += 8;
	return p;
}

char*
unpacksb(Gefs *fi, char *p0, int sz)
{
	vlong qgen;
	uvlong dh, xh;
	char *p;
	int i;

	assert(sz == Blksz);
	p = p0;
	if(memcmp(p, "gefs9.00", 8) != 0)
		error("%s %.8s", Efsvers, p);
	p += 8;
	fi->blksz = UNPACK32(p);		p += 4;
	fi->bufspc = UNPACK32(p);		p += 4;
	fi->narena = UNPACK32(p);		p += 4;
	fi->snap.ht = UNPACK32(p);		p += 4;
	fi->snap.bp.addr = UNPACK64(p);		p += 8;
	fi->snap.bp.hash = UNPACK64(p);		p += 8;
	fi->snap.bp.gen = -1;			p += 0;
	fi->snapdl.hd.addr = UNPACK64(p);	p += 8;
	fi->snapdl.hd.hash = UNPACK64(p);	p += 8;
	fi->snapdl.hd.gen = -1;			p += 0;
	fi->snapdl.gen = -1;			p += 0;
	fi->snapdl.tl.addr = UNPACK64(p);	p += 8;
	fi->snapdl.tl.hash = UNPACK64(p);	p += 8;
	fi->snapdl.tl.gen = -1;			p += 0;
	fi->snapdl.gen = -1;			p += 0;
	fi->flag = UNPACK64(p);			p += 8;
	fi->nextqid = UNPACK64(p);		p += 8;
	fi->nextgen = UNPACK64(p);		p += 8;
	qgen = UNPACK64(p);			p += 8;
	aswapv(&fs->qgen, qgen);
	fi->arenabp = emalloc(fi->narena * sizeof(Bptr), 0);
	for(i = 0; i < fi->narena; i++){
		fi->arenabp[i].addr = UNPACK64(p);	p += 8;
		fi->arenabp[i].hash = UNPACK64(p);	p += 8;
		fi->arenabp[i].gen = -1;
	}
	xh = bufhash(p0, p - p0);
	dh = UNPACK64(p);			p += 8;
	if(dh != xh)
		error("corrupt superblock: %llx != %llx", dh, xh);
	assert(fi->narena < 256);	/* should be more than anyone needs */
	return p;
}
