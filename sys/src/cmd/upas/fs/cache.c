#include "common.h"
#include <libsec.h>
#include "dat.h"

int
findcache(Mcache *c, Message *m)
{
	int i;

	for(i = 0; i < c->ntab; i++)
		if(c->ctab[i] == m)
			return i;
	return -1;
}

static void
prcache(Mcache *c, char *prefix)
{
	int j;
	Message *m;

	if(!debug)
		return;
	for(j = 0; j < c->ntab; j++){
		m = c->ctab[j];
		dprint("%s%d/%s\t%p\t%d\t%ld\n", prefix, j, m->name, m, m->refs, m->csize);
	}
}

/* debugging only */
static void
dupchk(Mcache *c)
{
	int i, j;

	if(!debug)
		return;
	for(i = 0; i < c->ntab; i++)
		for(j = i + 1; j < c->ntab; j++)
			if(c->ctab[i] == c->ctab[j])
				goto lose;
	return;
lose:
	for(j = 0; j < c->ntab; j++)
		dprint("%d\t%p	%d\t%ld\n", j, c->ctab[j], c->ctab[j]->refs, c->ctab[j]->size);
	abort();
}
	
int
addcache(Mcache *c, Message *m)
{
	int i;

	if((i = findcache(c, m)) < 0){
		if(c->ntab + 1 == nelem(c->ctab))
			abort();
		i = c->ntab++;
	}else{
		/* rotate */
		if(i == c->ntab - 1)
			return i;		/* silly shortcut to prevent excessive printage. */
		dprint("addcache rotate %d %d\n", i, c->ntab);
		prcache(c, "");
		memmove(c->ctab + i, c->ctab + i + 1, (c->ntab - i - 1)*sizeof c->ctab[0]);
		i = c->ntab - 1;
c->ctab[i] = m;
dupchk(c);
	}
	dprint("addcache %d %d 	%p\n", i, c->ntab, m);
	c->ctab[i] = m;
	return i;
}

static void
notecache(Mailbox *mb, Message *m, long sz)
{
	assert(Topmsg(mb, m));
	assert(sz >= 0 && sz < Maxmsg);
	m->csize += sz;
	mb->cached += sz;
	addcache(mb, m);
}

static long
cachefree0(Mailbox *mb, Message *m, int force)
{
	long sz, i;
	Message *s;

	if(!force && !mb->fetch)
		return 0;
	for(s = m->part; s; s = s->next)
		cachefree(mb, s, force);
	dprint("cachefree: %D	%p,	%p\n", m->fileid, m, m->start);
	if(m->mallocd){
		free(m->start);
		m->mallocd = 0;
	}
	if(m->ballocd){
		free(m->body);
		m->ballocd = 0;
	}
	if(m->hallocd){
		free(m->header);
		m->hallocd = 0;
	}
	for(i = 0; i < nelem(m->references); i++){
		free(m->references[i]);
		m->references[i] = 0;
	}
	sz = m->csize;
	m->csize = 0;
	m->start = 0;
	m->end = 0;
	m->header = 0;
	m->hend = 0;
	m->hlen = -1;
	m->body = 0;
	m->bend = 0;
	m->mheader = 0;
	m->mhend = 0;
	if(mb->decache)
		mb->decache(mb, m);
	m->decoded = 0;
	m->converted = 0;
	m->badchars = 0;
	m->cstate &= ~(Cheader|Cbody);
	if(Topmsg(mb, m))
		mb->cached -= sz;
	return sz;
}

long
cachefree(Mailbox *mb, Message *m, int force)
{
	long sz, i;

	sz = cachefree0(mb, m, force);
	for(i = 0; i < mb->ntab; i++)
		if(m == mb->ctab[i]){
			mb->ntab--;
			memmove(mb->ctab + i, mb->ctab + i + 1, sizeof m*mb->ntab - i);
			dupchk(mb);
			break;
		}
	return sz;
}

enum{
	Maxntab	= nelem(mbl->ctab) - 10,
};

vlong
sumcache(Mcache *c)
{
	int i;
	vlong sz;

	sz = 0;
	for(i = 0; i < c->ntab; i++)
		sz += c->ctab[i]->csize;
	return sz;
}

int
scancache(Mcache *c)
{
	int i;

	for(i = 0; i < c->ntab; i++)
		if(c->ctab[i]->csize > Maxmsg)
			return -1;
	return 0;
}

/* debugging only */
static void
chkcsize(Mailbox *mb, vlong sz, vlong sz0)
{
	int j;
	Mcache *c;
	Message *m;

	if(sumcache(mb) == mb->cached)
	if(scancache(mb) == 0)
		return;
	eprint("sz0 %lld sz %lld sum %lld sumca %lld\n", sz0, sz, sumcache(mb), mb->cached);
	eprint("%lld\n", sumcache(mb));
	c = mb;
	for(j = 0; j < c->ntab; j++){
		m = c->ctab[j];
		eprint("%d	%p	%d	%ld	%ld\n", j, m, m->refs, m->csize, m->size);
	}
	abort();
}

/*
 * strategy: start with i = 0. while cache exceeds limits,
 * find j so that all the [i:j] elements have refs == 0.
 * uncache all the [i:j], reduce ntab by i-j.  the tail
 * [j+1:ntab] is shifted to [i:ntab], and finally i = i+1.
 * we may safely skip the new i, since the condition
 * that stopped our scan there still holds.
 */
void
putcache(Mailbox *mb, Message *m)
{
	int i, j, k;
	vlong sz, sz0;
	Message **p;

	p = mb->ctab;
	sz0 = mb->cached;
	dupchk(mb);
	for(i = 0;; i++){
		sz = mb->cached;
		for(j = i;; j++){
			if(j >= mb->ntab ||
			sz < cachetarg && mb->ntab - (j - i) < Maxntab){
				if(j != i)
					break;
chkcsize(mb, sz, sz0);
				return;
			}
			if(p[j]->refs > 0)
				break;
			sz -= p[j]->csize;
		}
		if(sz == mb->cached){
			if(i >= mb->ntab)
				break;
			continue;
		}
		for(k = i; k < j; k++)
			cachefree0(mb, p[k], 0);
		mb->ntab -= j - i;
		memmove(p + i, p + j, (mb->ntab - i)*sizeof *p);
	}
chkcsize(mb, sz, sz0);
	k = 0;
	for(i = 0; i < mb->ntab; i++)
		k += p[i]->refs > 0;
	if((mb->ntab > 1 || k != mb->ntab) && Topmsg(mb, m))
		eprint("cache overflow: %D %llud bytes; %d entries\n", 
			m? m->fileid: 1ll, mb->cached, mb->ntab);
	if(k == mb->ntab)
		return;
	debug = 1; prcache(mb, "");
	abort();
}

static int
squeeze(Message *m, uvlong o, long l, int c)
{
	char *p, *q, *e;
	int n;

	q = memchr(m->start + o, c, l);
	if(q == nil)
		return 0;
	n = 0;
	e = m->start + o + l;
	for(p = q; q < e; q++){
		if(*q == c){
			n++;
			continue;
		}
		*p++ = *q;
	}
	return n;
}

void
msgrealloc(Message *m, ulong l)
{
	long l0, h0, m0, me, b0;

	l0 = m->end - m->start;
	m->mallocd = 1;
	h0 = m->hend - m->start;
	m0 = m->mheader - m->start;
	me = m->mhend - m->start;
	b0 = m->body - m->start;
	assert(h0 >= 0 && m0 >= 0 && me >= 0 && b0 >= 0);
	m->start = erealloc(m->start, l + 1);
	m->rbody = m->start + b0;
	m->rbend = m->end = m->start + l0;
	if(!m->hallocd){
		m->header = m->start;
		m->hend = m->start + h0;
	}
	if(!m->ballocd){
		m->body = m->start + b0;
		m->bend = m->start + l0;
	}
	m->mheader = m->start + m0;
	m->mhend = m->start + me;
}

/*
 * the way we squeeze out bad characters is exceptionally sneaky.
 */
static int
fetch(Mailbox *mb, Message *m, uvlong o, ulong l)
{
	int expand;
	long l0, n, sz0;

top:
	l0 = m->end - m->start;
	assert(l0 >= 0);
	dprint("fetch %lud sz %lud o %llud l %lud badchars %d\n", l0, m->size, o, l, m->badchars);
	assert(m->badchars < Maxmsg/10);
	if(l0 == m->size || o > m->size)
		return 0;
	expand = 0;
	if(o + l > m->size)
		l = m->size - o;
	if(o + l == m->size)
		l += m->ibadchars - m->badchars;
	if(o + l > l0){
		expand = 1;
		msgrealloc(m, o + m->badchars + l);
	}
	assert(l0 <= o);
	sz0 = m->size;
	if(mb->fetch(mb, m, o + m->badchars, l) == -1){
		logmsg(m, "can't fetch %D %llud %lud", m->fileid, o, l);
		m->deleted = Dead;
		return -1;
	}
	if(m->size - sz0)
		l += m->size - sz0;	/* awful botch for gmail */
	if(expand){
		/* grumble.  poor planning. */
		if(m->badchars > 0)
			memmove(m->start + o, m->start + o + m->badchars, l);
		n = squeeze(m, o, l, 0);
		n += squeeze(m, o, l - n, '\r');
		if(n > 0){
			if(m->ibadchars == 0)
				dprint("   %ld more badchars\n", n);
			l -= n;
			m->badchars += n;
			msgrealloc(m, o + l);
		}
		notecache(mb, m, l);
		m->bend = m->rbend = m->end = m->start + o + l;
		if(n)
		if(o + l + n == m->size && m->cstate&Cidx){
			dprint("   redux %llud %ld\n", o + l, n);
			o += l;
			l = n;
			goto top;
		}
	}else
		eprint("unhandled case in fetch\n");
	*m->end = 0;
	return 0;
}

void
cachehash(Mailbox *mb, Message *m)
{
//	fprint(2, "cachehash %P\n", mpair(mb, m));
	if(m->whole == m->whole->whole)
		henter(PATH(mb->id, Qmbox), m->name,
			(Qid){PATH(m->id, Qdir), 0, QTDIR}, m, mb);
	else
		henter(PATH(m->whole->id, Qdir), m->name,
			(Qid){PATH(m->id, Qdir), 0, QTDIR}, m, mb);
	henter(PATH(m->id, Qdir), "xxx",
		(Qid){PATH(m->id, Qmax), 0, QTFILE}, m, mb);	/* sleezy speedup */
}

void
newcachehash(Mailbox *mb, Message *m, int doplumb)
{
	if(doplumb)
		mailplumb(mb, m, 0);
	else
		if(insurecache(mb, m) == 0)
			msgdecref(mb, m);
	/* avoid cachehash on error? */
	cachehash(mb, m);
}

static char *itab[] = {
	"idx",
	"stale",
	"header",
	"body"
};

char*
cstate(Message *m)
{
	char *p, *e;
	int i, s;
	static char buf[64];

	s = m->cstate;
	p = e = buf;
	e += sizeof buf;
	for(i = 0; i < 8; i++)
		if(s & 1<<i)
		if(i < nelem(itab))
			p = seprint(p, e, "%s ", itab[i]);
	if(p > buf)
		p--;
	p[0] = 0;
	return buf;
}


static int
middlecache(Mailbox *mb, Message *m)
{
	int y;

	y = 0;
	while(!Topmsg(mb, m)){
		m = m->whole;
		if((m->cstate & Cbody) == 0)
			y = 1;
	}
	if(y == 0)
		return 0;
	dprint("middlecache %d [%D] %lud %lud\n", m->id, m->fileid, m->end - m->start, m->size);
	return cachebody(mb, m);
}

int
cacheheaders(Mailbox *mb, Message *m)
{
	char *p, *e;
	int r;
	ulong o;

	if(!mb->fetch || m->cstate&Cheader)
		return 0;
	if(!Topmsg(mb, m))
		return middlecache(mb, m);
	dprint("cacheheaders %d %D\n", m->id, m->fileid);
	if(m->size < 10000)
		r = fetch(mb, m, 0, m->size);
	else for(r = 0; (o = m->end - m->start) < m->size; ){
		if((r = fetch(mb, m, o, 4096)) < 0)
			break;
		p = m->start + o;
		if(o)
			p--;
		for(e = m->end - 2; p < e; p++){
			p = memchr(p, '\n', e - p);
			if(p == nil)
				break;
			if(p[1] == '\n' || (p[1] == '\r' && p[2] == '\n'))
				goto found;
		}
	}
	if(r < 0)
		return -1;
found:
	parseheaders(mb, m, mb->addfrom, 0);
	return 0;
}

void
digestmessage(Mailbox *mb, Message *m)
{
	assert(m->digest == 0);
	m->digest = emalloc(SHA1dlen);
	sha1((uchar*)m->start, m->end - m->start, m->digest, nil);
	if(mtreeisdup(mb, m)){
		logmsg(m, "dup detected");
		m->deleted = Dup;	/* no dups allowed */
	}else
		mtreeadd(mb, m);
	dprint("%d %#A\n", m->id, m->digest);
}

int
cachebody(Mailbox *mb, Message *m)
{
	ulong o;

	while(!Topmsg(mb, m))
		m = m->whole;
	if(!mb->fetch || m->cstate&Cbody)
		return 0;
	o = m->end - m->start;
	dprint("cachebody %d [%D] %lud %lud %s\n", m->id, m->fileid, o, m->size, cstate(m));
	if(o < m->size)
	if(fetch(mb, m, o, m->size - o) < 0)
		return -1;
	if((m->cstate&Cidx) == 0){
		assert(m->ibadchars == 0);
		if(m->badchars > 0)
			dprint("reducing size %ld %ld\n", m->size, m->size - m->badchars);
		m->size -= m->badchars;		/* sneaky */
		m->ibadchars = m->badchars;
	}
	if(m->digest == 0)
		digestmessage(mb, m);
	if(m->lines == 0)
		m->lines = countlines(m);
	parse(mb, m, mb->addfrom, 0);
	dprint("  →%s\n", cstate(m));
	return 0;
}

int
cacheidx(Mailbox *mb, Message *m)
{
	if(m->cstate & Cidx)
		return 0;
	if(cachebody(mb, m) == -1)
		return -1;
	m->cstate |= Cidxstale|Cidx;
	return 0;
}

static int
countparts(Message *m)
{
	Message *p;

	if(m->nparts == 0)
		for(p = m->part; p; p = p->next){
			countparts(p);
			m->nparts++;
		}
	return m->nparts;
}

int
insurecache(Mailbox *mb, Message *m)
{
	if(m->deleted || !m->inmbox)
		return -1;
	msgincref(m);
	cacheidx(mb, m);
	if((m->cstate & Cidx) == 0){
		logmsg(m, "%s: can't cache: %s: %r", mb->path, m->name);
		msgdecref(mb, m);
		return -1;
	}
	if(m->digest == 0)
		sysfatal("digest?");
	countparts(m);
	return 0;
}
