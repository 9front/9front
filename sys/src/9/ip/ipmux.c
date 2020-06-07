/*
 * IP packet filter
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "ip.h"
#include "ipv6.h"

typedef struct Ipmuxrock  Ipmuxrock;
typedef struct Ipmux      Ipmux;

enum
{
	Tver,
	Tproto,
	Tdata,
	Tiph,
	Tdst,
	Tsrc,
	Tifc,
};

/*
 *  a node in the decision tree
 */
struct Ipmux
{
	Ipmux	*yes;
	Ipmux	*no;
	uchar	type;		/* type of field(Txxxx) */
	uchar	len;		/* length in bytes of item to compare */
	uchar	n;		/* number of items val points to */
	int	off;		/* offset of comparison */
	uchar	*val;
	uchar	*mask;
	uchar	*e;		/* val+n*len*/
	int	ref;		/* so we can garbage collect */
	Conv	*conv;
};

/*
 *  someplace to hold per conversation data
 */
struct Ipmuxrock
{
	Ipmux	*chain;
};

static int	ipmuxsprint(Ipmux*, int, char*, int);
static void	ipmuxkick(void *x);
static void	ipmuxfree(Ipmux *f);

static char*
skipwhite(char *p)
{
	while(*p == ' ' || *p == '\t')
		p++;
	return p;
}

static char*
follows(char *p, char c)
{
	char *f;

	f = strchr(p, c);
	if(f == nil)
		return nil;
	*f++ = 0;
	f = skipwhite(f);
	if(*f == 0)
		return nil;
	return f;
}

static Ipmux*
parseop(char **pp)
{
	char *p = *pp;
	int type, off, end, len;
	Ipmux *f;

	p = skipwhite(p);
	if(strncmp(p, "ver", 3) == 0){
		type = Tver;
		off = 0;
		len = 1;
		p += 3;
	}
	else if(strncmp(p, "dst", 3) == 0){
		type = Tdst;
		off = offsetof(Ip6hdr, dst[0]);
		len = IPaddrlen;
		p += 3;
	}
	else if(strncmp(p, "src", 3) == 0){
		type = Tsrc;
		off = offsetof(Ip6hdr, src[0]);
		len = IPaddrlen;
		p += 3;
	}
	else if(strncmp(p, "ifc", 3) == 0){
		type = Tifc;
		off = -IPaddrlen;
		len = IPaddrlen;
		p += 3;
	}
	else if(strncmp(p, "proto", 5) == 0){
		type = Tproto;
		off = offsetof(Ip6hdr, proto);
		len = 1;
		p += 5;
	}
	else if(strncmp(p, "data", 4) == 0 || strncmp(p, "iph", 3) == 0){
		if(strncmp(p, "data", 4) == 0) {
			type = Tdata;
			p += 4;
		}
		else {
			type = Tiph;
			p += 3;
		}
		p = skipwhite(p);
		if(*p != '[')
			return nil;
		p++;
		off = strtoul(p, &p, 0);
		if(off < 0)
			return nil;
		p = skipwhite(p);
		if(*p != ':')
			end = off;
		else {
			p++;
			p = skipwhite(p);
			end = strtoul(p, &p, 0);
			if(end < off)
				return nil;
			p = skipwhite(p);
		}
		if(*p != ']')
			return nil;
		p++;
		len = end - off + 1;
	}
	else
		return nil;

	f = smalloc(sizeof(*f));
	f->type = type;
	f->len = len;
	f->off = off;
	f->val = nil;
	f->mask = nil;
	f->n = 1;
	f->ref = 1;
	return f;	
}

static int
htoi(char x)
{
	if(x >= '0' && x <= '9')
		x -= '0';
	else if(x >= 'a' && x <= 'f')
		x -= 'a' - 10;
	else if(x >= 'A' && x <= 'F')
		x -= 'A' - 10;
	else
		x = 0;
	return x;
}

static int
hextoi(char *p)
{
	return (htoi(p[0])<<4) | htoi(p[1]);
}

static void
parseval(uchar *v, char *p, int len)
{
	while(*p && len-- > 0){
		*v++ = hextoi(p);
		p += 2;
	}
}

static Ipmux*
parsemux(char *p)
{
	int n;
	Ipmux *f;
	char *val;
	char *mask;
	char *vals[20];
	uchar *v;

	/* parse operand */
	f = parseop(&p);
	if(f == nil)
		return nil;

	/* find value */
	val = follows(p, '=');
	if(val == nil)
		goto parseerror;

	/* parse mask */
	mask = follows(p, '&');
	if(mask != nil){
		switch(f->type){
		case Tsrc:
		case Tdst:
		case Tifc:
			f->mask = smalloc(f->len);
			parseipmask(f->mask, mask, 0);
			break;
		case Tdata:
		case Tiph:
			f->mask = smalloc(f->len);
			parseval(f->mask, mask, f->len);
			break;
		default:
			goto parseerror;
		}
	} else if(f->type == Tver){
		f->mask = smalloc(f->len);
		f->mask[0] = 0xF0;
	}

	/* parse vals */
	f->n = getfields(val, vals, nelem(vals), 1, "|");
	if(f->n == 0)
		goto parseerror;
	f->val = smalloc(f->n*f->len);
	v = f->val;
	for(n = 0; n < f->n; n++){
		switch(f->type){
		case Tver:
			if(f->n != 1)
				goto parseerror;
			if(strcmp(vals[n], "6") == 0)
				*v = IP_VER6;
			else if(strcmp(vals[n], "4") == 0)
				*v = IP_VER4;
			else
				goto parseerror;
			break;
		case Tsrc:
		case Tdst:
		case Tifc:
			if(parseip(v, vals[n]) == -1)
				goto parseerror;
			break;
		case Tproto:
		case Tdata:
		case Tiph:
			parseval(v, vals[n], f->len);
			break;
		}
		v += f->len;
	}
	f->e = f->val + f->n*f->len;
	return f;

parseerror:
	ipmuxfree(f);
	return nil;
}

/*
 *  Compare relative ordering of two ipmuxs.  This doesn't compare the
 *  values, just the fields being looked at.  
 *
 *  returns:	<0 if a is a more specific match
 *		 0 if a and b are matching on the same fields
 *		>0 if b is a more specific match
 */
static int
ipmuxcmp(Ipmux *a, Ipmux *b)
{
	int n;

	/* compare types, lesser ones are more important */
	n = a->type - b->type;
	if(n != 0)
		return n;

	/* compare offsets, call earlier ones more specific */
	n = a->off - b->off;
	if(n != 0)
		return n;

	/* compare match lengths, longer ones are more specific */
	n = b->len - a->len;
	if(n != 0)
		return n;

	/*
	 *  if we get here we have two entries matching
	 *  the same bytes of the record.  Now check
	 *  the mask for equality.  Longer masks are
	 *  more specific.
	 */
	if(a->mask != nil && b->mask == nil)
		return -1;
	if(a->mask == nil && b->mask != nil)
		return 1;
	if(a->mask != nil && b->mask != nil){
		n = memcmp(b->mask, a->mask, a->len);
		if(n != 0)
			return n;
	}
	return 0;
}

/*
 *  Compare the values of two ipmuxs.  We're assuming that ipmuxcmp
 *  returned 0 comparing them.
 */
static int
ipmuxvalcmp(Ipmux *a, Ipmux *b)
{
	int n;

	n = b->len*b->n - a->len*a->n;
	if(n != 0)
		return n;
	return memcmp(a->val, b->val, a->len*a->n);
} 

/*
 *  add onto an existing ipmux chain in the canonical comparison
 *  order
 */
static void
ipmuxchain(Ipmux **l, Ipmux *f)
{
	for(; *l; l = &(*l)->yes)
		if(ipmuxcmp(f, *l) < 0)
			break;
	f->yes = *l;
	*l = f;
}

/*
 *  copy a tree
 */
static Ipmux*
ipmuxcopy(Ipmux *f)
{
	Ipmux *nf;

	if(f == nil)
		return nil;
	nf = smalloc(sizeof *nf);
	*nf = *f;
	nf->no = ipmuxcopy(f->no);
	nf->yes = ipmuxcopy(f->yes);
	if(f->mask != nil){
		nf->mask = smalloc(f->len);
		memmove(nf->mask, f->mask, f->len);
	}
	nf->val = smalloc(f->n*f->len);
	nf->e = nf->val + f->len*f->n;
	memmove(nf->val, f->val, f->n*f->len);
	return nf;
}

static void
ipmuxfree(Ipmux *f)
{
	if(f == nil)
		return;
	free(f->val);
	free(f->mask);
	free(f);
}

static void
ipmuxtreefree(Ipmux *f)
{
	if(f == nil)
		return;
	ipmuxfree(f->no);
	ipmuxfree(f->yes);
	ipmuxfree(f);
}

/*
 *  merge two trees
 */
static Ipmux*
ipmuxmerge(Ipmux *a, Ipmux *b)
{
	int n;
	Ipmux *f;

	if(a == nil)
		return b;
	if(b == nil)
		return a;
	n = ipmuxcmp(a, b);
	if(n < 0){
		f = ipmuxcopy(b);
		a->yes = ipmuxmerge(a->yes, b);
		a->no = ipmuxmerge(a->no, f);
		return a;
	}
	if(n > 0){
		f = ipmuxcopy(a);
		b->yes = ipmuxmerge(b->yes, a);
		b->no = ipmuxmerge(b->no, f);
		return b;
	}
	if(ipmuxvalcmp(a, b) == 0){
		a->yes = ipmuxmerge(a->yes, b->yes);
		a->no = ipmuxmerge(a->no, b->no);
		a->ref++;
		ipmuxfree(b);
		return a;
	}
	a->no = ipmuxmerge(a->no, b);
	return a;
}

/*
 *  remove a chain from a demux tree.  This is like merging accept that
 *  we remove instead of insert.
 */
static int
ipmuxremove(Ipmux **l, Ipmux *f)
{
	int n, rv;
	Ipmux *ft;

	if(f == nil)
		return 0;		/* we've removed it all */
	if(*l == nil)
		return -1;

	ft = *l;
	n = ipmuxcmp(ft, f);
	if(n < 0){
		/* *l is maching an earlier field, descend both paths */
		rv = ipmuxremove(&ft->yes, f);
		rv += ipmuxremove(&ft->no, f);
		return rv;
	}
	if(n > 0){
		/* f represents an earlier field than *l, this should be impossible */
		return -1;
	}

	/* if we get here f and *l are comparing the same fields */
	if(ipmuxvalcmp(ft, f) != 0){
		/* different values mean mutually exclusive */
		return ipmuxremove(&ft->no, f);
	}

	ipmuxremove(&ft->no, f->no);

	/* we found a match */
	if(--(ft->ref) == 0){
		/*
		 *  a dead node implies the whole yes side is also dead.
		 *  since our chain is constrained to be on that side,
		 *  we're done.
		 */
		ipmuxtreefree(ft->yes);
		*l = ft->no;
		ipmuxfree(ft);
		return 0;
	}

	/*
	 *  free the rest of the chain.  it is constrained to match the
	 *  yes side.
	 */
	return ipmuxremove(&ft->yes, f->yes);
}

/*
 * convert to ipv4 filter
 */
static Ipmux*
ipmuxconv4(Ipmux *f)
{
	int i, n;

	if(f == nil)
		return nil;

	switch(f->type){
	case Tproto:
		f->off = offsetof(Ip4hdr, proto);
		break;
	case Tdst:
		f->off = offsetof(Ip4hdr, dst[0]);
		if(0){
	case Tsrc:
		f->off = offsetof(Ip4hdr, src[0]);
		}
		if(f->len != IPaddrlen)
			break;
		n = 0;
		for(i = 0; i < f->n; i++){
			if(isv4(f->val + i*IPaddrlen)){
				memmove(f->val + n*IPv4addrlen, f->val + i*IPaddrlen + IPv4off, IPv4addrlen);
				n++;
			}
		}
		if(n == 0){
			ipmuxtreefree(f);
			return nil;
		}
		f->n = n;
		f->len = IPv4addrlen;
		if(f->mask != nil)
			memmove(f->mask, f->mask+IPv4off, IPv4addrlen);
	}
	f->e = f->val + f->n*f->len;

	f->yes = ipmuxconv4(f->yes);
	f->no = ipmuxconv4(f->no);

	return f;
}

/*
 *  connection request is a semi separated list of filters
 *  e.g. ver=4;proto=17;data[0:4]=11aa22bb;ifc=135.104.9.2&255.255.255.0
 *
 *  there's no protection against overlapping specs.
 */
static char*
ipmuxconnect(Conv *c, char **argv, int argc)
{
	int i, n;
	char *field[10];
	Ipmux *mux, *chain;
	Ipmuxrock *r;
	Fs *f;

	f = c->p->f;

	if(argc != 2)
		return Ebadarg;

	n = getfields(argv[1], field, nelem(field), 1, ";");
	if(n <= 0)
		return Ebadarg;

	chain = nil;
	mux = nil;
	for(i = 0; i < n; i++){
		mux = parsemux(field[i]);
		if(mux == nil){
			ipmuxtreefree(chain);
			return Ebadarg;
		}
		ipmuxchain(&chain, mux);
	}
	if(chain == nil)
		return Ebadarg;
	mux->conv = c;

	if(chain->type != Tver) {
		char ver6[] = "ver=6";
		mux = parsemux(ver6);
		mux->yes = chain;
		mux->no = ipmuxcopy(chain);
		chain = mux;
	}
	if(*chain->val == IP_VER4)
		chain->yes = ipmuxconv4(chain->yes);
	else
		chain->no = ipmuxconv4(chain->no);

	/* save a copy of the chain so we can later remove it */
	mux = ipmuxcopy(chain);
	r = (Ipmuxrock*)(c->ptcl);
	r->chain = chain;

	/* add the chain to the protocol demultiplexor tree */
	wlock(f);
	f->ipmux->priv = ipmuxmerge(f->ipmux->priv, mux);
	wunlock(f);

	Fsconnected(c, nil);
	return nil;
}

static int
ipmuxstate(Conv *c, char *state, int n)
{
	Ipmuxrock *r;
	
	r = (Ipmuxrock*)(c->ptcl);
	return ipmuxsprint(r->chain, 0, state, n);
}

static void
ipmuxcreate(Conv *c)
{
	Ipmuxrock *r;

	c->rq = qopen(64*1024, Qmsg, 0, c);
	c->wq = qopen(64*1024, Qkick, ipmuxkick, c);
	r = (Ipmuxrock*)(c->ptcl);
	r->chain = nil;
}

static char*
ipmuxannounce(Conv*, char**, int)
{
	return "ipmux does not support announce";
}

static void
ipmuxclose(Conv *c)
{
	Ipmuxrock *r;
	Fs *f = c->p->f;

	r = (Ipmuxrock*)(c->ptcl);

	qclose(c->rq);
	qclose(c->wq);
	qclose(c->eq);
	ipmove(c->laddr, IPnoaddr);
	ipmove(c->raddr, IPnoaddr);
	c->lport = 0;
	c->rport = 0;

	wlock(f);
	ipmuxremove(&(c->p->priv), r->chain);
	wunlock(f);
	ipmuxtreefree(r->chain);
	r->chain = nil;
}

/*
 *  takes a fully formed ip packet and just passes it down
 *  the stack
 */
static void
ipmuxkick(void *x)
{
	Conv *c = x;
	Block *bp;

	bp = qget(c->wq);
	if(bp != nil) {
		Ip4hdr *ih4 = (Ip4hdr*)(bp->rp);

		if((ih4->vihl & 0xF0) != IP_VER6)
			ipoput4(c->p->f, bp, 0, ih4->ttl, ih4->tos, nil);
		else
			ipoput6(c->p->f, bp, 0, ((Ip6hdr*)ih4)->ttl, 0, nil);
	}
}

static int
maskmemcmp(uchar *m, uchar *v, uchar *c, int n)
{
	int i;

	if(m == nil)
		return memcmp(v, c, n) != 0;

	for(i = 0; i < n; i++)
		if((v[i] & m[i]) != c[i])
			return 1;
	return 0;
}

static void
ipmuxiput(Proto *p, Ipifc *ifc, Block *bp)
{
	Fs *f = p->f;
	Conv *c;
	Iplifc *lifc;
	Ipmux *mux;
	uchar *v;
	Ip4hdr *ip4;
	Ip6hdr *ip6;
	int off, hl;

	ip4 = (Ip4hdr*)bp->rp;
	if((ip4->vihl & 0xF0) == IP_VER4) {
		hl = (ip4->vihl&0x0F)<<2;
		ip6 = nil;
	} else {
		hl = IP6HDR;
		ip6 = (Ip6hdr*)ip4;
	}

	if(p->priv == nil)
		goto nomatch;

	c = nil;
	lifc = nil;

	/* run the filter */
	rlock(f);
	mux = f->ipmux->priv;
	while(mux != nil){
		switch(mux->type){
		case Tifc:
			if(mux->len != IPaddrlen)
				goto no;
			for(lifc = ifc->lifc; lifc != nil; lifc = lifc->next)
				for(v = mux->val; v < mux->e; v += IPaddrlen)
					if(maskmemcmp(mux->mask, lifc->local, v, IPaddrlen) == 0)
						goto yes;
			goto no;
		case Tdata:
			off = hl;
			break;
		default:
			off = 0;
			break;
		}
		off += mux->off;
		if(off < 0 || off + mux->len > BLEN(bp))
			goto no;
		for(v = mux->val; v < mux->e; v += mux->len)
			if(maskmemcmp(mux->mask, bp->rp + off, v, mux->len) == 0)
				goto yes;
no:
		mux = mux->no;
		continue;
yes:
		if(mux->conv != nil)
			c = mux->conv;
		mux = mux->yes;
	}
	runlock(f);

	if(c != nil){
		/* tack on interface address */
		bp = padblock(bp, IPaddrlen);
		if(lifc == nil)
			lifc = ifc->lifc;
		ipmove(bp->rp, lifc != nil ? lifc->local : IPnoaddr);
		qpass(c->rq, concatblock(bp));
		return;
	}

nomatch:
	/* doesn't match any filter, hand it to the specific protocol handler */
	if(ip6 != nil)
		p = f->t2p[ip6->proto];
	else
		p = f->t2p[ip4->proto];
	if(p != nil && p->rcv != nil){
		(*p->rcv)(p, ifc, bp);
		return;
	}
	freeblist(bp);
}

static int
ipmuxsprint(Ipmux *mux, int level, char *buf, int len)
{
	int i, j, n;
	uchar *v;

	n = 0;
	for(i = 0; i < level; i++)
		n += snprint(buf+n, len-n, " ");
	if(mux == nil){
		n += snprint(buf+n, len-n, "\n");
		return n;
	}
	n += snprint(buf+n, len-n, "%s[%d:%d]", 
		mux->type == Tdata ? "data": "iph",
		mux->off, mux->off+mux->len-1);
	if(mux->mask != nil){
		n += snprint(buf+n, len-n, "&");
		for(i = 0; i < mux->len; i++)
			n += snprint(buf+n, len - n, "%2.2ux", mux->mask[i]);
	}
	n += snprint(buf+n, len-n, "=");
	v = mux->val;
	for(j = 0; j < mux->n; j++){
		for(i = 0; i < mux->len; i++)
			n += snprint(buf+n, len - n, "%2.2ux", *v++);
		n += snprint(buf+n, len-n, "|");
	}
	n += snprint(buf+n, len-n, "\n");
	level++;
	n += ipmuxsprint(mux->no, level, buf+n, len-n);
	n += ipmuxsprint(mux->yes, level, buf+n, len-n);
	return n;
}

static int
ipmuxstats(Proto *p, char *buf, int len)
{
	int n;
	Fs *f = p->f;

	rlock(f);
	n = ipmuxsprint(p->priv, 0, buf, len);
	runlock(f);

	return n;
}

void
ipmuxinit(Fs *f)
{
	Proto *ipmux;

	ipmux = smalloc(sizeof(Proto));
	ipmux->priv = nil;
	ipmux->name = "ipmux";
	ipmux->connect = ipmuxconnect;
	ipmux->announce = ipmuxannounce;
	ipmux->state = ipmuxstate;
	ipmux->create = ipmuxcreate;
	ipmux->close = ipmuxclose;
	ipmux->rcv = ipmuxiput;
	ipmux->ctl = nil;
	ipmux->advise = nil;
	ipmux->stats = ipmuxstats;
	ipmux->ipproto = -1;
	ipmux->nc = 64;
	ipmux->ptclsize = sizeof(Ipmuxrock);

	f->ipmux = ipmux;			/* hack for Fsrcvpcol */

	Fsproto(f, ipmux);
}
