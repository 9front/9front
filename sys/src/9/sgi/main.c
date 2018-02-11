#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"init.h"
#include	"pool.h"
#include	"../ip/ip.h"
#include	<tos.h>
#include	<../port/error.h>

enum {
	/* space for syscall args, return PC, top-of-stack struct */
	Stkheadroom	= sizeof(Sargs) + sizeof(uintptr) + sizeof(Tos),
};

static uchar *sp;		/* XXX - must go - user stack of init proc */
static FPsave initfp;

/*
 * software tlb simulation
 */
static Softtlb stlb[MAXMACH][STLBSIZE];

Conf	conf;

char*
getconf(char *name)
{
	return (char*)arcs(0x78, name);
}

static void
fmtinit(void)
{
	printinit();
	quotefmtinstall();
	/* ipreset installs these when chandevreset runs */
	fmtinstall('i', eipfmt);
	fmtinstall('I', eipfmt);
	fmtinstall('E', eipfmt);
	fmtinstall('V', eipfmt);
	fmtinstall('M', eipfmt);
}

static int
ckpagemask(ulong mask, ulong size)
{
	int s;
	ulong pm;

	s = splhi();
	setpagemask(mask);
	pm = getpagemask();
	splx(s);
	if(pm != mask){
		iprint("page size %ldK not supported on this cpu; "
			"mask %#lux read back as %#lux\n", size/1024, mask, pm);
		return -1;
	}
	return 0;
}

void
addmem(uintptr base, uintptr top)
{
	uintptr s, e;
	ulong *m;
	int i;

	if(base >= top)
		return;

	/* exclude kernel */
	s = 0;
	e = PADDR(PGROUND((uintptr)end));
	if(s < top && e > base){
		if(s > base)
			addmem(base, s);
		if(e < top)
			addmem(e, top);
		return;
	}

	/* exclude reserved firmware memory regions */
	m = nil;
	while((m = (ulong*)arcs(0x48, m)) != nil){
		s = m[1]<<12;
		e = s + (m[2]<<12);
		switch(m[0]){
		case 2:	/* FreeMemory */
		case 3:	/* BadMemory */
			continue;
		}
		if(s < top && e > base){
			if(s > base)
				addmem(base, s);
			if(e < top)
				addmem(e, top);
			return;
		}
	}
	for(i=0; i<nelem(conf.mem); i++){
		if(conf.mem[i].npage == 0){
			conf.mem[i].base = base;
			conf.mem[i].npage = (top - base)/BY2PG;

			conf.npage += conf.mem[i].npage;
			return;
		}
	}
	print("conf.mem[] too small\n");
}

/* 
 * get memory configuration word for a bank
 */
ulong
bank_conf(int bank)
{
	switch(bank){
	case 0:
		return *(ulong *)(KSEG1|MEMCFG0) >> 16;
	case 1:
		return *(ulong *)(KSEG1|MEMCFG0) & 0xffff;
	case 2:
		return *(ulong *)(KSEG1|MEMCFG1) >> 16;
	case 3:
		return *(ulong *)(KSEG1|MEMCFG1) & 0xffff;
	}
	return 0;
}

void
meminit(void)
{
	uintptr base, size, top;
	ulong mconf;
	int i;

	/*
	 *  divide memory twixt user pages and kernel.
	 */
	conf.npage = 0;
	for(i=0; i<4; i++){
		mconf = bank_conf(i);
		if(!(mconf & 0x2000))
			continue;
		base = (mconf & 0xff) << 22;
		size = ((mconf & 0x1f00) + 0x0100) << 14;
		top = base + size;
		addmem(base, top);
	}
}

static int
havegfx(void)
{
	char *s = getconf("ConsoleOut");
	return s != nil && strstr(s, "video()") != nil;
}

void
main(void)
{
	savefpregs(&initfp);

	arcsconsinit();

	meminit();
	confinit();
	machinit();			/* calls clockinit */
	active.exiting = 0;
	active.machs[0] = 1;
	print("\nPlan 9\n");

	kmapinit();
	xinit();
	timersinit();
	fmtinit();

	if(havegfx()){
		conf.monitor = 1;
		screeninit();
	}

	ckpagemask(PGSZ, BY2PG);
	tlbinit();
	pageinit();
	procinit0();
	initseg();
	links();
	chandevreset();
	userinit();
	schedinit();
	panic("schedinit returned");
}

/*
 *  initialize a processor's mach structure.  each processor does this
 *  for itself.
 */
void
machinit(void)
{
	extern void gevector(void);	/* l.s */
	extern void utlbmiss(void);
	extern void vector0(void);
	extern void vector180(void);

	void **sbp = (void*)SPBADDR;

	m->stb = stlb[m->machno];

	/* install exception handlers */
	sbp[0x18/4] = utlbmiss;
	sbp[0x14/4] = gevector;

	/* we could install our own vectors directly, but we'll try to play nice */
	if(0){
		memmove((void*)(KSEG0+0x0), (void*)vector0, 0x80);
		memmove((void*)(KSEG0+0x180), (void*)vector180, 0x80);
		icflush((void*)(KSEG0+0x0), 0x80);
		icflush((void*)(KSEG0+0x180), 0x80);
	}

	/* Ensure CU1 is off */
	clrfpintr();
	clockinit();
}

void
init0(void)
{
	char buf[128];

	up->nerrlab = 0;

	spllo();

	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	pathclose(up->slash->path);
	up->slash->path = newpath("/");
	up->dot = cclone(up->slash);

	chandevinit();

	if(!waserror()){
		ksetenv("cputype", "mips", 0);
		snprint(buf, sizeof buf, "mips %s", conffile);
		ksetenv("terminal", buf, 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);

		ksetenv("bootargs", "tcp", 0);
		ksetenv("console", "0", 0);

		/* no usb */
		ksetenv("usbwait", "0", 0);
		ksetenv("nousbrc", "1", 0);

		poperror();
	}

	/* process input for arcs console */
	if(!conf.keyboard)
		kproc("arcs", arcsproc, 0);

	kproc("alarm", alarmkproc, 0);
	touser(sp);
}

static uchar *
pusharg(char *p)
{
	int n;
	
	n = strlen(p) + 1;
	sp -= n;
	memmove(sp, p, n);
	return sp;
}

static void
bootargs(uintptr base)
{	int i, ac;
	uchar *av[32];
	uchar **lsp;
	
	sp = (uchar *) base + BY2PG - sizeof(Tos);
	
	ac = 0;
	av[ac++] = pusharg("boot");
	sp = (uchar *) ((ulong) sp & ~7);
	sp -= ROUND((ac + 1) * sizeof(sp), 8) + 4;
	lsp = (uchar **) sp;
	for(i = 0; i < ac; i++)
		lsp[i] = av[i] + ((USTKTOP - BY2PG) - (ulong) base);
	lsp[i] = 0;
	sp += (USTKTOP - BY2PG) - (ulong) base;
}

void
userinit(void)
{
	Proc *p;
	KMap *k;
	Page *pg;
	Segment *s;

	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;

	kstrdup(&eve, "");
	kstrdup(&p->text, "*init*");
	kstrdup(&p->user, eve);

	procsetup(p);

	/*
	 * Kernel Stack
	 */
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->kstack+KSTACK-Stkheadroom;
	p->sched.sp = STACKALIGN(p->sched.sp);

	/*
	 * User Stack
	 *
	 * Technically, newpage can't be called here because it
	 * should only be called when in a user context as it may
	 * try to sleep if there are no pages available, but that
	 * shouldn't be the case here.
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	p->seg[SSEG] = s;
	pg = newpage(1, 0, USTKTOP-BY2PG);
	segpage(s, pg);
	k = kmap(pg);
	bootargs(VA(k));
	kunmap(k);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, 1);
	s->flushme++;
	p->seg[TSEG] = s;
	pg = newpage(1, 0, UTZERO);
	pg->txtflush = ~0;
	segpage(s, pg);
	k = kmap(s->map[0]->pages[0]);
	memset((void *)VA(k), 0, BY2PG);
	memmove((ulong*)VA(k), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

void
exit(int)
{
	cpushutdown();
	splhi();
	arcs(0x18);	/* reboot */
}

void
reboot(void *, void *, ulong)
{
}

void
evenaddr(uintptr va)
{
	if((va & 3) != 0){
		dumpstack();
		postnote(up, 1, "sys: odd address", NDebug);
		error(Ebadarg);
	}
}

void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
	memmove(p->fpsave, &initfp, sizeof(FPsave));

	cycles(&p->kentry);
	p->pcycles = -p->kentry;
}

void
procfork(Proc *p)
{
	int s;

	p->kentry = up->kentry;
	p->pcycles = -p->kentry;

	s = splhi();
	switch(up->fpstate & ~FPillegal){
	case FPactive:
		savefpregs(up->fpsave);
		up->fpstate = FPinactive;
		/* wet floor */
	case FPinactive:
		memmove(p->fpsave, up->fpsave, sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	splx(s);
}

void
procsave(Proc *p)
{
	uvlong t;

	if(p->fpstate == FPactive){
		if(p->state != Moribund) {
			savefpregs(p->fpsave);
			p->fpstate = FPinactive;
		}
	}
	
	cycles(&t);
	p->pcycles += t;
}

void
procrestore(Proc *p)
{
	uvlong t;

	if(p->kp)
		return;
	cycles(&t);
	p->pcycles -= t;
}

void
idlehands(void)
{
}

void
confinit(void)
{
	ulong kpages;

	/*
	 *  set up CPU's mach structure
	 *  cpu0's was zeroed in l.s and our stack is in Mach, so don't zero it.
	 */
	m->machno = 0;
	m->speed = 150;			/* initial guess at MHz */
	m->hz = m->speed * Mhz;
	conf.nmach = 1;

	/* set up other configuration parameters */
	conf.nproc = 2000;
	conf.nswap = 262144;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = 0;		/* copy on write */

	kpages = conf.npage - (conf.npage*80)/100;
	if(kpages > (64*MB + conf.npage*sizeof(Page))/BY2PG){
		kpages = (64*MB + conf.npage*sizeof(Page))/BY2PG;
		kpages += (conf.nproc*KSTACK)/BY2PG;
	}
	conf.upages = conf.npage - kpages;
	conf.ialloc = (kpages/2)*BY2PG;

	kpages *= BY2PG;
	kpages -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap
		+ conf.nswppo*sizeof(Page*);
	mainmem->maxsize = kpages;
	imagmem->maxsize = kpages;
//	mainmem->flags |= POOL_PARANOIA;
}

void
setupwatchpts(Proc *, Watchpt *, int n)
{
	if(n > 0)
		error("no watchpoints");
}

int
isaconfig(char *, int, ISAConf*)
{
	return 0;
}
