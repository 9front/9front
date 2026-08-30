#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

_Noreturn static void
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
		up->psstate = nil;
		error(s);
	}
	pprint("suicide: %s\n", buf);
	pexit(s, 1);
}

void
faultnote(char *type, char *access, uintptr addr)
{
	char buf[ERRMAX];

	checkpages();
	snprint(buf, sizeof(buf), "sys: trap: %s %s addr=%#p", type, access, addr);
	postnote(up, 1, buf, NDebug);
}

static Page**
pio(Segment *s, uintptr addr, Page **pg)
{
	KMap *k;
	Chan *c;
	int n, ask;
	uintptr daddr, vaddr;
	Page *old, *new;
	Image *image;

retry:
	old = *pg;
	if(old == nil) {	/* from a text/data image */
		daddr = s->fstart+(addr - s->base);
		image = s->image;
		new = lookpage(image, daddr);
		if(new != nil) {
			*pg = new;
			s->used++;
			return pg;
		}

		ask = image->c->iounit;
		if(ask == 0) ask = qiomaxatomic;
		ask &= -BY2PG;
		if(ask == 0) ask = BY2PG;

		daddr = (addr - s->base) & -ask;
		if(daddr+ask > s->flen)
			ask = s->flen-daddr;
		vaddr = s->base + daddr;
		daddr += s->fstart;
	} else {		/* from a swap image */
		daddr = swapaddr(old);
		image = swapimage;
		new = lookpage(image, daddr);
		if(new != nil) {
			*pg = new;
			s->swapped--;
			putswap(old);
			return pg;
		}
		vaddr = addr;
		ask = BY2PG;
	}
	qunlock(s);

	c = image->c;
	if(waserror()) {
		if(strcmp(up->errstr, Eintr) == 0)
			return nil;
		faulterror(Eioload, c);
	}
	if(ask <= BY2PG) {
		new = newpage(vaddr, nil);
		new->daddr = daddr;
		k = kmap(new);
		if(waserror()){
			kunmap(k);
			putpage(new);
			nexterror();
		}
		n = devtab[c->type]->read(c, (uchar*)VA(k), ask, daddr);
		if(n != ask)
			error(Eshort);
		if(n < BY2PG)
			memset((uchar*)VA(k)+n, 0, BY2PG-n);
		kunmap(k);
		settxtflush(new, s->flushme);
		cachepage(new, image);
		putpage(new);
		poperror();
	} else {
		uintptr o;
		Block *b;

		b = devtab[c->type]->bread(c, ask, daddr);
		if(waserror()){
			freeblist(b);
			nexterror();
		}
		for(o = 0; o < ask; o += BY2PG){
			new = lookpage(image, daddr + o);
			if(new != nil){
				putpage(new);
				continue;
			}
			new = newpage(vaddr + o, nil);
			new->daddr = daddr + o;
			k = kmap(new);
			n = ask - o;
			if(n > BY2PG)
				n = BY2PG;
			else if(n < BY2PG)
				memset((uchar*)VA(k)+n, 0, BY2PG-n);
			if(readblist(b, (uchar*)VA(k), n, o) != n){
				kunmap(k);
				putpage(new);
				error(Eshort);
			}
			kunmap(k);
			settxtflush(new, s->flushme);
			cachepage(new, image);
			putpage(new);
		}
		freeblist(b);
		poperror();
	}
	poperror();

	qlock(s);
	pg = segmap(s, addr);
	if(pg == nil){
		qunlock(s);
		return nil;
	}

	/*
	 *  race, another proc may have gotten here first
	 *  (and the pager may have run on that page) while
	 *  s was unlocked
	 */
	if(*pg != old && !pagedout(*pg))
		return pg;
	goto retry;
}

static int
fixfault(Segment *s, uintptr addr, int read)
{
	Page **pg, *old, *new;
	uintptr mmuphys;

	addr &= ~(BY2PG-1);
	pg = segmap(s, addr);
	if(pg == nil){
		qunlock(s);
		if(!waserror()){
			resrcwait("no memory for segmap");
			poperror();
		}
		return -1;
	}

	switch(s->type & SG_TYPE) {
	default:
		panic("fault");

	case SG_TEXT: 			/* Demand load */
		if(pagedout(*pg)) {
			pg = pio(s, addr, pg);
			if(pg == nil)
				return -1;
		}
		new = *pg;
		mmuphys = PPN(new->pa) | PTERONLY | PTECACHED | PTEVALID;
		new->modref = PG_REF;
		break;

	case SG_BSS:
	case SG_SHARED:			/* fill on demand */
	case SG_STACK:
		if(*pg == nil) {
			new = newpage(addr, s);
			if(new == nil)
				return -1;
			*pg = fillpage(new, (s->type&SG_TYPE)==SG_STACK? 0xfe: 0);
			s->used++;
		}
		/* wet floor */
	case SG_DATA:			/* Demand load/pagein/copy on write */
		if(pagedout(*pg)) {
			pg = pio(s, addr, pg);
			if(pg == nil)
				return -1;
		}

		/*
		 *  It's only possible to copy on write if
		 *  we're the only user of the segment.
		 */
		if(read && conf.copymode == 0 && s->ref == 1) {
			new = *pg;
			mmuphys = PPN(new->pa) | PTERONLY | PTECACHED | PTEVALID;
			new->modref |= PG_REF;
			break;
		}

		old = *pg;
		if(swapimage != nil && old->image == swapimage && (old->ref + swapcount(old->daddr)) == 1)
			uncachepage(old);
		if(old->ref > 1 || old->image != nil) {
			new = newpage(addr, s);
			if(new == nil)
				return -1;
			copypage(old, new);
			settxtflush(new, s->flushme);
			*pg = new;
			/* s->used count unchanged */
			putpage(old);
		}
		/* wet floor */
	case SG_STICKY:			/* Never paged out */
		new = *pg;
		mmuphys = PPN(new->pa) | PTEWRITE | PTECACHED | PTEVALID;
		new->modref |= up->privatemem? PG_PRIV|PG_MOD|PG_REF: PG_MOD|PG_REF;
		break;

	case SG_FIXED:			/* Never paged out */
		new = *pg;
		mmuphys = PPN(new->pa) | PTEWRITE | PTEUNCACHED | PTEVALID;
		new->modref |= up->privatemem? PG_PRIV|PG_MOD|PG_REF: PG_MOD|PG_REF;
		break;
	}

#ifdef PTENOEXEC
	if((s->type & SG_NOEXEC) != 0 || s->flushme == 0)
		mmuphys |= PTENOEXEC;
#endif

	qunlock(s);

	putmmu(addr, mmuphys, new);

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
	settxtflush(&pg, s->flushme);

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
	int pnd, ins, attr;

	if(up == nil)
		panic("fault: no user process pc=%#p addr=%#p", pc, addr);

	if(up->nlocks){
		Lock *l = up->lastlock;
		print("fault: nlocks %d, proc %lud %s, addr %#p, lock %#p, lpc %#p\n", 
			up->nlocks, up->pid, up->text, addr, l, l ? l->pc : 0);
	}

	pnd = up->notepending;
	ins = up->insyscall;
	up->insyscall = 1;
	sps = up->psstate;
	up->psstate = "Fault";

	m->pfault++;

	for(;;) {
		spllo();

		s = seg(up, addr, 1);		/* leaves s locked if seg != nil */
		if(s == nil) {
			up->psstate = sps;
			up->insyscall = ins;
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
			up->insyscall = ins;
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
			if(up->nerrlab){
				up->psstate = sps;
				up->insyscall = ins;
				up->notepending |= pnd;
				error(up->procctl==Proc_exitbig?
					"Killed: Insufficient physical memory":
					"Killed");
			}
			procctl();
			break;
		}
	}

	up->psstate = sps;
	up->insyscall = ins;
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
	Segment *s;
	int i;

	for(i = 0; i < NSEG; i++) {
		if((s = p->seg[i]) == nil)
			continue;
		if(addr >= s->base && addr < s->top) {
			if(dolock == 0)
				return s;

			qlock(s);
			if(addr >= s->base && addr < s->top)
				return s;
			qunlock(s);
		}
	}

	return nil;
}

extern void checkmmu(uintptr, uintptr);

void
checkpages(void)
{
	uintptr addr;
	Segment *s;
	Page *p;
	int i;
	
	if(up == nil)
		return;

	for(i = 0; i < NSEG; i++) {
		if((s = up->seg[i]) == nil)
			continue;
		qlock(s);
		if(s->mapsize > 0){
			for(addr=s->base; addr<s->top; addr+=BY2PG){
				p = segpeek(s, addr);
				if(!pagedout(p))
					checkmmu(addr, p->pa);
			}
		}
		qunlock(s);
	}
}
