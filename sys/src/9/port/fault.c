#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

int
fault(uintptr addr, int read)
{
	Segment *s;
	char *sps;
	int pnd;

	if(up == nil)
		panic("fault: nil up");
	if(up->nlocks){
		Lock *l = up->lastlock;
		print("fault: nlocks %d, proc %lud %s, addr %#p, lock %#p, lpc %#p\n", 
			up->nlocks, up->pid, up->text, addr, l, l ? l->pc : 0);
	}

	pnd = up->notepending;
	sps = up->psstate;
	up->psstate = "Fault";

	m->pfault++;
	for(;;) {
		spllo();

		s = seg(up, addr, 1);		/* leaves s locked if seg != nil */
		if(s == nil) {
			up->psstate = sps;
			return -1;
		}

		if(!read && (s->type&SG_RONLY)) {
			qunlock(s);
			up->psstate = sps;
			return -1;
		}

		if(fixfault(s, addr, read, 1) == 0)
			break;

		splhi();
		switch(up->procctl){
		case Proc_exitme:
		case Proc_exitbig:
			procctl();
		}
	}

	up->psstate = sps;
	up->notepending |= pnd;

	return 0;
}

static void
faulterror(char *s, Chan *c, int isfatal)
{
	char buf[ERRMAX];

	if(c != nil && c->path != nil){
		snprint(buf, sizeof buf, "%s accessing %s: %s", s, c->path->s, up->errstr);
		s = buf;
	}
	if(up->nerrlab) {
		if(isfatal)
			postnote(up, 1, s, NDebug);
		error(s);
	}
	pexit(s, 1);
}

static void
pio(Segment *s, uintptr addr, uintptr soff, Page **p, int isfatal)
{
	Page *new;
	KMap *k;
	Chan *c;
	int n, ask;
	char *kaddr;
	uintptr daddr;
	Page *loadrec;

retry:
	loadrec = *p;
	if(loadrec == nil) {	/* from a text/data image */
		daddr = s->fstart+soff;
		new = lookpage(s->image, daddr);
		if(new != nil) {
			*p = new;
			return;
		}

		c = s->image->c;
		ask = BY2PG;
		if(soff >= s->flen)
			ask = 0;
		else if((soff+ask) > s->flen)
			ask = s->flen-soff;
	}
	else {			/* from a swap image */
		daddr = swapaddr(loadrec);
		new = lookpage(&swapimage, daddr);
		if(new != nil) {
			putswap(loadrec);
			*p = new;
			return;
		}

		c = swapimage.c;
		ask = BY2PG;
	}
	qunlock(s);

	new = newpage(0, 0, addr);
	k = kmap(new);
	kaddr = (char*)VA(k);
	while(waserror()) {
		if(isfatal && strcmp(up->errstr, Eintr) == 0)
			continue;
		kunmap(k);
		putpage(new);
		faulterror(Eioload, c, isfatal);
	}
	n = devtab[c->type]->read(c, kaddr, ask, daddr);
	if(n != ask)
		error(Eshort);
	if(ask < BY2PG)
		memset(kaddr+ask, 0, BY2PG-ask);
	poperror();
	kunmap(k);

	qlock(s);
	if(loadrec == nil) {	/* This is demand load */
		/*
		 *  race, another proc may have gotten here first while
		 *  s was unlocked
		 */
		if(*p == nil) { 
			/*
			 *  check page cache again after i/o to reduce double caching
			 */
			*p = lookpage(s->image, daddr);
			if(*p == nil) {
				incref(new);
				new->daddr = daddr;
				cachepage(new, s->image);
				*p = new;
			}
		}
	}
	else {			/* This is paged out */
		/*
		 *  race, another proc may have gotten here first
		 *  (and the pager may have run on that page) while
		 *  s was unlocked
		 */
		if(*p != loadrec) {
			if(!pagedout(*p)) {
				/* another process did it for me */
				goto done;
			} else if(*p != nil) {
				/* another process and the pager got in */
				putpage(new);
				goto retry;
			} else {
				/* another process segfreed the page */
				incref(new);
				k = kmap(new);
				memset((void*)VA(k), 0, ask);
				kunmap(k);
				*p = new;
				goto done;
			}
		}

		incref(new);
		new->daddr = daddr;
		cachepage(new, &swapimage);
		*p = new;
		putswap(loadrec);
	}
done:
	putpage(new);
	if(s->flushme)
		(*p)->txtflush = ~0;
}

void	(*checkaddr)(uintptr, Segment *, Page *);
uintptr	addr2check;

int
fixfault(Segment *s, uintptr addr, int read, int doputmmu)
{
	int type;
	Pte **p, *etp;
	uintptr soff, mmuphys=0;
	Page **pg, *old, *new;

	addr &= ~(BY2PG-1);
	soff = addr-s->base;
	p = &s->map[soff/PTEMAPMEM];
	if(*p == nil)
		*p = ptealloc();

	etp = *p;
	pg = &etp->pages[(soff&(PTEMAPMEM-1))/BY2PG];
	type = s->type&SG_TYPE;

	if(pg < etp->first)
		etp->first = pg;
	if(pg > etp->last)
		etp->last = pg;

	switch(type) {
	default:
		panic("fault");
		break;

	case SG_TEXT: 			/* Demand load */
		if(pagedout(*pg))
			pio(s, addr, soff, pg, doputmmu);

		mmuphys = PPN((*pg)->pa) | PTERONLY|PTEVALID;
		(*pg)->modref = PG_REF;
		break;

	case SG_BSS:
	case SG_SHARED:			/* Zero fill on demand */
	case SG_STACK:
		if(*pg == nil) {
			new = newpage(1, &s, addr);
			if(s == nil)
				return -1;
			*pg = new;
		}
		goto common;

	case SG_DATA:
	common:			/* Demand load/pagein/copy on write */
		if(pagedout(*pg))
			pio(s, addr, soff, pg, doputmmu);

		/*
		 *  It's only possible to copy on write if
		 *  we're the only user of the segment.
		 */
		if(read && conf.copymode == 0 && s->ref == 1) {
			mmuphys = PPN((*pg)->pa)|PTERONLY|PTEVALID;
			(*pg)->modref |= PG_REF;
			break;
		}

		old = *pg;
		if(old->image == &swapimage && (old->ref + swapcount(old->daddr)) == 1)
			uncachepage(old);
		if(old->ref > 1 || old->image != nil) {
			new = newpage(0, &s, addr);
			if(s == nil)
				return -1;
			if(s->flushme)
				new->txtflush = ~0;
			*pg = new;
			copypage(old, *pg);
			putpage(old);
		}
		mmuphys = PPN((*pg)->pa) | PTEWRITE | PTEVALID;
		(*pg)->modref = PG_MOD|PG_REF;
		break;

	case SG_PHYSICAL:
		if(*pg == nil){
			new = smalloc(sizeof(Page));
			new->va = addr;
			new->pa = s->pseg->pa+(addr-s->base);
			new->ref = 1;
			*pg = new;
		}
		if (checkaddr && addr == addr2check)
			(*checkaddr)(addr, s, *pg);
		mmuphys = PPN((*pg)->pa) |PTEWRITE|PTEUNCACHED|PTEVALID;
		(*pg)->modref = PG_MOD|PG_REF;
		break;
	}
	qunlock(s);

	if(doputmmu)
		putmmu(addr, mmuphys, *pg);

	return 0;
}

/*
 * Called only in a system call
 */
int
okaddr(uintptr addr, ulong len, int write)
{
	Segment *s;

	if((long)len >= 0) {
		for(;;) {
			s = seg(up, addr, 0);
			if(s == nil || (write && (s->type&SG_RONLY)))
				break;

			if(addr+len > s->top) {
				len -= s->top - addr;
				addr = s->top;
				continue;
			}
			return 1;
		}
	}
	return 0;
}

void
validaddr(uintptr addr, ulong len, int write)
{
	if(!okaddr(addr, len, write)){
		pprint("suicide: invalid address %#p/%lud in sys call pc=%#p\n", addr, len, userpc());
		postnote(up, 1, "sys: bad address in syscall", NDebug);
		error(Ebadarg);
	}
}

/*
 * &s[0] is known to be a valid address.
 */
void*
vmemchr(void *s, int c, int n)
{
	int m;
	uintptr a;
	void *t;

	a = (uintptr)s;
	while(PGROUND(a) != PGROUND(a+n-1)){
		/* spans pages; handle this page */
		m = BY2PG - (a & (BY2PG-1));
		t = memchr((void*)a, c, m);
		if(t)
			return t;
		a += m;
		n -= m;
		if(a < KZERO)
			validaddr(a, 1, 0);
	}

	/* fits in one page */
	return memchr((void*)a, c, n);
}

Segment*
seg(Proc *p, uintptr addr, int dolock)
{
	Segment **s, **et, *n;

	et = &p->seg[NSEG];
	for(s = p->seg; s < et; s++) {
		if((n = *s) == nil)
			continue;
		if(addr >= n->base && addr < n->top) {
			if(dolock == 0)
				return n;

			qlock(n);
			if(addr >= n->base && addr < n->top)
				return n;
			qunlock(n);
		}
	}

	return nil;
}

extern void checkmmu(uintptr, uintptr);

void
checkpages(void)
{
	uintptr addr, off;
	Pte *p;
	Page *pg;
	Segment **sp, **ep, *s;
	
	if(up == nil)
		return;

	for(sp=up->seg, ep=&up->seg[NSEG]; sp<ep; sp++){
		if((s = *sp) == nil)
			continue;
		qlock(s);
		for(addr=s->base; addr<s->top; addr+=BY2PG){
			off = addr - s->base;
			if((p = s->map[off/PTEMAPMEM]) == nil)
				continue;
			pg = p->pages[(off&(PTEMAPMEM-1))/BY2PG];
			if(pagedout(pg))
				continue;
			checkmmu(addr, pg->pa);
		}
		qunlock(s);
	}
}
