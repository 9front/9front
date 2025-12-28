#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

typedef struct Path	Path;

struct Path {
	/* Flowing down for flush */
	Msg	*ins;	/* inserted values, bounded by lo..hi */
	Blk	*b;	/* to shadow */
	int	idx;	/* insert at */
	int	lo;	/* key range */
	int	hi;	/* key range */
	int	sz;	/* size of range */

	/* Flowing up from flush */
	int	op;	/* change done along path */
	Blk	*nl;	/* new left */
	Blk	*nr;	/* new right, if we split or rotated */
	int	midx;	/* modification index */
	int	npull;	/* number of messages successfully pulled */
	int	pullsz;	/* size of pulled messages */
};

#define efreeblk(t, b) do { \
	if(b != nil) \
		freeblk(t, b); \
	} while(0)

static void
stablesort(Msg *m, int nm)
{
	int i, j;
	Msg t;

	for(i = 1; i < nm; i++){
		for(j = i; j > 0; j--){
			if(keycmp(&m[j-1], &m[j]) <= 0)
				break;
			t = m[j-1];
			m[j-1] = m[j];
			m[j] = t;
		}
	}
}

void
cpkey(Key *dst, Key *src, char *buf, int nbuf)
{
	assert(src->nk <= nbuf);
	memmove(buf, src->k, src->nk);
	dst->k = buf;
	dst->nk = src->nk;
}

void
cpkvp(Kvp *dst, Kvp *src, char *buf, int nbuf)
{
	assert(src->nk+src->nv <= nbuf);
	memmove(buf, src->k, src->nk);
	memmove(buf+ src->nk, src->v, src->nv);
	dst->k = buf;
	dst->nk = src->nk;
	dst->v = buf+src->nk;
	dst->nv = src->nv;
}

int
keycmp(Key *a, Key *b)
{
	int c, n;

	n = (a->nk < b->nk) ? a->nk : b->nk;
	if((c = memcmp(a->k, b->k, n)) != 0)
		return c < 0 ? -1 : 1;
	if(a->nk < b->nk)
		return -1;
	else if(a->nk > b->nk)
		return 1;
	else
		return 0;
}

static int
msgsz(Msg *m)
{
	/* disp + op + klen + key + vlen + v */
	return 2+1+2+m->nk +2+ m->nv;
}

static int
valsz(Kvp *kv)
{
	return 2 + 2+kv->nk + 2+kv->nv;
}

void
getval(Blk *b, int i, Kvp *kv)
{
	char *p;
	int o;

	bassert(b, i >= 0 && i < b->nval);
	p = b->data + 2*i;
	o = UNPACK16(p);	p = b->data + o;
	kv->nk = UNPACK16(p);	p += 2;
	kv->k = p;		p += kv->nk;
	kv->nv = UNPACK16(p);	p += 2;
	kv->v = p;
}

Bptr
getptr(Kvp *kv, int *fill)
{
	assert(kv->nv == Ptrsz || kv->nv == Ptrsz+2);
	*fill = UNPACK16(kv->v + Ptrsz);
	return unpackbp(kv->v, kv->nv);
}

/* Exported for reaming */
void
setval(Blk *b, Kvp *kv)
{
	int off, spc;
	char *p;

	spc = (b->type == Tleaf) ? Leafspc : Pivspc;
	b->valsz += 2 + kv->nk + 2 + kv->nv;
	off = spc - b->valsz;

	if(kv->k[0] == Kent) assert(kv->nv != 0);
	bassert(b, 2*(b->nval+1) + b->valsz <= spc);
	bassert(b, 2*(b->nval+1) <= off);

	p = b->data + 2*b->nval;
	PACK16(p, off);

	p = b->data + off;
	PACK16(p, kv->nk);		p += 2;
	memmove(p, kv->k, kv->nk);	p += kv->nk;
	PACK16(p, kv->nv);		p += 2;
	memmove(p, kv->v, kv->nv);

	b->nval++;
}

static void
setptr(Blk *b, Key *k, Bptr bp, int fill)
{
	char *p, buf[Ptrsz+2];
	Kvp kv;

	kv.k = k->k;
	kv.nk = k->nk;
	kv.v = buf;
	kv.nv = sizeof(buf);
	p = packbp(buf, sizeof(buf), &bp);
	PACK16(p, fill);
	setval(b, &kv);
}

static void
setmsg(Blk *b, Msg *m)
{
	char *p;
	int o;

	bassert(b, b->type == Tpivot);
	b->bufsz += msgsz(m)-2;

	p = b->data + Pivspc + 2*b->nbuf;
	o = Bufspc - b->bufsz;
	PACK16(p, o);

	p = b->data + Pivspc + o;
	*p = m->op;			p += 1;
	PACK16(p, m->nk);		p += 2;
	memmove(p, m->k, m->nk);	p += m->nk;
	PACK16(p, m->nv);		p += 2;
	memmove(p, m->v, m->nv);

	b->nbuf++;
}

void
getmsg(Blk *b, int i, Msg *m)
{
	char *p;
	int o;

	bassert(b, b->type == Tpivot);
	bassert(b, i >= 0 && i < b->nbuf);
	p = b->data + Pivspc + 2*i;
	o = UNPACK16(p);
	p = b->data + Pivspc + o;
	m->op = *p;		p += 1;
	m->nk = UNPACK16(p);	p += 2;
	m->k = p;		p += m->nk;
	m->nv = UNPACK16(p);	p += 2;
	m->v = p;
}

static int
bufsearch(Blk *b, Key *k, Msg *m, int *same)
{
	int lo, hi, ri, mid, r;
	Msg cmp;

	ri = -1;
	lo = 0;
	hi = b->nbuf-1;
	while(lo <= hi){
		mid = (hi + lo) / 2;
		getmsg(b, mid, &cmp);
		r = keycmp(k, &cmp);
		switch(r){
		case -1:
			hi = mid-1;
			break;
		case 0:
			ri = mid;
			hi = mid-1;
			break;
		case 1:
			lo = mid+1;
			break;
		}
	}
	/*
	 * we can have duplicate messages, and we
	 * want to point to the first of them:
	 * scan backwards.
	 */
	*same = 0;
	if(ri == -1)
		ri = lo-1;
	else
		*same = 1;
	if(m != nil && ri >= 0)
		getmsg(b, ri, m);
	return ri;
}

static int
blksearch(Blk *b, Key *k, Kvp *rp, int *same)
{
	int lo, hi, ri, mid, r;
	Kvp cmp;

	ri = -1;
	lo = 0;
	hi = b->nval-1;
	while(lo <= hi){
		mid = (hi + lo) / 2;
		getval(b, mid, &cmp);
		r = keycmp(k, &cmp);
		switch(r){
		case -1:
			hi = mid-1;
			break;
		case 0:
			ri = mid;
			hi = mid-1;
			break;
		case 1:
			lo = mid+1;
			break;
		}
	}
	*same = 0;
	if(ri == -1)
		ri = lo-1;
	else
		*same = 1;
	if(ri >= 0)
		getval(b, ri, rp);
	return ri;
}

static int
buffill(Blk *b)
{
	bassert(b, b->type == Tpivot);
	return 2*b->nbuf + b->bufsz;
}

static int
filledbuf(Blk *b, int nmsg, int needed)
{
	bassert(b, b->type == Tpivot);
	return 2*(b->nbuf+nmsg) + b->bufsz + needed > Bufspc;
}

static int
filledleaf(Blk *b, int needed)
{
	bassert(b, b->type == Tleaf);
	return 2*(b->nval+1) + b->valsz + needed > Leafspc;
}

static int
filledpiv(Blk *b, int reserve)
{
	/* 
	 * We need to guarantee there's room for one message
	 * at all times, so that splits along the whole path
	 * have somewhere to go as they propagate up. For
	 * each key-ptr pair we have 8 bytes of overhead,
	 * so remember to account for that: 2 bytes of fill,
	 * 2 bytes of offset table, and 2 bytes of length
	 * for both key and value.
	 */
	bassert(b, b->type == Tpivot);
	return 2*(b->nval+1) + b->valsz + reserve*(8+Keymax+Ptrsz) > Pivspc;
}

static void
copyup(Blk *n, Path *pp, int *nbytes)
{
	Kvp kv;
	Msg m;

	/*
	 * It's possible for the previous node to have
	 * been fully cleared out by a large number of
	 * delete messages, so we need to check if
	 * there's anything in it to copy up.
	 */
	if(pp->nl != nil){
		getval(pp->nl, 0, &kv);
		if(pp->nl->nbuf > 0){
			getmsg(pp->nl, 0, &m);
			if(keycmp(&kv, &m) > 0)
				kv.Key = m.Key;
		}
		setptr(n, &kv, pp->nl->bp, blkfill(pp->nl));
		if(nbytes != nil)
			*nbytes += valsz(&kv);
	}
	if(pp->nr != nil){
		getval(pp->nr, 0, &kv);
		if(pp->nr->nbuf > 0){
			getmsg(pp->nr, 0, &m);
			if(keycmp(&kv, &m) > 0)
				kv.Key = m.Key;
		}
		setptr(n, &kv, pp->nr->bp, blkfill(pp->nr));
		if(nbytes != nil)
			*nbytes += valsz(&kv);
	}
}

static void
statupdate(Kvp *kv, Msg *m)
{
	char *p, *e;
	int op;
	Xdir d;

	assert(m->nv >= 1);
	p = m->v;
	op = *p++;
	kv2dir(kv, &d);
	/* bump version */
	d.qid.vers++;
	if(op & Owsize){
		d.length = UNPACK64(p);
		p += 8;
	}
	if(op & Owmode){
		d.mode = UNPACK32(p);
		d.qid.type = d.mode>>24;
		p += 4;
	}
	if(op & Owmtime){
		d.mtime = UNPACK64(p);
		p += 8;
	}
	if(op & Owatime){
		d.atime = UNPACK64(p);
		p += 8;
	}
	if(op & Owuid){
		d.uid = UNPACK32(p);
		p += 4;
	}
	if(op & Owgid){
		d.gid = UNPACK32(p);
		p += 4;
	}
	if(op & Owmuid){
		d.muid = UNPACK32(p);
		p += 4;
	}
	if(p != m->v + m->nv)
		fatal("malformed stat: kv=%P, m=%M\n", kv, m);
	e = packdval(kv->v, kv->nv, &d);
	if(e == nil || e - kv->v != kv->nv)
		fatal("repacking dir failed\n");
}

static int
apply(Kvp *kv, Msg *m, char *buf, int nbuf)
{
	vlong *pv;
	char *p;
	Tree t;

	switch(m->op){
	case Odelete:
		assert(keycmp(kv, m) == 0);
		return 0;
	case Oclearb:
	case Oclobber:
		return 0;
	case Oinsert:
		cpkvp(kv, m, buf, nbuf);
		return 1;
	case Owstat:
		assert(keycmp(kv, m) == 0);
		statupdate(kv, m);
		return 1;
	case Orelink:
	case Oreprev:
		unpacktree(&t, kv->v, kv->nv);
		p = m->v;
		pv = (m->op == Orelink) ? &t.succ : &t.pred;
		*pv = UNPACK64(p);	p += 8;
		t.nlbl += *p;		p++;
		t.nref += *p;		p++;
		assert(t.nlbl >= 0 && t.nref >= 0);
		assert(p == m->v + m->nv);
		packtree(kv->v, kv->nv, &t);
		return 1;
	default:
		fatal("invalid op %d\n", m->op);
	}
}

static void
setb(Blk **dst, Tree *t, Blk *b)
{
	if(*dst != nil){
		freeblk(t, *dst);
		dropblk(*dst);
	}
	if(b->nval == 0){
		freeblk(t, b);
		dropblk(b);
		*dst = nil;
	}else{
		enqueue(b);
		*dst = b;
	}
}
		

static int
pullmsg(Path *p, int i, Kvp *v, Msg *m, int *full, int spc)
{
	if(i < 0 || i >= p->hi || *full)
		return -1;

	if(p->ins != nil)
		*m = p->ins[i];
	else
		getmsg(p->b, i, m);
	if(msgsz(m) <= spc)
		return (v == nil) ? 0 : keycmp(v, m);
	*full = 1;
	return -1;
}

/*
 * Creates a new block with the contents of the old
 * block. When copying the contents, it repacks them
 * to minimize the space uses, and applies the changes
 * pending from the downpath blocks.
 *
 * When pidx != -1, 
 */
static void
updateleaf(Tree *t, Path *up, Path *p)
{
	char buf[Msgmax];
	int i, j, c, ok, full, spc;
	Blk *b, *n;
	Bptr bp;
	Msg m;
	Kvp v;

	i = 0;
	j = up->lo;
	b = p->b;
	/*
	 * spc is the amount of room we have
	 * to copy data down from the parent; it's
	 * necessarily a bit conservative, because
	 * deletion messages don't take space -- but
	 * we don't know how what the types of all
	 * messages are.
	 */
	full = 0;
	spc = Leafspc - blkfill(b);
	n = newblk(t, b->type);
	assert(i >= 0 && j >= 0);
	while(i < b->nval || j < up->hi){
		if(i >= b->nval)
			c = 1;
		else{
			c = -1;
			getval(p->b, i, &v);
			if(j < up->hi){
				if(up->ins != nil)
					m = up->ins[j];
				else
					getmsg(up->b, j, &m);
				if(msgsz(&m) <= spc)
					c = keycmp(&v, &m);
				else
					full = 1;
			}
		}
		switch(c){
		/* Value before message: just copy value */
		case -1:
			i++;
			setval(n, &v);
			break;
		/* Value merges with message sequence */
		case 0:
			i++;
			j++;
			cpkvp(&v, &v, buf, sizeof(buf));
			if(v.nk > 0 && v.k[0] == Kdat)
			if(m.op == Oclearb
			|| m.op == Oinsert
			|| m.op == Odelete){
				bp = unpackbp(v.v, v.nv);
				freebp(t, bp);
			}
			ok = apply(&v, &m, buf, sizeof(buf));
			goto Copyloop;
		/* Message before value: Insert message sequence */
		case 1:
			j++;
			cpkvp(&v, &m, buf, sizeof(buf));
			ok = 0;
			if(m.op != Oclearb && m.op != Oclobber){
				/* New keys need to start off with Oinsert */
				assert(m.op == Oinsert);
				spc -= valsz(&m);
				p->pullsz += msgsz(&m);
				ok = 1;
			}
			goto Copyloop;
		Copyloop:
			while(j < up->hi){
				if(pullmsg(up, j, &v, &m, &full, spc) != 0)
					break;
				assert(!full);
				if(ok && v.nk > 0 && v.k[0] == Kdat)
				if(m.op == Oclearb
				|| m.op == Oinsert
				|| m.op == Odelete){
					bp = unpackbp(v.v, v.nv);
					freebp(t, bp);
				}
				p->pullsz += msgsz(&m);
				ok = apply(&v, &m, buf, sizeof(buf));
				j++;
			}
			if(ok)
				setval(n, &v);
			break;
		}
	}
	p->npull = (j - up->lo);
	p->op = POmod;
	setb(&p->nl, t, n);
}

/*
 * Creates a new block with the contents of the old
 * block. When copying the contents, it repacks them
 * to minimize the space uses, and applies the changes
 * pending from the downpath blocks.
 *
 * When pidx != -1, 
 */
static void
updatepiv(Tree *t, Path *up, Path *p, Path *pp)
{
	char buf[Msgmax];
	int i, j, r, sz, full, spc;
	Blk *b, *n;
	Msg m, u;

	b = p->b;
	n = newblk(t, b->type);
	for(i = 0; i < b->nval; i++){
		if(pp != nil && i == p->midx){
			copyup(n, pp, nil);
			if(pp->op == POrot || pp->op == POmerge)
				i++;
		}else{
			getval(b, i, &m);
			setval(n, &m);
		}
	}
	i = 0;
	j = up->lo;
	sz = 0;
	full = 0;
	/*
	 * Compute free space for pulling messages
	 * from the parent node. The amount of space
	 * is the space we currently have, plus the
	 * amount we pulled down from this node in
	 * our child.
	 */
	spc = Bufspc - buffill(b);
	if(pp != nil)
		spc += pp->pullsz;
	while(i < b->nbuf){
		if(i == p->lo)
			i += pp->npull;
		if(i == b->nbuf)
			break;
		getmsg(b, i, &m);
		/*
		 * a bit tricky: we're signalling full full not when we're done
		 * copying, but when we're out of room for new messages. The
		 * amount of consumed space here is coming from messages that
		 * we pull down from the parent node. If not, we keep cloning
		 * messages without pulling. We do not want to break out when
		 * we report a full buffer here -- it's only too full for new
		 * data when that happens.
		 */
		r = pullmsg(up, j, &m, &u, &full, spc - sz);
		switch(r){
		case -1:
		case 0:
			setmsg(n, &m);
			i++;
			break;
		case 1:
			cpkvp(&m, &u, buf, sizeof(buf));
			while(pullmsg(up, j, &m, &u, &full, spc) == 0){
				setmsg(n, &u);
				sz = msgsz(&u);
				p->pullsz += sz;
				spc -= sz;
				j++;
			}
		}
	}
	while(j < up->hi){
		pullmsg(up, j, nil, &u, &full, spc);
		if(full)
			break;
		setmsg(n, &u);
		sz = msgsz(&u);
		p->pullsz += sz;
		spc -= sz;
		j++;
	}
	p->npull = (j - up->lo);
	p->op = POmod;
	setb(&p->nl, t, n);
}

/*
 * Splits a node, returning the block that msg
 * would be inserted into. Split must never
 * grow the total height of the tree by more than 1.
 */
static void
splitleaf(Tree *t, Path *up, Path *p)
{
	char buf[Msgmax];
	Blk *b, *d, *l, *r;
	int full, copied, spc, ok, halfsz;
	int i, j, c;
	Bptr bp;
	Msg m;
	Kvp v;

	/*
	 * If the block one entry up the
	 * p is nil, we're at the root,
	 * so we want to make a new block.
	 */
	b = p->b;
	l = nil;
	r = nil;
	if(waserror()){
		efreeblk(t, l);
		efreeblk(t, r);
		nexterror();
	}
	l = newblk(t, b->type);
	r = newblk(t, b->type);

	d = l;
	i = 0;
	j = up->lo;
	full = 0;
	copied = 0;
	halfsz = (2*b->nval + b->valsz + up->sz) / 2;
	if(halfsz > Leafspc/2)
		halfsz = Leafspc/2;
	spc = Leafspc - (halfsz + Msgmax);
	bassert(b, b->nval >= 4);
	while(i < b->nval){
		/*
		 * We're trying to balance size,
		 * but we need at least 2 nodes
		 * in each half of the split if
		 * we want a valid tree.
		 */
		if(d == l)
		if((i == b->nval-2) || (i >= 2 && copied >= halfsz)){
			d = r;
			spc = Leafspc - (halfsz + Msgmax);
		}
		getval(b, i, &v);
 		c = pullmsg(up, j, &v, &m, &full, spc);
		switch(c){
		case -1:
			i++;
			setval(d, &v);
			copied += valsz(&v);
			break;
		case 0:
			i++;
			j++;
			cpkvp(&v, &v, buf, sizeof(buf));
			copied += valsz(&v);
			if(v.nk > 0 && v.k[0] == Kdat)
			if(m.op == Oclearb
			|| m.op == Oinsert
			|| m.op == Odelete){
				bp = unpackbp(v.v, v.nv);
				freebp(t, bp);
			}
			ok = apply(&v, &m, buf, sizeof(buf));
			goto Copyloop;
		case 1:
			j++;
			cpkvp(&v, &m, buf, sizeof(buf));
			copied += valsz(&v);
			ok = 0;
			if(m.op != Oclearb && m.op != Oclobber){
				/* New keys need to start off with Oinsert */
				assert(m.op == Oinsert);
				spc -= valsz(&m);
				p->pullsz += msgsz(&m);
				ok = 1;
			}
			goto Copyloop;
		Copyloop:
			while(j < up->hi){
				if(pullmsg(up, j, &v, &m, &full, spc) != 0)
					break;
				if(ok && v.nk > 0 && v.k[0] == Kdat)
				if(m.op == Oclearb
				|| m.op == Oinsert
				|| m.op == Odelete){
					bp = unpackbp(v.v, v.nv);
					freebp(t, bp);
				}
				p->pullsz += msgsz(&m);
				ok = apply(&v, &m, buf, sizeof(buf));
				j++;
			}
			if(ok)
				setval(d, &v);
			break;
		}
	}
	p->npull = (j - up->lo);
	p->op = POsplit;
	setb(&p->nl, t, l);
	setb(&p->nr, t, r);
	poperror();
}

/*
 * Splits a node, returning the block that msg
 * would be inserted into. Split must never
 * grow the total height of the tree by more
 * than one.
 */
static void
splitpiv(Tree *t, Path *, Path *p, Path *pp)
{
	int i, copied, halfsz;
	Blk *b, *d, *l, *r;
	Kvp tk, mid;
	Msg m;

	/*
	 * If the bp->lock one entry up the
	 * p is nil, we're at the root,
	 * so we want to make a new bp->lock.
	 */
	b = p->b;
	l = nil;
	r = nil;
	if(waserror()){
		efreeblk(t, l);
		efreeblk(t, r);
		nexterror();
	}
	l = newblk(t, b->type);
	r = newblk(t, b->type);
	d = l;
	copied = 0;
	halfsz = (2*b->nval + b->valsz)/2;
	bassert(b, b->nval >= 4);
	for(i = 0; i < b->nval; i++){
		/*
		 * Balance size, but ensure we have at least 2 nodes
		 * in each half of the split so we have a valid tree.
		 */
		if(d == l && (i == b->nval-2 || (i >= 2 && copied >= halfsz)))
			d = r;
		if(i == p->idx){
			copyup(d, pp, &copied);
			continue;
		}
		getval(b, i, &tk);
		setval(d, &tk);
		copied += valsz(&tk);
	}
	d = l;
	getval(r, 0, &mid);
	for(i = 0; i < b->nbuf; i++){
		if(i == p->lo)
			i += pp->npull;
		if(i == b->nbuf)
			break;
		getmsg(b, i, &m);
		if(d == l && keycmp(&m, &mid) >= 0)
			d = r;
		setmsg(d, &m);
	}
	p->op = POsplit;
	setb(&p->nl, t, l);
	setb(&p->nr, t, r);
	poperror();
}

static void
merge(Tree *t, Path *p, Path *pp, int idx, Blk *a, Blk *b)
{
	Blk *d;
	Msg m;
	int i;

	d = newblk(t, a->type);
	for(i = 0; i < a->nval; i++){
		getval(a, i, &m);
		setval(d, &m);
	}
	for(i = 0; i < b->nval; i++){
		getval(b, i, &m);
		setval(d, &m);
	}
	if(a->type == Tpivot){
		for(i = 0; i < a->nbuf; i++){
			getmsg(a, i, &m);
			setmsg(d, &m);
		}
		for(i = 0; i < b->nbuf; i++){
			getmsg(b, i, &m);
			setmsg(d, &m);
		}
	}
	p->midx = idx;
	pp->op = POmerge;
	setb(&pp->nl, t, d);
}

/*
 * Scan a single block for the split offset;
 * returns 1 if we'd spill out of the buffer,
 * updates *idx and returns 0 otherwise.
 */
static int
spillscan(Blk *d, Blk *b, Msg *m, int *idx, int o)
{
	int i, used;
	Msg n;

	used = 2*d->nbuf + d->bufsz;
	for(i = *idx; i < b->nbuf; i++){
		getmsg(b, i, &n);
		if(keycmp(m, &n) <= 0){
			*idx = i + o;
			return 0;
		}
		used += msgsz(&n);
		if(used > Bufspc)
			return 1;
	}
	*idx = b->nbuf;
	return 0;
}

/*
 * Returns whether the keys in b between
 * idx and m would spill out of the buffer
 * of d.
 */
static int
spillsbuf(Blk *d, Blk *l, Blk *r, Msg *m, int *idx)
{
	if(l->type == Tleaf)
		return 0;

	if(*idx < l->nbuf && spillscan(d, l, m, idx, 0))
		return 1;
	if(*idx >= l->nbuf && spillscan(d, r, m, idx, l->nbuf))
		return 1;
	return 0;
}

static void
rotate(Tree *t, Path *p, Path *pp, int midx, Blk *a, Blk *b, int halfpiv)
{
	int i, o, cp, sp, idx;
	Blk *d, *l, *r;
	Msg m;

	l = nil;
	r = nil;
	if(waserror()){
		efreeblk(t, l);
		efreeblk(t, r);
		nexterror();
	}
	l = newblk(t, a->type);
	r = newblk(t, a->type);
	d = l;
	cp = 0;
	sp = -1;
	idx = 0;
	for(i = 0; i < a->nval; i++){
		getval(a, i, &m);
		if(d == l && (cp >= halfpiv || spillsbuf(d, a, b, &m, &idx))){
			sp = idx;
			d = r;
		}
		setval(d, &m);
		cp += valsz(&m);
	}
	for(i = 0; i < b->nval; i++){
		getval(b, i, &m);
		if(d == l && (cp >= halfpiv || spillsbuf(d, a, b, &m, &idx))){
			sp = idx;
			d = r;
		}
		setval(d, &m);
		cp += valsz(&m);
	}
	if(a->type == Tpivot){
		d = l;
		o = 0;
		for(i = 0; i < a->nbuf; i++){
			if(o == sp){
				d = r;
				o = 0;
			}
			getmsg(a, i, &m);
			setmsg(d, &m);
			o++;
		}
		for(i = 0; i < b->nbuf; i++){
			if(o == sp){
				d = r;
				o = 0;
			}
			getmsg(b, i, &m);
			setmsg(d, &m);
			o++;
		}
	}
	p->midx = midx;
	pp->op = POrot;
	setb(&pp->nl, t, l);
	setb(&pp->nr, t, r);
	poperror();
}

static void
rotmerge(Tree *t, Path *p, Path *pp, int idx, Blk *a, Blk *b)
{
	int na, nb, ma, mb, imbalance;

	bassert(a, a->type == b->type);

	na = 2*a->nval + a->valsz;
	nb = 2*b->nval + b->valsz;
	if(a->type == Tleaf){
		ma = 0;
		mb = 0;
	}else{
		ma = 2*a->nbuf + a->bufsz;
		mb = 2*b->nbuf + b->bufsz;
	}
	imbalance = na - nb;
	if(imbalance < 0)
		imbalance *= -1;
	/* works for leaf, because 0 always < Bufspc */
	if(na + nb < (Pivspc - 4*Msgmax) && ma + mb < Bufspc)
		merge(t, p, pp, idx, a, b);
	else if(imbalance > 4*Msgmax)
		rotate(t, p, pp, idx, a, b, (na + nb)/2);
}

static void
trybalance(Tree *t, Path *p, Path *pp, int idx)
{
	Blk *l, *m, *r;
	Kvp kl, kr;
	int spc, fill;
	Bptr bp;

	if(p->idx == -1 || pp == nil || pp->nl == nil)
		return;
	if(pp->op != POmod && pp->op != POmerge)
		return;

	l = nil;
	r = nil;
	m = holdblk(pp->nl);
	if(waserror()){
		dropblk(m);
		dropblk(l);
		dropblk(r);
		nexterror();
	}
	spc = (m->type == Tleaf) ? Leafspc : Pivspc;
	if(idx-1 >= 0){
		getval(p->b, idx-1, &kl);
		bp = getptr(&kl, &fill);
		if(fill + blkfill(m) < spc){
			l = getblk(bp, 0);
			rotmerge(t, p, pp, idx-1, l, m);
			goto Done;
		}
	}
	if(idx+1 < p->b->nval){
		getval(p->b, idx+1, &kr);
		bp = getptr(&kr, &fill);
		if(fill + blkfill(m) < spc){
			r = getblk(bp, 0);
			rotmerge(t, p, pp, idx, m, r);
			goto Done;
		}
	}
Done:
	dropblk(m);
	dropblk(l);
	dropblk(r);
	poperror();
}

static Path*
flush(Tree *t, Path *path, int npath)
{

	Path *up, *p, *pp, *rp;

	/*
	 * The path must contain at minimum two elements:
	 * we must have 1 node we're inserting into, and
	 * an empty element at the top of the path that
	 * we put the new root into if the root gets split.
	 */
	assert(npath >= 2);
	rp = nil;
	pp = nil;
	p = &path[npath - 1];
	up = &path[npath - 2];
	if(p->b->type == Tleaf){
		if(!filledleaf(p->b, up->sz)){
			updateleaf(t, p-1, p);
			rp = p;
		}else{
			splitleaf(t, up, p);
		}
		p->midx = -1;
		pp = p;
		up--;
		p--;
	}
	while(p != path){
		/*
		 * While we will add at most one key to a tree,
		 * we can mutate the children so that we end up
		 * replacing small nodes with large ones; as a
		 * result we need to be a bit more conservative
		 * here, and split earlier than we may otherwise
		 * want to do.
		 */
		if(!filledpiv(p->b, 2)){
			trybalance(t, p, pp, p->idx);
			/* If we merged the root node, break out. */
			if(up == path && pp != nil && pp->op == POmerge && p->b->nval == 2){
				pp->npull = p->npull;
				rp = pp;
				goto Out;
			}
			updatepiv(t, up, p, pp);
			rp = p;
		}else{
			splitpiv(t, up, p, pp);
		}
		pp = p;
		up--;
		p--;
	}
	if(pp->nl != nil && pp->nr != nil){
		rp = &path[0];
		rp->nl = newblk(t, Tpivot);
		rp->npull = pp->npull;
		rp->pullsz = pp->pullsz;
		copyup(rp->nl, pp, nil);
		enqueue(rp->nl);
	}
Out:
	return rp;
}

static void
freepath(Tree *t, Path *path, int npath, int ok)
{
	Path *p;

	for(p = path; p != path + npath; p++){
		if(ok && p->b != nil)
			freeblk(t, p->b);
		dropblk(p->b);
		dropblk(p->nl);
		dropblk(p->nr);
	}
	free(path);
}

/*
 * Select child node that with the largest message
 * segment in the current node's buffer.
 */
static void
victim(Blk *b, Path *p)
{
	int i, j, lo, maxsz, cursz;
	Kvp kv;
	Msg m;

	j = 0;
	maxsz = 0;
	p->b = b;
	/* 
	 * Start at the second pivot: all values <= this
	 * go to the first node. Stop *after* the last entry,
	 * because entries >= the last entry all go into it.
	 */
	for(i = 1; i <= b->nval; i++){
		if(i < b->nval)
			getval(b, i, &kv);
		cursz = 0;
		lo = j;
		for(; j < b->nbuf; j++){
			getmsg(b, j, &m);
			if(i < b->nval && keycmp(&m, &kv) >= 0)
				break;
			/* 2 bytes for offset, plus message size in buffer */
			cursz += msgsz(&m);
		}
		if(cursz > maxsz){
			maxsz = cursz;
			p->op = POmod;
			p->lo = lo;
			p->hi = j;
			p->sz = maxsz;
			p->idx = i - 1;
			p->midx = i - 1;
			p->npull = 0;
			p->pullsz = 0;
		}
	}
}

static void
fastupsert(Tree *t, Blk *b, Msg *msg, int nmsg)
{
	int i, c, o, ri, lo, hi, mid, nbuf;
	Msg cmp;
	char *p;
	Blk *r;

	if((r = dupblk(t, b)) == nil)
		error(Enomem);

	nbuf = r->nbuf;
	for(i = 0; i < nmsg; i++)
		setmsg(r, &msg[i]);

	for(i = 0; i < nmsg; i++){
		ri = -1;
		lo = 0;
		hi = nbuf+i-1;
		while(lo <= hi){
			mid = (hi + lo) / 2;
			getmsg(r, mid, &cmp);
			c = keycmp(&msg[i], &cmp);
			switch(c){
			case -1:
				hi = mid-1;
				break;
			case 0:
				ri = mid+1;
				lo = mid+1;
				break;
			case 1:
				lo = mid+1;
				break;
			}
		}
		if(ri == -1)
			ri = hi+1;
		p = r->data + Pivspc + 2*(nbuf+i);
		o = UNPACK16(p);
		p = r->data + Pivspc + 2*ri;
		memmove(p+2, p, 2*(nbuf+i-ri));
		PACK16(p, o);
	}
	enqueue(r);

	lock(&t->lk);
	t->bp = r->bp;
	t->dirty = 1;
	unlock(&t->lk);

	freeblk(t, b);
	dropblk(b);
	dropblk(r);
}
	

void
btupsert(Tree *t, Msg *msg, int nmsg)
{
	int i, npath, npull, dh, sz, height;
	Path *path, *rp;
	Blk *b, *rb;
	Kvp sep;
	Bptr bp;

	assert(!canqlock(&fs->mutlk));
	sz = 0;
	stablesort(msg, nmsg);
	for(i = 0; i < nmsg; i++){
		assert(msg[i].nk <= Keymax);
		assert(msg[i].nv <= Inlmax);
		sz += msgsz(&msg[i]);
	}
	npull = 0;
Again:
	b = getroot(t, &height);
	if(waserror()){
		aincl(&fs->rdonly, 1);
		dropblk(b);
		nexterror();
	}
	if(npull == 0 && b->type == Tpivot && !filledbuf(b, nmsg, sz)){
		fastupsert(t, b, msg, nmsg);
		poperror();
		return;
	}
	/*
	 * The tree can grow in height by 1 when we
	 * split, so we allocate room for one extra
	 * node in the path.
	 */
	if((path = calloc((height + 2), sizeof(Path))) == nil)
		error(Enomem);
	poperror();
	if(waserror()){
		freepath(t, path, height+2, 0);	/* npath not volatile */
		nexterror();
	}
	npath = 0;
	path[npath].b = nil;
	path[npath].idx = -1;
	path[npath].midx = -1;
	npath++;

	path[0].sz = sz;
	path[0].ins = msg;
	path[0].lo = npull;
	path[0].hi = nmsg;
	while(b->type == Tpivot){
		if(!filledbuf(b, nmsg, path[npath - 1].sz))
			break;
		victim(b, &path[npath]);
		getval(b, path[npath].idx, &sep);
		bp = unpackbp(sep.v, sep.nv);
		b = getblk(bp, 0);
		npath++;
		assert(npath < height+2);
	}
	path[npath].b = b;
	path[npath].idx = -1;
	path[npath].midx = -1;
	path[npath].lo = -1;
	path[npath].hi = -1;
	path[npath].npull = 0;
	path[npath].pullsz = 0;
	npath++;

	rp = flush(t, path, npath);
	rb = rp->nl;

	if(path[0].nl != nil)
		dh = 1;
	else if(path[1].nl != nil)
		dh = 0;
	else if(npath >2 && path[2].nl != nil)
		dh = -1;
	else
		fatal("broken path change");

	bassert(rb, rb->bp.addr != 0);
	bassert(rb, rb->bp.addr != 0);

	lock(&t->lk);
	traceb("setroot", rb->bp);
	t->ht += dh;
	t->bp = rb->bp;
	t->dirty = 1;
	unlock(&t->lk);

	npull += rp->npull;
	freepath(t, path, npath, 1);
	poperror();

	if(npull != nmsg){
		tracem("short pull");
		goto Again;
	}
}

Blk*
getroot(Tree *t, int *h)
{
	Bptr bp;

	lock(&t->lk);
	bp = t->bp;
	if(h != nil)
		*h = t->ht;
	unlock(&t->lk);

	return getblk(bp, 0);
}

int
btlookup(Tree *t, Key *k, Kvp *r, char *buf, int nbuf)
{
	int i, j, h, ok, same;
	Blk *b, **p;
	Bptr bp;
	Msg m;

	b = getroot(t, &h);
	if((p = calloc(h, sizeof(Blk*))) == nil){
		dropblk(b);
		error(Enomem);
	}
	if(waserror()){
		for(i = 0; i < h; i++)
			dropblk(p[i]);
		dropblk(b);
		free(p);
		nexterror();
	}
	ok = 0;
	p[0] = holdblk(b);
	for(i = 1; i < h; i++){
		if(blksearch(p[i-1], k, r, &same) == -1)
			break;
		bp = unpackbp(r->v, r->nv);
		p[i] = getblk(bp, 0);
	}
	if(p[h-1] != nil)
		blksearch(p[h-1], k, r, &ok);
	if(ok)
		cpkvp(r, r, buf, nbuf);
	for(i = h-2; i >= 0; i--){
		if(p[i] == nil)
			continue;
		j = bufsearch(p[i], k, &m, &same);
		if(j < 0 || !same)
			continue;
		if(ok || m.op == Oinsert)
			ok = apply(r, &m, buf, nbuf);
		else if(m.op != Oclearb && m.op != Oclobber)
			fatal("lookup %K << %M missing insert\n", k, &m);
		for(j++; j < p[i]->nbuf; j++){
			getmsg(p[i], j, &m);
			if(keycmp(k, &m) != 0)
				break;
			ok = apply(r, &m, buf, nbuf);
		}
	}
	for(i = 0; i < h; i++)
		dropblk(p[i]);
	dropblk(b);
	free(p);
	poperror();
	return ok;
}

void
btnewscan(Scan *s, char *pfx, int npfx)
{
	memset(s, 0, sizeof(*s));
	s->first = 1;
	s->donescan = 0;
	s->offset = 0;
	s->pfx.k = s->pfxbuf;
	s->pfx.nk = npfx;
	memmove(s->pfxbuf, pfx, npfx);

	s->kv.v = s->kvbuf+npfx;
	s->kv.nv = 0;
	cpkey(&s->kv, &s->pfx, s->kvbuf, sizeof(s->kvbuf));
}

void
btenter(Tree *t, Scan *s)
{
	int i, same;
	Scanp *p;
	Msg m, c;
	Bptr bp;
	Blk *b;
	Kvp v;

	if(s->donescan)
		return;
	b = getroot(t, &s->ht);
	if((s->path = calloc(s->ht, sizeof(Scanp))) == nil){
		dropblk(b);
		error(Enomem);
	}
	if(waserror()){
		btexit(s);
		nexterror();
	}
	p = s->path;
	p[0].b = b;
	for(i = 0; i < s->ht; i++){
		p[i].vi = blksearch(b, &s->kv, &v, &same);
		if(b->type == Tpivot){
			if(p[i].vi == -1)
				getval(b, ++p[i].vi, &v);
			p[i].bi = bufsearch(b, &s->kv, &m, &same);
			if(p[i].bi == -1){
				p[i].bi++;
			}else if(!same || !s->first){
				/* scan past repeated messages */
				while(p[i].bi < p[i].b->nbuf){
					getmsg(p[i].b, p[i].bi, &c);
					if(keycmp(&m, &c) != 0)
						break;
					p[i].bi++;
				}
			}
			bp = unpackbp(v.v, v.nv);
			b = getblk(bp, 0);
			p[i+1].b = b;
		}else if(p[i].vi == -1 || !same || !s->first)
			p[i].vi++;
	}
	s->first = 0;
	poperror();
}

int
btnext(Scan *s, Kvp *r)
{
	int i, j, h, ok, start, bufsrc;
	Scanp *p;
	Msg m, n;
	Bptr bp;
	Kvp kv;

Again:
	p = s->path;
	h = s->ht;
	start = h;
	bufsrc = -1;
	if(p == nil || s->donescan)
		return 0;
	if(waserror()){
		btexit(s);
		nexterror();
	}
	/* load up the correct blocks for the scan */
	for(i = h-1; i >= 0; i--){
		if(p[i].b != nil
		&&(p[i].vi < p[i].b->nval || p[i].bi < p[i].b->nbuf))
			break;
		if(i == 0){
			s->donescan = 1;
			poperror();
			return 0;
		}
		if(p[i].b != nil)
			dropblk(p[i].b);
		p[i].b = nil;
		p[i].vi = 0;
		p[i].bi = 0;
		p[i-1].vi++;
		start = i;
	}

	if(p[start-1].vi < p[start-1].b->nval){
		for(i = start; i < h; i++){
			getval(p[i-1].b, p[i-1].vi, &kv);
			bp = unpackbp(kv.v, kv.nv);
			p[i].b = getblk(bp, 0);
		}
	
		/* find the minimum key along the path up */
		m.op = Oinsert;
		getval(p[h-1].b, p[h-1].vi, &m);
	}else{
		getmsg(p[start-1].b, p[start-1].bi, &m);
		assert(m.op == Oinsert);
		bufsrc = start-1;
	}

	for(i = h-2; i >= 0; i--){
		if(p[i].b == nil || p[i].bi == p[i].b->nbuf)
			continue;
		getmsg(p[i].b, p[i].bi, &n);
		if(keycmp(&n, &m) < 0){
			bufsrc = i;
			m = n;
		}
	}
	if(m.nk < s->pfx.nk || memcmp(m.k, s->pfx.k, s->pfx.nk) != 0){
		s->donescan = 1;
		poperror();
		return 0;
	}

	/* scan all messages applying to the message */
	ok = 1;
	cpkvp(r, &m, s->kvbuf, sizeof(s->kvbuf));
	if(bufsrc == -1)
		p[h-1].vi++;
	else
		p[bufsrc].bi++;
	for(i = h-2; i >= 0; i--){
		for(j = p[i].bi; p[i].b != nil && j < p[i].b->nbuf; j++){
			getmsg(p[i].b, j, &m);
			if(keycmp(r, &m) != 0)
				break;
			ok = apply(r, &m, s->kvbuf, sizeof(s->kvbuf));
			p[i].bi++;
		}
	}
	poperror();
	if(!ok)
		goto Again;
	return 1;
}

void
btexit(Scan *s)
{
	int i;

	for(i = 0; i < s->ht; i++)
		dropblk(s->path[i].b);
	free(s->path);
	s->path = nil;
	s->ht = 0;
}
