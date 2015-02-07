#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"tos.h"
#include	"ureg.h"
#include	"init.h"
#include	"pool.h"
#include	"reboot.h"

Mach *m;

/*
 * Where configuration info is left for the loaded programme.
 * This will turn into a structure as more is done by the boot loader
 * (e.g. why parse the .ini file twice?).
 * There are 3584 bytes available at CONFADDR.
 */
#define BOOTLINE	((char*)CONFADDR)
#define BOOTLINELEN	64
#define BOOTARGS	((char*)(CONFADDR+BOOTLINELEN))
#define	BOOTARGSLEN	(4096-0x200-BOOTLINELEN)
#define	MAXCONF		64

Conf conf;
char *confname[MAXCONF];
char *confval[MAXCONF];
int nconf;
char *sp;	/* user stack of init proc */
int delaylink;
int idle_spin;

static void
multibootargs(void)
{
	char *cp, *ep;
	ulong *m, l;

	extern ulong *multiboot;

	if(multiboot == nil)
		return;

	/* command line */
	if((multiboot[0] & (1<<2)) != 0)
		strncpy(BOOTLINE, KADDR(multiboot[4]), BOOTLINELEN-1);

	cp = BOOTARGS;
	ep = cp + BOOTARGSLEN-1;

	/* memory map */
	if((multiboot[0] & (1<<6)) != 0 && (l = multiboot[11]) >= 24){
		cp = seprint(cp, ep, "*e820=");
		m = KADDR(multiboot[12]);
		while(m[0] >= 20 && m[0] <= l-4){
			uvlong base, size;
			m++;
			base = ((uvlong)m[0] | (uvlong)m[1]<<32);
			size = ((uvlong)m[2] | (uvlong)m[3]<<32);
			cp = seprint(cp, ep, "%.1lux %.16llux %.16llux ",
				m[4] & 0xF, base, base+size);
			l -= m[-1]+4;
			m = (ulong*)((ulong)m + m[-1]);
		}
		cp[-1] = '\n';
	}

	/* plan9.ini passed as the first module */
	if((multiboot[0] & (1<<3)) != 0 && multiboot[5] > 0){
		m = KADDR(multiboot[6]);
		l = m[1] - m[0];
		m = KADDR(m[0]);
		if(cp+l > ep)
			l = ep - cp;
		memmove(cp, m, l);
		cp += l;
	}
	*cp = 0;
}

static void
options(void)
{
	long i, n;
	char *cp, *line[MAXCONF], *p, *q;

	multibootargs();

	/*
	 *  parse configuration args from dos file plan9.ini
	 */
	cp = BOOTARGS;	/* where b.com leaves its config */
	cp[BOOTARGSLEN-1] = 0;

	/*
	 * Strip out '\r', change '\t' -> ' '.
	 */
	p = cp;
	for(q = cp; *q; q++){
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}
	*p = 0;

	n = getfields(cp, line, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = strchr(line[i], '=');
		if(cp == nil)
			continue;
		*cp++ = '\0';
		confname[nconf] = line[i];
		confval[nconf] = cp;
		nconf++;
	}
}

extern void (*i8237alloc)(void);
extern void bootscreeninit(void);

void
main(void)
{
	mach0init();
	options();
	ioinit();
	i8250console();
	quotefmtinstall();
	screeninit();

	print("\nPlan 9\n");

	trapinit0();
	kbdinit();
	i8253init();
	cpuidentify();
	meminit();
	confinit();
	xinit();
	archinit();
	bootscreeninit();
	if(i8237alloc != nil)
		i8237alloc();
	trapinit();
	printinit();
	cpuidprint();
	mmuinit();
	if(arch->intrinit)	/* launches other processors on an mp */
		arch->intrinit();
	timersinit();
	mathinit();
	kbdenable();
	if(arch->clockenable)
		arch->clockenable();
	procinit0();
	initseg();
	if(delaylink){
		bootlinks();
		pcimatch(0, 0, 0);
	}else
		links();
	conf.monitor = 1;
	chandevreset();
	pageinit();
	swapinit();
	userinit();
	active.thunderbirdsarego = 1;
	schedinit();
}

void
mach0init(void)
{
	conf.nmach = 1;
	MACHP(0) = (Mach*)CPU0MACH;
	m->pdb = (ulong*)CPU0PDB;
	m->gdt = (Segdesc*)CPU0GDT;

	machinit();

	active.machs = 1;
	active.exiting = 0;
}

void
machinit(void)
{
	int machno;
	ulong *pdb;
	Segdesc *gdt;

	machno = m->machno;
	pdb = m->pdb;
	gdt = m->gdt;
	memset(m, 0, sizeof(Mach));
	m->machno = machno;
	m->pdb = pdb;
	m->gdt = gdt;
	m->perf.period = 1;

	/*
	 * For polled uart output at boot, need
	 * a default delay constant. 100000 should
	 * be enough for a while. Cpuidentify will
	 * calculate the real value later.
	 */
	m->loopconst = 100000;
}

void
init0(void)
{
	int i;
	char buf[2*KNAMELEN];

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
		snprint(buf, sizeof(buf), "%s %s", arch->id, conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "386", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		for(i = 0; i < nconf; i++){
			if(confname[i][0] != '*')
				ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	touser(sp);
}

void
userinit(void)
{
	void *v;
	Proc *p;
	Segment *s;
	Page *pg;

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
	 *
	 * N.B. make sure there's enough space for syscall to check
	 *	for valid args and 
	 *	4 bytes for gotolabel's return PC
	 */
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->kstack+KSTACK-(sizeof(Sargs)+BY2WD);

	/*
	 * User Stack
	 *
	 * N.B. cannot call newpage() with clear=1, because pc kmap
	 * requires up != nil.  use tmpmap instead.
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	p->seg[SSEG] = s;
	pg = newpage(0, 0, USTKTOP-BY2PG);
	v = tmpmap(pg);
	memset(v, 0, BY2PG);
	segpage(s, pg);
	bootargs(v);
	tmpunmap(v);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, 1);
	s->flushme++;
	p->seg[TSEG] = s;
	pg = newpage(0, 0, UTZERO);
	pg->txtflush = ~0;
	segpage(s, pg);
	v = tmpmap(pg);
	memset(v, 0, BY2PG);
	memmove(v, initcode, sizeof initcode);
	tmpunmap(v);

	ready(p);
}

void
bootargs(void *base)
{
	char *argv[8];
	int i, argc;

#define UA(ka)	((char*)(ka) + ((uintptr)(USTKTOP - BY2PG) - (uintptr)base))
	sp = (char*)base + BY2PG - sizeof(Tos);

	/* push boot command line onto the stack */
	sp -= BOOTLINELEN;
	sp[BOOTLINELEN-1] = '\0';
	memmove(sp, BOOTLINE, BOOTLINELEN-1);

	/* parse boot command line */
	argc = tokenize(sp, argv, nelem(argv));
	if(argc < 1){
		strcpy(sp, "boot");
		argc = 0;
		argv[argc++] = sp;
	}

	/* 4 byte word align stack */
	sp = (char*)((uintptr)sp & ~3);

	/* build argv on stack */
	sp -= (argc+1)*BY2WD;
	for(i=0; i<argc; i++)
		((char**)sp)[i] = UA(argv[i]);
	((char**)sp)[i] = nil;

	sp = UA(sp);
#undef UA
	sp -= BY2WD;
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return confval[i];
	return 0;
}

static void
writeconf(void)
{
	char *p, *q;
	int n;

	p = getconfenv();

	if(waserror()) {
		free(p);
		nexterror();
	}

	/* convert to name=value\n format */
	for(q=p; *q; q++) {
		q += strlen(q);
		*q = '=';
		q += strlen(q);
		*q = '\n';
	}
	n = q - p + 1;
	if(n >= BOOTARGSLEN)
		error("kernel configuration too large");
	memset(BOOTLINE, 0, BOOTLINELEN);
	memmove(BOOTARGS, p, n);
	poperror();
	free(p);
}

void
confinit(void)
{
	char *p;
	int i, userpcnt;
	ulong kpages;

	if(p = getconf("service")){
		if(strcmp(p, "cpu") == 0)
			cpuserver = 1;
		else if(strcmp(p,"terminal") == 0)
			cpuserver = 0;
	}

	if(p = getconf("*kernelpercent"))
		userpcnt = 100 - strtol(p, 0, 0);
	else
		userpcnt = 0;

	conf.npage = 0;
	for(i=0; i<nelem(conf.mem); i++)
		conf.npage += conf.mem[i].npage;

	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nimage = 200;
	conf.nswap = conf.nproc*80;
	conf.nswppo = 4096;

	if(cpuserver) {
		if(userpcnt < 10)
			userpcnt = 70;
		kpages = conf.npage - (conf.npage*userpcnt)/100;

		/*
		 * Hack for the big boys. Only good while physmem < 4GB.
		 * Give the kernel fixed max + enough to allocate the
		 * page pool.
		 * This is an overestimate as conf.upages < conf.npages.
		 * The patch of nimage is a band-aid, scanning the whole
		 * page list in imagereclaim just takes too long.
		 */
		if(getconf("*imagemaxmb") == 0)
		if(kpages > (64*MB + conf.npage*sizeof(Page))/BY2PG){
			kpages = (64*MB + conf.npage*sizeof(Page))/BY2PG;
			conf.nimage = 2000;
			kpages += (conf.nproc*KSTACK)/BY2PG;
		}
	} else {
		if(userpcnt < 10) {
			if(conf.npage*BY2PG < 16*MB)
				userpcnt = 50;
			else
				userpcnt = 60;
		}
		kpages = conf.npage - (conf.npage*userpcnt)/100;

		/*
		 * Make sure terminals with low memory get at least
		 * 4MB on the first Image chunk allocation.
		 */
		if(conf.npage*BY2PG < 16*MB)
			imagmem->minarena = 4*MB;
	}

	/*
	 * can't go past the end of virtual memory
	 * (ulong)-KZERO is 2^32 - KZERO
	 */
	if(kpages > ((ulong)-KZERO)/BY2PG)
		kpages = ((ulong)-KZERO)/BY2PG;

	conf.upages = conf.npage - kpages;
	conf.ialloc = (kpages/2)*BY2PG;

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for.
	 */
	kpages *= BY2PG;
	kpages -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap
		+ conf.nswppo*sizeof(Page*);
	mainmem->maxsize = kpages;

	/*
	 * the dynamic allocation will balance the load properly,
	 * hopefully. be careful with 32-bit overflow.
	 */
	imagmem->maxsize = kpages - (kpages/10);
	if(p = getconf("*imagemaxmb")){
		imagmem->maxsize = strtol(p, nil, 0)*MB;
		if(imagmem->maxsize > mainmem->maxsize)
			imagmem->maxsize = mainmem->maxsize;
	}
}

/*
 * we keep FPsave structure in sse format emulating FXSAVE / FXRSTOR
 * instructions for legacy x87 fpu.
 *
 * Note that fpx87restore() and fpxsserestore() do modify the FPsave
 * data structure for conversion / realignment shuffeling. this means
 * that p->fpsave is only valid when p->fpstate == FPinactive.
 */
void
fpx87save(FPsave *fps)
{
	ushort tag;

	fpx87save0(fps);

	/*
	 * convert x87 tag word to fxsave tag byte:
	 * 00, 01, 10 -> 1, 11 -> 0
	 */
	tag = ~fps->tag;
	tag = (tag | (tag >> 1)) & 0x5555;
	tag = (tag | (tag >> 1)) & 0x3333;
	tag = (tag | (tag >> 2)) & 0x0F0F;
	tag = (tag | (tag >> 4)) & 0x00FF;

	/* NOP fps->fcw = fps->control; */
	fps->fsw = fps->status;
	fps->ftw = tag;
	fps->fop = fps->opcode;
	fps->fpuip = fps->pc;
	fps->cs = fps->selector;
	fps->fpudp = fps->operand;
	fps->ds = fps->oselector;

#define MOVA(d,s) \
	*((ushort*)(d+8)) = *((ushort*)(s+8)), \
	*((ulong*)(d+4)) = *((ulong*)(s+4)), \
	*((ulong*)(d)) = *((ulong*)(s))

	MOVA(fps->xregs+0x70, fps->regs+70);
	MOVA(fps->xregs+0x60, fps->regs+60);
	MOVA(fps->xregs+0x50, fps->regs+50);
	MOVA(fps->xregs+0x40, fps->regs+40);
	MOVA(fps->xregs+0x30, fps->regs+30);
	MOVA(fps->xregs+0x20, fps->regs+20);
	MOVA(fps->xregs+0x10, fps->regs+10);
	MOVA(fps->xregs+0x00, fps->regs+00);

#undef MOVA

#define CLR6(d)	\
	*((ulong*)(d)) = 0, \
	*((ushort*)(d+4)) = 0

	CLR6(fps->xregs+0x70+10);
	CLR6(fps->xregs+0x60+10);
	CLR6(fps->xregs+0x50+10);
	CLR6(fps->xregs+0x40+10);
	CLR6(fps->xregs+0x30+10);
	CLR6(fps->xregs+0x20+10);
	CLR6(fps->xregs+0x10+10);
	CLR6(fps->xregs+0x00+10);

#undef CLR6

	fps->rsrvd1 = fps->rsrvd2 = fps->mxcsr = fps->mxcsr_mask = 0;
}

void
fpx87restore(FPsave *fps)
{
	ushort msk, tos, tag, *reg;

	/* convert fxsave tag byte to x87 tag word */
	tag = 0;
	tos = 7 - ((fps->fsw >> 11) & 7);
	for(msk = 0x80; msk != 0; tos--, msk >>= 1){
		tag <<= 2;
		if((fps->ftw & msk) != 0){
			reg = (ushort*)&fps->xregs[(tos & 7) << 4];
			switch(reg[4] & 0x7fff){
			case 0x0000:
				if((reg[0] | reg[1] | reg[2] | reg[3]) == 0){
					tag |= 1;	/* 01 zero */
					break;
				}
				/* no break */
			case 0x7fff:
				tag |= 2;		/* 10 special */
				break;
			default:
				if((reg[3] & 0x8000) == 0)
					break;		/* 00 valid */
				tag |= 2;		/* 10 special */
				break;
			}
		}else{
			tag |= 3;			/* 11 empty */
		}
	}

#define MOVA(d,s) \
	*((ulong*)(d)) = *((ulong*)(s)), \
	*((ulong*)(d+4)) = *((ulong*)(s+4)), \
	*((ushort*)(d+8)) = *((ushort*)(s+8))

	MOVA(fps->regs+00, fps->xregs+0x00);
	MOVA(fps->regs+10, fps->xregs+0x10);
	MOVA(fps->regs+20, fps->xregs+0x20);
	MOVA(fps->regs+30, fps->xregs+0x30);
	MOVA(fps->regs+40, fps->xregs+0x40);
	MOVA(fps->regs+50, fps->xregs+0x50);
	MOVA(fps->regs+60, fps->xregs+0x60);
	MOVA(fps->regs+70, fps->xregs+0x70);

#undef MOVA

	fps->oselector = fps->ds;
	fps->operand = fps->fpudp;
	fps->opcode = fps->fop & 0x7ff;
	fps->selector = fps->cs;
	fps->pc = fps->fpuip;
	fps->tag = tag;
	fps->status = fps->fsw;
	/* NOP fps->control = fps->fcw;  */

	fps->r1 = fps->r2 = fps->r3 = fps->r4 = 0;

	fpx87restore0(fps);
}

/*
 * sse fp save and restore buffers have to be 16-byte (FPalign) aligned,
 * so we shuffle the data up and down as needed or make copies.
 */
void
fpssesave(FPsave *fps)
{
	FPsave *afps;

	afps = (FPsave *)ROUND(((uintptr)fps), FPalign);
	fpssesave0(afps);
	if(fps != afps)  /* not aligned? shuffle down from aligned buffer */
		memmove(fps, afps, sizeof(FPssestate) - FPalign);
}

void
fpsserestore(FPsave *fps)
{
	FPsave *afps;

	afps = (FPsave *)ROUND(((uintptr)fps), FPalign);
	if(fps != afps)  /* shuffle up to make aligned */
		memmove(afps, fps, sizeof(FPssestate) - FPalign);
	fpsserestore0(afps);
}

static char* mathmsg[] =
{
	nil,	/* handled below */
	"denormalized operand",
	"division by zero",
	"numeric overflow",
	"numeric underflow",
	"precision loss",
};

static void
mathnote(ulong status, ulong pc)
{
	char *msg, note[ERRMAX];
	int i;

	/*
	 * Some attention should probably be paid here to the
	 * exception masks and error summary.
	 */
	msg = "unknown exception";
	for(i = 1; i <= 5; i++){
		if(!((1<<i) & status))
			continue;
		msg = mathmsg[i];
		break;
	}
	if(status & 0x01){
		if(status & 0x40){
			if(status & 0x200)
				msg = "stack overflow";
			else
				msg = "stack underflow";
		}else
			msg = "invalid operation";
	}
	snprint(note, sizeof note, "sys: fp: %s fppc=0x%lux status=0x%lux",
		msg, pc, status);
	postnote(up, 1, note, NDebug);
}

/*
 *  math coprocessor error
 */
static void
matherror(Ureg*, void*)
{
	/*
	 *  a write cycle to port 0xF0 clears the interrupt latch attached
	 *  to the error# line from the 387
	 */
	if(!(m->cpuiddx & Fpuonchip))
		outb(0xF0, 0xFF);

	/*
	 *  get floating point state to check out error
	 */
	fpenv(&up->fpsave);
	mathnote(up->fpsave.status, up->fpsave.pc);
}

/*
 *  SIMD error
 */
static void
simderror(Ureg *ureg, void*)
{
	fpsave(&up->fpsave);
	up->fpstate = FPinactive;
	mathnote(up->fpsave.mxcsr & 0x3f, ureg->pc);
}

/*
 *  math coprocessor emulation fault
 */
static void
mathemu(Ureg *ureg, void*)
{
	ulong status, control;

	if(up->fpstate & FPillegal){
		/* someone did floating point in a note handler */
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}
	switch(up->fpstate){
	case FPinit:
		fpinit();
		if(fpsave == fpssesave)
			ldmxcsr(0);	/* no simd exceptions on 386 */
		up->fpstate = FPactive;
		break;
	case FPinactive:
		/*
		 * Before restoring the state, check for any pending
		 * exceptions, there's no way to restore the state without
		 * generating an unmasked exception.
		 * More attention should probably be paid here to the
		 * exception masks and error summary.
		 */
		status = up->fpsave.fsw;
		control = up->fpsave.fcw;
		if((status & ~control) & 0x07F){
			mathnote(status, up->fpsave.fpuip);
			break;
		}
		fprestore(&up->fpsave);
		up->fpstate = FPactive;
		break;
	case FPactive:
		panic("math emu pid %ld %s pc 0x%lux", 
			up->pid, up->text, ureg->pc);
		break;
	}
}

/*
 *  math coprocessor segment overrun
 */
static void
mathover(Ureg*, void*)
{
	pexit("math overrun", 0);
}

void
mathinit(void)
{
	trapenable(VectorCERR, matherror, 0, "matherror");
	if(X86FAMILY(m->cpuidax) == 3)
		intrenable(IrqIRQ13, matherror, 0, BUSUNKNOWN, "matherror");
	trapenable(VectorCNA, mathemu, 0, "mathemu");
	trapenable(VectorCSO, mathover, 0, "mathover");
	trapenable(VectorSIMD, simderror, 0, "simderror");
}

/*
 *  set up floating point for a new process
 */
void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
	fpoff();

	cycles(&p->kentry);
	p->pcycles = -p->kentry;

	memset(p->gdt, 0, sizeof(p->gdt));
	p->ldt = nil;
	p->nldt = 0;
}

void
procfork(Proc *p)
{
	int s;

	p->kentry = up->kentry;
	p->pcycles = -p->kentry;

	/* inherit user descriptors */
	memmove(p->gdt, up->gdt, sizeof(p->gdt));

	/* copy local descriptor table */
	if(up->ldt != nil && up->nldt > 0){
		p->ldt = smalloc(sizeof(Segdesc) * up->nldt);
		memmove(p->ldt, up->ldt, sizeof(Segdesc) * up->nldt);
		p->nldt = up->nldt;
	}

	/* save floating point state */
	s = splhi();
	switch(up->fpstate & ~FPillegal){
	case FPactive:
		fpsave(&up->fpsave);
		up->fpstate = FPinactive;
	case FPinactive:
		p->fpsave = up->fpsave;
		p->fpstate = FPinactive;
	}
	splx(s);
}

void
procrestore(Proc *p)
{
	uvlong t;

	if(p->kp)
		return;

	cycles(&t);
	p->kentry += t;
	p->pcycles -= t;
}

/*
 *  Save the mach dependent part of the process state.
 */
void
procsave(Proc *p)
{
	uvlong t;

	cycles(&t);
	p->kentry -= t;
	p->pcycles += t;

	if(p->fpstate == FPactive){
		if(p->state == Moribund)
			fpclear();
		else{
			/*
			 * Fpsave() stores without handling pending
			 * unmasked exeptions. Postnote() can't be called
			 * here as sleep() already has up->rlock, so
			 * the handling of pending exceptions is delayed
			 * until the process runs again and generates an
			 * emulation fault to activate the FPU.
			 */
			fpsave(&p->fpsave);
		}
		p->fpstate = FPinactive;
	}

	/*
	 * While this processor is in the scheduler, the process could run
	 * on another processor and exit, returning the page tables to
	 * the free list where they could be reallocated and overwritten.
	 * When this processor eventually has to get an entry from the
	 * trashed page tables it will crash.
	 *
	 * If there's only one processor, this can't happen.
	 * You might think it would be a win not to do this in that case,
	 * especially on VMware, but it turns out not to matter.
	 */
	mmuflushtlb(PADDR(m->pdb));
}

static void
shutdown(int ispanic)
{
	int ms, once;

	lock(&active);
	if(ispanic)
		active.ispanic = ispanic;
	else if(m->machno == 0 && (active.machs & (1<<m->machno)) == 0)
		active.ispanic = 0;
	once = active.machs & (1<<m->machno);
	/*
	 * setting exiting will make hzclock() on each processor call exit(0),
	 * which calls shutdown(0) and arch->reset(), which on mp systems calls
	 * mpshutdown(), from which there is no return: the processor is idled
	 * or initiates a reboot.  clearing our bit in machs avoids calling
	 * exit(0) from hzclock() on this processor.
	 */
	active.machs &= ~(1<<m->machno);
	active.exiting = 1;
	unlock(&active);

	if(once)
		iprint("cpu%d: exiting\n", m->machno);

	/* wait for any other processors to shutdown */
	spllo();
	for(ms = 5*1000; ms > 0; ms -= TK2MS(2)){
		delay(TK2MS(2));
		if(active.machs == 0 && consactive() == 0)
			break;
	}

	if(active.ispanic){
		if(!cpuserver)
			for(;;)
				halt();
		if(getconf("*debug"))
			delay(5*60*1000);
		else
			delay(10000);
	}
}

void
reboot(void *entry, void *code, ulong size)
{
	void (*f)(ulong, ulong, ulong);
	ulong *pdb;

	writeconf();

	/*
	 * the boot processor is cpu0.  execute this function on it
	 * so that the new kernel has the same cpu0.  this only matters
	 * because the hardware has a notion of which processor was the
	 * boot processor and we look at it at start up.
	 */
	if (m->machno != 0) {
		procwired(up, 0);
		sched();
	}
	shutdown(0);

	iprint("shutting down...\n");
	delay(200);

	splhi();

	/* turn off buffered serial console */
	serialoq = nil;

	/* shutdown devices */
	chandevshutdown();
	arch->introff();

	/*
	 * Modify the machine page table to directly map the low 4MB of memory
	 * This allows the reboot code to turn off the page mapping
	 */
	pdb = m->pdb;
	pdb[PDX(0)] = pdb[PDX(KZERO)];
	mmuflushtlb(PADDR(pdb));

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));

	/* off we go - never to return */
	coherence();
	(*f)((ulong)entry & ~0xF0000000UL, PADDR(code), size);
}


void
exit(int ispanic)
{
	shutdown(ispanic);
	arch->reset();
}
