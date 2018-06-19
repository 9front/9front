#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"ip.h"

static void	walkadd(Fs*, Route**, Route*);
static void	addnode(Fs*, Route**, Route*);
static void	calcd(Route*);

/* these are used for all instances of IP */
static Route*	v4freelist;
static Route*	v6freelist;
static RWlock	routelock;
static ulong	v4routegeneration, v6routegeneration;

static void
freeroute(Route *r)
{
	Route **l;

	r->ref = 0;
	r->left = nil;
	r->right = nil;
	if(r->type & Rv4)
		l = &v4freelist;
	else
		l = &v6freelist;
	r->mid = *l;
	*l = r;
}

static Route*
allocroute(int type)
{
	Route *r, **l;
	int n;

	if(type & Rv4){
		n = sizeof(RouteTree) + sizeof(V4route);
		l = &v4freelist;
	} else {
		n = sizeof(RouteTree) + sizeof(V6route);
		l = &v6freelist;
	}

	r = *l;
	if(r != nil){
		*l = r->mid;
	} else {
		r = malloc(n);
		if(r == nil)
			panic("out of routing nodes");
	}
	memset(r, 0, n);
	r->type = type;
	r->ifc = nil;
	r->ref = 1;

	return r;
}

static void
addqueue(Route **q, Route *r)
{
	Route *l;

	if(r == nil)
		return;

	l = allocroute(r->type);
	l->left = r;
	l->mid = *q;
	*q = l;
}

/*
 *   compare 2 v6 addresses
 */
static int
lcmp(ulong *a, ulong *b)
{
	int i;

	for(i = 0; i < IPllen; i++){
		if(a[i] > b[i])
			return 1;
		if(a[i] < b[i])
			return -1;
	}
	return 0;
}

/*
 *  compare 2 v4 or v6 ranges
 */
enum
{
	Rpreceeds,	/* a left of b */
	Rfollows,	/* a right of b */
	Requals,	/* a equals b */
	Rcontains,	/* a contians b */
	Roverlaps,	/* a overlaps b */
};

static int
rangecompare(Route *a, Route *b)
{
	if(a->type & Rv4){
		if(a->v4.endaddress < b->v4.address)
			return Rpreceeds;
		if(a->v4.address > b->v4.endaddress)
			return Rfollows;
		if(a->v4.address <= b->v4.address
		&& a->v4.endaddress >= b->v4.endaddress){
			if(a->v4.address == b->v4.address
			&& a->v4.endaddress == b->v4.endaddress){
				if(a->v4.source <= b->v4.source
				&& a->v4.endsource >= b->v4.endsource){
					if(a->v4.source == b->v4.source
					&& a->v4.endsource == b->v4.endsource)
						return Requals;
					return Rcontains;
				}
				return Roverlaps;
			}
			return Rcontains;
		}
		return Roverlaps;
	}

	if(lcmp(a->v6.endaddress, b->v6.address) < 0)
		return Rpreceeds;
	if(lcmp(a->v6.address, b->v6.endaddress) > 0)
		return Rfollows;
	if(lcmp(a->v6.address, b->v6.address) <= 0
	&& lcmp(a->v6.endaddress, b->v6.endaddress) >= 0){
		if(lcmp(a->v6.address, b->v6.address) == 0
		&& lcmp(a->v6.endaddress, b->v6.endaddress) == 0){
			if(lcmp(a->v6.source, b->v6.source) <= 0
			&& lcmp(a->v6.endsource, b->v6.endsource) >= 0){
				if(lcmp(a->v6.source, b->v6.source) == 0
				&& lcmp(a->v6.endsource, b->v6.endsource) == 0)
					return Requals;
				return Rcontains;
			}
			return Roverlaps;
		}
		return Rcontains;
	}
	return Roverlaps;
}

/* return 1 if a matches b, otherwise 0 */
static int
matchroute(Route *a, Route *b)
{
	if(a == b)
		return 1;

	if((a->type^b->type) & (Rifc|Runi|Rmulti|Rbcast))
		return 0;

	if(a->type & Rv4){
		if(memcmp(a->v4.gate, IPnoaddr+IPv4off, IPv4addrlen) != 0
		&& memcmp(a->v4.gate, b->v4.gate, IPv4addrlen) != 0)
			return 0;
	} else {
		if(ipcmp(a->v6.gate, IPnoaddr) != 0
		&& ipcmp(a->v6.gate, b->v6.gate) != 0)
			return 0;
	}

	if(a->ifc != nil && b->ifc != nil && (a->ifc != b->ifc || a->ifcid != b->ifcid))
		return 0;

	if(*a->tag != 0 && strncmp(a->tag, b->tag, sizeof(a->tag)) != 0)
		return 0;

	return 1;
}

static void
copygate(Route *old, Route *new)
{
	old->type = new->type;
	old->ifc = new->ifc;
	old->ifcid = new->ifcid;
	if(new->type & Rv4)
		memmove(old->v4.gate, new->v4.gate, IPv4addrlen);
	else
		ipmove(old->v6.gate, new->v6.gate);
	strncpy(old->tag, new->tag, sizeof(new->tag));
}

/*
 *  walk down a tree adding nodes back in
 */
static void
walkadd(Fs *f, Route **root, Route *p)
{
	Route *l, *r;

	l = p->left;
	r = p->right;
	p->left = nil;
	p->right = nil;
	addnode(f, root, p);
	if(l != nil)
		walkadd(f, root, l);
	if(r != nil)
		walkadd(f, root, r);
}

/*
 *  calculate depth
 */
static void
calcd(Route *p)
{
	Route *q;
	int d;

	if(p != nil) {
		d = 0;
		q = p->left;
		if(q != nil)
			d = q->depth;
		q = p->right;
		if(q != nil && q->depth > d)
			d = q->depth;
		q = p->mid;
		if(q != nil && q->depth > d)
			d = q->depth;
		p->depth = d+1;
	}
}

/*
 *  balance the tree at the current node
 */
static void
balancetree(Route **cur)
{
	Route *p, *l, *r;
	int dl, dr;

	/*
	 * if left and right are
	 * too out of balance,
	 * rotate tree node
	 */
	p = *cur;
	dl = 0; if((l = p->left) != nil) dl = l->depth;
	dr = 0; if((r = p->right) != nil) dr = r->depth;

	if(dl > dr+1) {
		p->left = l->right;
		l->right = p;
		*cur = l;
		calcd(p);
		calcd(l);
	} else
	if(dr > dl+1) {
		p->right = r->left;
		r->left = p;
		*cur = r;
		calcd(p);
		calcd(r);
	} else
		calcd(p);
}

/*
 *  add a new node to the tree
 */
static void
addnode(Fs *f, Route **cur, Route *new)
{
	Route *p;

	p = *cur;
	if(p == nil) {
		*cur = new;
		new->depth = 1;
		return;
	}

	switch(rangecompare(new, p)){
	case Rpreceeds:
		addnode(f, &p->left, new);
		break;
	case Rfollows:
		addnode(f, &p->right, new);
		break;
	case Rcontains:
		/*
		 *  if new node is superset
		 *  of tree node,
		 *  replace tree node and
		 *  queue tree node to be
		 *  merged into root.
		 */
		*cur = new;
		new->depth = 1;
		addqueue(&f->queue, p);
		break;
	case Requals:
		/*
		 *  supercede the old entry if the old one isn't
		 *  a local interface.
		 */
		if((p->type & Rifc) == 0)
			copygate(p, new);
		else if(new->type & Rifc)
			p->ref++;
		freeroute(new);
		break;
	case Roverlaps:
		addnode(f, &p->mid, new);
		break;
	}
	
	balancetree(cur);
}

/*
 *  find node matching r
 */
static Route**
looknode(Route **cur, Route *r)
{
	Route *p;

	for(;;){
		p = *cur;
		if(p == nil)
			return nil;
		switch(rangecompare(r, p)){
		case Rcontains:
			return nil;
		case Rpreceeds:
			cur = &p->left;
			break;
		case Rfollows:
			cur = &p->right;
			break;
		case Roverlaps:
			cur = &p->mid;
			break;
		case Requals:
			if((p->type & Rifc) == 0 && !matchroute(r, p))
				return nil;
			return cur;
		}
	}
}

static Route*
looknodetag(Route *r, char *tag)
{
	Route *x;

	if(r == nil)
		return nil;

	if((x = looknodetag(r->mid, tag)) != nil)
		return x;
	if((x = looknodetag(r->left, tag)) != nil)
		return x;
	if((x = looknodetag(r->right, tag)) != nil)
		return x;

	if((r->type & Rifc) == 0){
		if(tag == nil || strncmp(tag, r->tag, sizeof(r->tag)) == 0)
			return r;
	}

	return nil;
}

#define	V4H(a)	((a&0x07ffffff)>>(32-Lroot-5))
#define	V6H(a)	(((a)[IPllen-1]&0x07ffffff)>>(32-Lroot-5))

static void
routeadd(Fs *f, Route *r)
{
	Route **h, **e, *p;

	if(r->type & Rv4){
		h = &f->v4root[V4H(r->v4.address)];
		e = &f->v4root[V4H(r->v4.endaddress)];
	} else {
		h = &f->v6root[V6H(r->v6.address)];
		e = &f->v6root[V6H(r->v6.endaddress)];
	}

	for(; h <= e; h++) {
		p = allocroute(r->type);

		p->ifc = r->ifc;
		p->ifcid = r->ifcid;

		if(r->type & Rv4)
			memmove(&p->v4, &r->v4, sizeof(r->v4));
		else
			memmove(&p->v6, &r->v6, sizeof(r->v6));

		memmove(p->tag, r->tag, sizeof(r->tag));

		addnode(f, h, p);
		while((p = f->queue) != nil) {
			f->queue = p->mid;
			walkadd(f, h, p->left);
			freeroute(p);
		}
	}

	if(r->type & Rv4)
		v4routegeneration++;
	else
		v6routegeneration++;
}

static void
routerem(Fs *f, Route *r)
{
	Route **h, **e, **l, *p;

	if(r->type & Rv4){
		h = &f->v4root[V4H(r->v4.address)];
		e = &f->v4root[V4H(r->v4.endaddress)];
	} else {
		h = &f->v6root[V6H(r->v6.address)];
		e = &f->v6root[V6H(r->v6.endaddress)];
	}

	for(; h <= e; h++) {
		if((l = looknode(h, r)) == nil)
			continue;
		p = *l;
		if(--(p->ref) != 0)
			continue;
		*l = nil;
		addqueue(&f->queue, p->left);
		addqueue(&f->queue, p->mid);
		addqueue(&f->queue, p->right);
		freeroute(p);

		while((p = f->queue) != nil) {
			f->queue = p->mid;
			walkadd(f, h, p->left);
			freeroute(p);
		}
	}

	if(r->type & Rv4)
		v4routegeneration++;
	else
		v6routegeneration++;
}

static Route
mkroute(uchar *a, uchar *mask, uchar *s, uchar *smask, uchar *gate, int type, Ipifc *ifc, char *tag)
{
	ulong x, y;
	Route r;
	int h;

	memset(&r, 0, sizeof(r));

	r.type = type;

	if(type & Rv4){
		x = nhgetl(a+IPv4off);
		y = nhgetl(mask+IPv4off);
		r.v4.address = x & y;
		r.v4.endaddress = x | ~y;

		x = nhgetl(s+IPv4off);
		y = nhgetl(smask+IPv4off);
		if(y != 0)
			r.type |= Rsrc;
		r.v4.source = x & y;
		r.v4.endsource = x | ~y;

		memmove(r.v4.gate, gate+IPv4off, IPv4addrlen);
	} else {
		for(h = 0; h < IPllen; h++){
			x = nhgetl(a+4*h);
			y = nhgetl(mask+4*h);
			r.v6.address[h] = x & y;
			r.v6.endaddress[h] = x | ~y;

			x = nhgetl(s+4*h);
			y = nhgetl(smask+4*h);
			if(y != 0)
				r.type |= Rsrc;
			r.v6.source[h] = x & y;
			r.v6.endsource[h] = x | ~y;
		}

		memmove(r.v6.gate, gate, IPaddrlen);
	}

	if(ifc != nil){
		r.ifc = ifc;
		r.ifcid = ifc->ifcid;
	}

	if(tag != nil)
		strncpy(r.tag, tag, sizeof(r.tag));

	return r;
}

void
addroute(Fs *f, uchar *a, uchar *mask, uchar *s, uchar *smask, uchar *gate, int type, Ipifc *ifc, char *tag)
{
	Route r = mkroute(a, mask, s, smask, gate, type, ifc, tag);
	wlock(&routelock);
	routeadd(f, &r);
	wunlock(&routelock);
}

void
remroute(Fs *f, uchar *a, uchar *mask, uchar *s, uchar *smask, uchar *gate, int type, Ipifc *ifc, char *tag)
{
	Route r = mkroute(a, mask, s, smask, gate, type, ifc, tag);
	wlock(&routelock);
	routerem(f, &r);
	wunlock(&routelock);
}

Route*
v4lookup(Fs *f, uchar *a, uchar *s, Routehint *rh)
{
	uchar local[IPaddrlen], gate[IPaddrlen];
	ulong la, ls;
	Route *p, *q;
	Ipifc *ifc;

	if(rh != nil
	&& rh->rgen == v4routegeneration
	&& (q = rh->r) != nil
	&& (ifc = q->ifc) != nil
	&& q->ifcid == ifc->ifcid
	&& q->ref > 0)
		return q;

	la = nhgetl(a);
	ls = nhgetl(s);
	q = nil;
	for(p = f->v4root[V4H(la)]; p != nil;){
		if(la < p->v4.address){
			p = p->left;
			continue;
		}
		if(la > p->v4.endaddress){
			p = p->right;
			continue;
		}
		if(p->type & Rsrc){
			if(ls < p->v4.source){
				p = p->mid;
				continue;
			}
			if(ls > p->v4.endsource){
				p = p->mid;
				continue;
			}
		}
		q = p;
		p = p->mid;
	}

	if(q == nil || q->ref == 0)
		return nil;

	if(q->ifc == nil || q->ifcid != q->ifc->ifcid){
		if(q->type & Rifc) {
			hnputl(gate+IPv4off, q->v4.address);
			memmove(gate, v4prefix, IPv4off);
		} else
			v4tov6(gate, q->v4.gate);
		v4tov6(local, s);
		ifc = findipifc(f, local, gate, q->type);
		if(ifc == nil)
			return nil;
		q->ifc = ifc;
		q->ifcid = ifc->ifcid;
	}

	if(rh != nil){
		rh->r = q;
		rh->rgen = v4routegeneration;
	}

	return q;
}

Route*
v6lookup(Fs *f, uchar *a, uchar *s, Routehint *rh)
{
	uchar gate[IPaddrlen];
	ulong la[IPllen], ls[IPllen];
	ulong x, y;
	Route *p, *q;
	Ipifc *ifc;
	int h;

	if(isv4(s)){
		if(isv4(a))
			return v4lookup(f, a+IPv4off, s+IPv4off, rh);
		return nil;
	}

	if(rh != nil
	&& rh->rgen == v6routegeneration
	&& (q = rh->r) != nil
	&& (ifc = q->ifc) != nil
	&& q->ifcid == ifc->ifcid
	&& q->ref > 0)
		return q;

	for(h = 0; h < IPllen; h++){
		la[h] = nhgetl(a+4*h);
		ls[h] = nhgetl(s+4*h);
	}

	q = nil;
	for(p = f->v6root[V6H(la)]; p != nil;){
		for(h = 0; h < IPllen; h++){
			x = la[h];
			y = p->v6.address[h];
			if(x == y)
				continue;
			if(x < y){
				p = p->left;
				goto next;
			}
			break;
		}
		for(h = 0; h < IPllen; h++){
			x = la[h];
			y = p->v6.endaddress[h];
			if(x == y)
				continue;
			if(x > y){
				p = p->right;
				goto next;
			}
			break;
		}
		if(p->type & Rsrc){
			for(h = 0; h < IPllen; h++){
				x = ls[h];
				y = p->v6.source[h];
				if(x == y)
					continue;
				if(x < y){
					p = p->mid;
					goto next;
				}
				break;
			}
			for(h = 0; h < IPllen; h++){
				x = ls[h];
				y = p->v6.endsource[h];
				if(x == y)
					continue;
				if(x > y){
					p = p->mid;
					goto next;
				}
				break;
			}
		}
		q = p;
		p = p->mid;
next:		;
	}

	if(q == nil || q->ref == 0)
		return nil;

	if(q->ifc == nil || q->ifcid != q->ifc->ifcid){
		if(q->type & Rifc) {
			for(h = 0; h < IPllen; h++)
				hnputl(gate+4*h, q->v6.address[h]);
			ifc = findipifc(f, s, gate, q->type);
		} else
			ifc = findipifc(f, s, q->v6.gate, q->type);
		if(ifc == nil)
			return nil;
		q->ifc = ifc;
		q->ifcid = ifc->ifcid;
	}

	if(rh != nil){
		rh->r = q;
		rh->rgen = v6routegeneration;
	}
	
	return q;
}

static int
parseroutetype(char *p)
{
	int type = 0;
	switch(*p++){
	default:	return -1;	
	case '4':	type |= Rv4;
	case '6':	break;
	}
	for(;;) switch(*p++){
	default: 
		return -1;
	case 'i':
		if(((type ^= Rifc) & Rifc) != Rifc) return -1;
		break;
	case 'u':
		if(((type ^= Runi) & (Runi|Rbcast|Rmulti)) != Runi) return -1;
		break;
	case 'b':
		if(((type ^= Rbcast) & (Runi|Rbcast|Rmulti)) != Rbcast) return -1;
		break;
	case 'm':
		if(((type ^= Rmulti) & (Runi|Rbcast|Rmulti)) != Rmulti) return -1;
		break;
	case 'p':
		if(((type ^= Rptpt) & Rptpt) != Rptpt) return -1;
		break;
	case '\0':
		return type;
	}
}

void
routetype(int type, char p[8])
{
	if(type & Rv4)
		*p++ = '4';
	else
		*p++ = '6';

	if(type & Rifc)
		*p++ = 'i';

	if(type & Runi)
		*p++ = 'u';
	else if(type & Rbcast)
		*p++ = 'b';
	else if(type & Rmulti)
		*p++ = 'm';

	if(type & Rptpt)
		*p++ = 'p';
	*p = 0;
}

static void
convroute(Route *r, uchar *addr, uchar *mask, uchar *src, uchar *smask, uchar *gate)
{
	int i;

	if(r->type & Rv4){
		memmove(addr, v4prefix, IPv4off);
		hnputl(addr+IPv4off, r->v4.address);

		memset(mask, 0xff, IPv4off);
		hnputl(mask+IPv4off, ~(r->v4.endaddress ^ r->v4.address));

		memmove(src, v4prefix, IPv4off);
		hnputl(src+IPv4off, r->v4.source);

		memset(smask, 0xff, IPv4off);
		hnputl(smask+IPv4off, ~(r->v4.endsource ^ r->v4.source));

		memmove(gate, v4prefix, IPv4off);
		memmove(gate+IPv4off, r->v4.gate, IPv4addrlen);
	} else {
		for(i = 0; i < IPllen; i++){
			hnputl(addr + 4*i, r->v6.address[i]);
			hnputl(mask + 4*i, ~(r->v6.endaddress[i] ^ r->v6.address[i]));
			hnputl(src + 4*i, r->v6.source[i]);
			hnputl(smask + 4*i, ~(r->v6.endsource[i] ^ r->v6.source[i]));
		}
		memmove(gate, r->v6.gate, IPaddrlen);
	}
}

static char*
seprintroute(char *p, char *e, Route *r)
{
	uchar addr[IPaddrlen], mask[IPaddrlen], src[IPaddrlen], smask[IPaddrlen], gate[IPaddrlen];
	char type[8], ifbuf[4], *iname;

	convroute(r, addr, mask, src, smask, gate);
	routetype(r->type, type);
	if(r->ifc != nil && r->ifcid == r->ifc->ifcid)
		snprint(iname = ifbuf, sizeof ifbuf, "%d", r->ifc->conv->x);
	else
		iname = "-";
	return seprint(p, e, "%-15I %-4M %-15I %-4s %4.4s %3s %-15I %-4M\n",
		addr, mask, gate, type, r->tag, iname, src, smask);
}

typedef struct Routewalk Routewalk;
struct Routewalk
{
	int	o;
	int	h;
	char*	p;
	char*	e;
};

static int
rr1(Routewalk *rw, Route *r)
{
	int n = seprintroute(rw->p, rw->e, r) - rw->p;
	if(rw->o < 0){
		if(n > -rw->o){
			memmove(rw->p, rw->p - rw->o, n + rw->o);
			rw->p += n + rw->o;
		}
		rw->o += n;
	} else
		rw->p += n;
	return rw->p < rw->e;
}

static int
rr(Route *r, Routewalk *rw)
{
	int h;

	if(r == nil)
		return 1;
	if(rr(r->left, rw) == 0)
		return 0;
	if(r->type & Rv4)
		h = V4H(r->v4.address);
	else
		h = V6H(r->v6.address);
	if(h == rw->h){
		if(rr1(rw, r) == 0)
			return 0;
	}
	if(rr(r->mid, rw) == 0)
		return 0;
	return rr(r->right, rw);
}

long
routeread(Fs *f, char *p, ulong offset, int n)
{
	Routewalk rw[1];

	rw->p = p;
	rw->e = p+n;
	rw->o = -offset;
	if(rw->o > 0)
		return 0;

	rlock(&routelock);
	if(rw->p < rw->e) {
		for(rw->h = 0; rw->h < nelem(f->v4root); rw->h++)
			if(rr(f->v4root[rw->h], rw) == 0)
				break;
	}
	if(rw->p < rw->e) {
		for(rw->h = 0; rw->h < nelem(f->v6root); rw->h++)
			if(rr(f->v6root[rw->h], rw) == 0)
				break;
	}
	runlock(&routelock);

	return rw->p - p;
}

/*
 *	4	add	addr	mask	gate
 *	5	add	addr	mask	gate			ifc
 *	6	add	addr	mask	gate				src	smask
 *	7	add	addr	mask	gate			ifc	src	smask
 *	8	add	addr	mask	gate		tag	ifc	src	smask
 *	9	add	addr	mask	gate	type	tag	ifc	src	smask
 *	3	remove	addr	mask
 *	4	remove	addr	mask	gate
 *	5	remove	addr	mask					src	smask
 *	6	remove	addr	mask	gate				src	smask
 *	7	remove	addr	mask	gate			ifc	src	smask
 *	8	remove	addr	mask	gate		tag	ifc	src	smask
 *	9	remove	addr	mask	gate	type	tag	ifc	src	smask
 */
static Route
parseroute(Fs *f, char **argv, int argc)
{
	uchar addr[IPaddrlen], mask[IPaddrlen];
	uchar src[IPaddrlen], smask[IPaddrlen];
	uchar gate[IPaddrlen];
	Ipifc *ifc;
	char *tag;
	int type;

	type = 0;
	tag = nil;
	ifc = nil;
	ipmove(gate, IPnoaddr);
	ipmove(src, IPnoaddr);
	ipmove(smask, IPnoaddr);

	if(argc < 3)
		error(Ebadctl);
	if(parseip(addr, argv[1]) == -1)
		error(Ebadip);
	parseipmask(mask, argv[2]);

	if(strcmp(argv[0], "add") == 0 || (argc > 3 && argc != 5)){
		if(argc < 4)
			error(Ebadctl);
		if(parseip(gate, argv[3]) == -1)
			error(Ebadip);
	}
	if(argc > 4 && (strcmp(argv[0], "add") != 0 || argc != 5)){
		if(parseip(src, argv[argc-2]) == -1)
			error(Ebadip);
		parseipmask(smask, argv[argc-1]);
	}
	if(argc == 5 && strcmp(argv[0], "add") == 0)
		ifc = findipifcstr(f, argv[4]);
	if(argc > 6)
		ifc = findipifcstr(f, argv[argc-3]);
	if(argc > 7)
		tag = argv[argc-4];
	if(argc > 8){
		if((type = parseroutetype(argv[argc-5])) < 0)
			error(Ebadctl);
	} else {
		if(isv4(addr))
			type |= Rv4;
	}
	if(argc > 9)
		error(Ebadctl);

	if(type & Rv4){
		if(!isv4(addr))
			error(Ebadip);
		if(ipcmp(smask, IPnoaddr) != 0 && !isv4(src))
			error(Ebadip);
		if(ipcmp(gate, IPnoaddr) != 0 && !isv4(gate))
			error(Ebadip);
	} else {
		if(isv4(addr))
			error(Ebadip);
	}

	return mkroute(addr, mask, src, smask, gate, type, ifc, tag);	
}

long
routewrite(Fs *f, Chan *c, char *p, int n)
{
	Cmdbuf *cb;
	IPaux *a;
	Route *x, r;

	cb = parsecmd(p, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	if(cb->nf < 1)
		error("short control request");
	if(strcmp(cb->f[0], "flush") == 0){
		char *tag = cb->nf < 2 ? nil : cb->f[1];
		int h;

		wlock(&routelock);
		for(h = 0; h < nelem(f->v4root); h++)
			while((x = looknodetag(f->v4root[h], tag)) != nil){
				memmove(&r, x, sizeof(RouteTree) + sizeof(V4route));
				routerem(f, &r);
			}
		for(h = 0; h < nelem(f->v6root); h++)
			while((x = looknodetag(f->v6root[h], tag)) != nil){
				memmove(&r, x, sizeof(RouteTree) + sizeof(V6route));
				routerem(f, &r);
			}
		wunlock(&routelock);
	} else if(strcmp(cb->f[0], "add") == 0 || strcmp(cb->f[0], "remove") == 0){
		r = parseroute(f, cb->f, cb->nf);
		if(*r.tag == 0){
			a = c->aux;
			strncpy(r.tag, a->tag, sizeof(r.tag));
		}
		wlock(&routelock);
		if(strcmp(cb->f[0], "add") == 0)
			routeadd(f, &r);
		else
			routerem(f, &r);
		wunlock(&routelock);
	} else if(strcmp(cb->f[0], "tag") == 0) {
		if(cb->nf < 2)
			error(Ebadarg);
		a = c->aux;
		c->aux = newipaux(a->owner, cb->f[1]);
		free(a);
	} else
		error(Ebadctl);

	poperror();
	free(cb);
	return n;
}
