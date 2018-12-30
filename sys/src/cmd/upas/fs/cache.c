#include "common.h"
#include <libsec.h>
#include "dat.h"

static void
addlru(Mcache *c, Message *m)
{
	Message *l, **ll;

	if((m->cstate & (Cheader|Cbody)) == 0)
		return;

	c->nlru++;
	ll = &c->lru;
	while((l = *ll) != nil){
		if(l == m){
			c->nlru--;
			*ll = m->lru;
		} else {
			ll = &l->lru;
		}
	}
	m->lru = nil;
	*ll = m;
}

static void
notecache(Mailbox *mb, Message *m, long sz)
{
	assert(Topmsg(mb, m));
	assert(sz >= 0 && sz <= Maxmsg);
	m->csize += sz;
	mb->cached += sz;
	addlru(mb, m);
}

void
cachefree(Mailbox *mb, Message *m, int force)
{
	long i;
	Message *s, **ll;

	if(Topmsg(mb, m)){
		for(ll = &mb->lru; *ll != nil; ll = &((*ll)->lru)){
			if(*ll == m){
				mb->nlru--;
				*ll = m->lru;
				m->lru = nil;
				break;
			}
		}
		if(mb->decache)
			mb->decache(mb, m);
		mb->cached -= m->csize;
	}
	for(s = m->part; s; s = s->next)
		cachefree(mb, s, force);
	if(!force && mb->fetch == nil)
		return;
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
	m->decoded = 0;
	m->converted = 0;
	m->badchars = 0;
	m->cstate &= ~(Cheader|Cbody);
}

void
putcache(Mailbox *mb, Message *m)
{
	int n;

	while(!Topmsg(mb, m)) m = m->whole;
	addlru(mb, m);
	while(mb->lru != nil && (mb->cached > cachetarg || mb->nlru > 10)){
		n = 0;
		while(mb->lru->refs > 0){
			if(++n >= mb->nlru)
				return;
			addlru(mb, mb->lru);
		}
		cachefree(mb, mb->lru, 0);
	}
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
	assert(mb->refs >= 0);
	if(mb->refs == 0)
		return;
	if(m->whole == m->whole->whole)
		henter(PATH(mb->id, Qmbox), m->name,
			(Qid){PATH(m->id, Qdir), 0, QTDIR}, m, mb);
	else
		henter(PATH(m->whole->id, Qdir), m->name,
			(Qid){PATH(m->id, Qdir), 0, QTDIR}, m, mb);
	henter(PATH(m->id, Qdir), "xxx",
		(Qid){PATH(m->id, Qmax), 0, QTFILE}, m, mb);	/* sleezy speedup */
}

static char *itab[] = {
	"idx",
	"stale",
	"header",
	"body",
	"new",
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
	dprint("middlecache %lud [%D] %lud %lud\n",
		m->id, m->fileid, (ulong)(m->end - m->start), m->size);
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
	dprint("cacheheaders %lud %D\n", m->id, m->fileid);
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
	assert(m->digest == nil);
	m->digest = emalloc(SHA1dlen);
	sha1((uchar*)m->start, m->end - m->start, m->digest, nil);
	if(mtreeisdup(mb, m)){
		logmsg(m, "dup detected");
		m->deleted = Dup;	/* no dups allowed */
	}else
		mtreeadd(mb, m);
	dprint("%lud %#A\n", m->id, m->digest);
}

int
cachebody(Mailbox *mb, Message *m)
{
	ulong o;

	while(!Topmsg(mb, m))
		m = m->whole;
	if(mb->fetch == nil || m->cstate&Cbody)
		return 0;
	o = m->end - m->start;
	dprint("cachebody %lud [%D] %lud %lud %s", m->id, m->fileid, o, m->size, cstate(m));
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
	if(m->digest == nil)
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
	if(cachebody(mb, m) < 0)
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
	if((m->deleted & ~Deleted) != 0 || !m->inmbox)
		return -1;
	msgincref(mb, m);
	cacheidx(mb, m);
	if((m->cstate & Cidx) == 0){
		logmsg(m, "%s: can't cache: %s: %r", mb->path, m->name);
		msgdecref(mb, m);
		return -1;
	}
	if(m->digest == nil)
		sysfatal("digest?");
	countparts(m);
	return 0;
}
