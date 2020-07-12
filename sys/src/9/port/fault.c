#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

static void
faulterror(char *s, Chan *c)
{
	char buf[ERRMAX];

	if(c != nil)
		snprint(buf, sizeof buf, "sys: %s accessing %s: %s", s, chanpath(c), up->errstr);
	else
		snprint(buf, sizeof buf, "sys: %s", s);
	if(up->nerrlab) {
		if(up->kp == 0)
			postnote(up, 1, buf, NDebug);
		error(s);
	}
	pprint("suicide: %s\n", buf);
	pexit(s, 1);
}

static void
pio(Segment *s, uintptr addr, uintptr soff, Page **p)
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
		if(strcmp(up->errstr, Eintr) == 0)
			continue;
		kunmap(k);
		putpage(new);
		faulterror(Eioload, c);
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

static int
fixfault(Segment *s, uintptr addr, int read)
{
	Pte **pte, *etp;
	uintptr soff, mmuphys;
	Page **pg, *old, *new;

	addr &= ~(BY2PG-1);
	soff = addr-s->base;
	pte = &s->map[soff/PTEMAPMEM];
	if((etp = *pte) == nil)
		*pte = etp = ptealloc();

	pg = &etp->pages[(soff&(PTEMAPMEM-1))/BY2PG];
	if(pg < etp->first)
		etp->first = pg;
	if(pg > etp->last)
		etp->last = pg;

	switch(s->type & SG_TYPE) {
	default:
		panic("fault");
		return -1;

	case SG_TEXT: 			/* Demand load */
		if(pagedout(*pg))
			pio(s, addr, soff, pg);

		mmuphys = PPN((*pg)->pa) | PTERONLY | PTECACHED | PTEVALID;
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
		/* wet floor */
	case SG_DATA:			/* Demand load/pagein/copy on write */
		if(pagedout(*pg))
			pio(s, addr, soff, pg);

		/*
		 *  It's only possible to copy on write if
		 *  we're the only user of the segment.
		 */
		if(read && conf.copymode == 0 && s->ref == 1) {
			mmuphys = PPN((*pg)->pa) | PTERONLY | PTECACHED | PTEVALID;
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
		/* wet floor */
	case SG_STICKY:			/* Never paged out */
		mmuphys = PPN((*pg)->pa) | PTEWRITE | PTECACHED | PTEVALID;
		(*pg)->modref = PG_MOD|PG_REF;
		break;

	case SG_FIXED:			/* Never paged out */
		mmuphys = PPN((*pg)->pa) | PTEWRITE | PTEUNCACHED | PTEVALID;
		(*pg)->modref = PG_MOD|PG_REF;
		break;
	}

#ifdef PTENOEXEC
	if((s->type & SG_NOEXEC) != 0 || s->flushme == 0)
		mmuphys |= PTENOEXEC;
#endif

	qunlock(s);

	putmmu(addr, mmuphys, *pg);

	return 0;
}

static void
mapphys(Segment *s, uintptr addr, int attr)
{
	uintptr mmuphys;
	Page pg = {0};

	addr &= ~(BY2PG-1);
	pg.ref = 1;
	pg.va = addr;
	pg.pa = s->pseg->pa+(addr-s->base);
	if(s->flushme)
		pg.txtflush = ~0;

	mmuphys = PPN(pg.pa) | PTEVALID;
	if((attr & SG_RONLY) == 0)
		mmuphys |= PTEWRITE;
	else
		mmuphys |= PTERONLY;

#ifdef PTENOEXEC
	if((attr & SG_NOEXEC) != 0 || s->flushme == 0)
		mmuphys |= PTENOEXEC;
#endif

#ifdef PTEDEVICE
	if((attr & SG_DEVICE) != 0)
		mmuphys |= PTEDEVICE;
	else
#endif
	if((attr & SG_CACHED) == 0)
		mmuphys |= PTEUNCACHED;
	else
		mmuphys |= PTECACHED;

	qunlock(s);

	putmmu(addr, mmuphys, &pg);
}

int
fault(uintptr addr, uintptr pc, int read)
{
	Segment *s;
	char *sps;
	int pnd, attr;

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

		attr = s->type;
		if((attr & SG_TYPE) == SG_PHYSICAL)
			attr |= s->pseg->attr;

		if((attr & SG_FAULT) != 0
		|| read? ((attr & SG_NOEXEC) != 0 || s->flushme == 0) && (addr & -BY2PG) == (pc & -BY2PG):
			 (attr & SG_RONLY) != 0) {
			qunlock(s);
			up->psstate = sps;
			if(up->kp && up->nerrlab)	/* for segio */
				error(Eio);
			return -1;
		}

		if((attr & SG_TYPE) == SG_PHYSICAL){
			mapphys(s, addr, attr);
			break;
		}

		if(fixfault(s, addr, read) == 0)
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

/*
 * Called only in a system call
 */
int
okaddr(uintptr addr, ulong len, int write)
{
	Segment *s;

	if((long)len >= 0 && len <= -addr) {
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
vmemchr(void *s, int c, ulong n)
{
	uintptr a;
	ulong m;
	void *t;

	a = (uintptr)s;
	for(;;){
		m = BY2PG - (a & (BY2PG-1));
		if(n <= m)
			break;
		/* spans pages; handle this page */
		t = memchr((void*)a, c, m);
		if(t != nil)
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
		if(s->mapsize > 0){
			for(addr=s->base; addr<s->top; addr+=BY2PG){
				off = addr - s->base;
				if((p = s->map[off/PTEMAPMEM]) == nil)
					continue;
				pg = p->pages[(off&(PTEMAPMEM-1))/BY2PG];
				if(pagedout(pg))
					continue;
				checkmmu(addr, pg->pa);
			}
		}
		qunlock(s);
	}
}
