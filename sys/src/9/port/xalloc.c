#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

enum
{
	Nhole		= 128,
	Magichole	= 0x484F4C45,			/* HOLE */
};

typedef struct Hole Hole;
typedef struct Xalloc Xalloc;
typedef struct Xhdr Xhdr;

struct Hole
{
	uintptr	addr;
	uintptr	size;
	uintptr	top;
	Hole*	link;
};

struct Xhdr
{
	ulong	size;
	ulong	magix;
	char	data[];
};

struct Xalloc
{
	Lock;
	Hole	hole[Nhole];
	Hole*	flist;
	Hole*	table;
};

static Xalloc	xlists;

void
xinit(void)
{
	ulong maxpages, kpages, n;
	uintptr size;
	Confmem *m;
	Pallocmem *pm;
	Hole *h, *eh;
	int i;

	eh = &xlists.hole[Nhole-1];
	for(h = xlists.hole; h < eh; h++)
		h->link = h+1;

	xlists.flist = xlists.hole;

	kpages = conf.npage - conf.upages;

	pm = palloc.mem;
	for(i=0; i<nelem(conf.mem); i++){
		m = &conf.mem[i];
		n = m->npage;
		if(n > kpages)
			n = kpages;
		/* don't try to use non-KADDR-able memory for kernel */
		maxpages = cankaddr(m->base)/BY2PG;
		if(n > maxpages)
			n = maxpages;
		size = (uintptr)n*BY2PG;
		/* first give to kernel */
		if(n > 0){
			m->kbase = (uintptr)KADDR(m->base);
			m->klimit = (uintptr)m->kbase+size;
			if(m->klimit == 0)
				m->klimit = (uintptr)-BY2PG;
			xhole(m->base, m->klimit - m->kbase);
			kpages -= n;
		}
		/* if anything left over, give to user */
		if(n < m->npage){
			if(pm >= palloc.mem+nelem(palloc.mem)){
				print("xinit: losing %lud pages\n", m->npage-n);
				continue;
			}
			pm->base = m->base+size;
			pm->npage = m->npage - n;
			pm++;
		}
	}
	xsummary();
}

void*
xspanalloc(ulong size, int align, ulong span)
{
	uintptr a, v, t;

	a = (uintptr)xalloc(size+align+span);
	if(a == 0)
		panic("xspanalloc: %lud %d %lux", size, align, span);

	if(span > 2) {
		v = (a + span) & ~((uintptr)span-1);
		t = v - a;
		if(t > 0)
			xhole(PADDR(a), t);
		t = a + span - v;
		if(t > 0)
			xhole(PADDR(v+size+align), t);
	}
	else
		v = a;

	if(align > 1)
		v = (v + align) & ~((uintptr)align-1);

	return (void*)v;
}

void*
xallocz(ulong size, int zero)
{
	Xhdr *p;
	Hole *h, **l;

	/* add room for magix & size overhead, round up to nearest vlong */
	size += BY2V + offsetof(Xhdr, data[0]);
	size &= ~(BY2V-1);

	ilock(&xlists);
	l = &xlists.table;
	for(h = *l; h; h = h->link) {
		if(h->size >= size) {
			p = (Xhdr*)KADDR(h->addr);
			h->addr += size;
			h->size -= size;
			if(h->size == 0) {
				*l = h->link;
				h->link = xlists.flist;
				xlists.flist = h;
			}
			iunlock(&xlists);
			if(zero)
				memset(p, 0, size);
			p->magix = Magichole;
			p->size = size;
			return p->data;
		}
		l = &h->link;
	}
	iunlock(&xlists);
	return nil;
}

void*
xalloc(ulong size)
{
	return xallocz(size, 1);
}

void
xfree(void *p)
{
	Xhdr *x;

	x = (Xhdr*)((uintptr)p - offsetof(Xhdr, data[0]));
	if(x->magix != Magichole) {
		xsummary();
		panic("xfree(%#p) %#ux != %#lux", p, Magichole, x->magix);
	}
	xhole(PADDR((uintptr)x), x->size);
}

int
xmerge(void *vp, void *vq)
{
	Xhdr *p, *q;

	p = (Xhdr*)(((uintptr)vp - offsetof(Xhdr, data[0])));
	q = (Xhdr*)(((uintptr)vq - offsetof(Xhdr, data[0])));
	if(p->magix != Magichole || q->magix != Magichole) {
		int i;
		ulong *wd;
		void *badp;

		xsummary();
		badp = (p->magix != Magichole? p: q);
		wd = (ulong *)badp - 12;
		for (i = 24; i-- > 0; ) {
			print("%#p: %lux", wd, *wd);
			if (wd == badp)
				print(" <-");
			print("\n");
			wd++;
		}
		panic("xmerge(%#p, %#p) bad magic %#lux, %#lux",
			vp, vq, p->magix, q->magix);
	}
	if((uchar*)p+p->size == (uchar*)q) {
		p->size += q->size;
		return 1;
	}
	return 0;
}

void
xhole(uintptr addr, uintptr size)
{
	Hole *h, *c, **l;
	uintptr top;

	if(size == 0)
		return;

	top = addr + size;
	ilock(&xlists);
	l = &xlists.table;
	for(h = *l; h; h = h->link) {
		if(h->top == addr) {
			h->size += size;
			h->top = h->addr+h->size;
			c = h->link;
			if(c && h->top == c->addr) {
				h->top += c->size;
				h->size += c->size;
				h->link = c->link;
				c->link = xlists.flist;
				xlists.flist = c;
			}
			iunlock(&xlists);
			return;
		}
		if(h->addr > addr)
			break;
		l = &h->link;
	}
	if(h && top == h->addr) {
		h->addr -= size;
		h->size += size;
		iunlock(&xlists);
		return;
	}

	if(xlists.flist == nil) {
		iunlock(&xlists);
		print("xfree: no free holes, leaked %llud bytes\n", (uvlong)size);
		return;
	}

	h = xlists.flist;
	xlists.flist = h->link;
	h->addr = addr;
	h->top = top;
	h->size = size;
	h->link = *l;
	*l = h;
	iunlock(&xlists);
}

void
xsummary(void)
{
	int i;
	Hole *h;
	uintptr s;

	i = 0;
	for(h = xlists.flist; h; h = h->link)
		i++;
	print("%d holes free\n", i);

	s = 0;
	for(h = xlists.table; h; h = h->link) {
		print("%#8.8p %#8.8p %llud\n", h->addr, h->top, (uvlong)h->size);
		s += h->size;
	}
	print("%llud bytes free\n", (uvlong)s);
}
