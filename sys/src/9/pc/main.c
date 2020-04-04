#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"pool.h"
#include	"rebootcode.i"

Mach *m;
Conf conf;

int delaylink;
int idle_spin;

extern void (*i8237alloc)(void);
extern void bootscreeninit(void);
extern void multibootdebug(void);

void
main(void)
{
	mach0init();
	bootargsinit();
	ioinit();
	i8250console();
	quotefmtinstall();
	screeninit();

	print("\nPlan 9\n");
	
	trapinit0();
	i8253init();
	cpuidentify();
	meminit0();
	archinit();
	meminit();
	ramdiskinit();
	confinit();
	xinit();
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
	if(arch->clockenable)
		arch->clockenable();
	procinit0();
	initseg();
	if(delaylink){
		bootlinks();
		pcimatch(0, 0, 0);
	}else
		links();
	chandevreset();
	pageinit();
	userinit();
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

	active.machs[0] = 1;
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
	char buf[2*KNAMELEN], **sp;

	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", arch->id, conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "386", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		setconfenv();
		poperror();
	}
	kproc("alarm", alarmkproc, 0);

	sp = (char**)(USTKTOP - sizeof(Tos) - 8 - sizeof(sp[0])*4);
	sp[3] = sp[2] = nil;
	strcpy(sp[1] = (char*)&sp[4], "boot");
	sp[0] = nil;
	touser(sp);
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
		conf.nimage = conf.nproc;

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
 * we keep FPsave structure in SSE format emulating FXSAVE / FXRSTOR
 * instructions for legacy x87 fpu.
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
	fpsave(up->fpsave);
	up->fpstate = FPinactive;
	mathnote(up->fpsave->fsw, up->fpsave->fpuip);
}

/*
 *  SIMD error
 */
static void
simderror(Ureg *ureg, void*)
{
	fpsave(up->fpsave);
	up->fpstate = FPinactive;
	mathnote(up->fpsave->mxcsr & 0x3f, ureg->pc);
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
			ldmxcsr(0x1f80);	/* no simd exceptions on 386 */
		while(up->fpsave == nil)
			up->fpsave = mallocalign(sizeof(FPsave), FPalign, 0, 0);
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
		status = up->fpsave->fsw;
		control = up->fpsave->fcw;
		if((status & ~control) & 0x07F){
			mathnote(status, up->fpsave->fpuip);
			break;
		}
		fprestore(up->fpsave);
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
	if(m->cpuidfamily == 3)
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

	memset(p->gdt, 0, sizeof(p->gdt));
	p->nldt = 0;
	
	/* clear debug registers */
	memset(p->dr, 0, sizeof(p->dr));
	if(m->dr7 != 0){
		m->dr7 = 0;
		putdr7(0);
	}

	cycles(&p->kentry);
	p->pcycles = -p->kentry;
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
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	case FPinactive:
		while(p->fpsave == nil)
			p->fpsave = mallocalign(sizeof(FPsave), FPalign, 0, 0);
		memmove(p->fpsave, up->fpsave, sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	splx(s);
}

void
procrestore(Proc *p)
{
	uvlong t;
	
	if(p->dr[7] != 0){
		m->dr7 = p->dr[7];
		putdr(p->dr);
	}
	
	if(p->vmx != nil)
		vmxprocrestore(p);

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

	/* we could just always putdr7(0) but accessing DR7 might be slow in a VM */
	if(m->dr7 != 0){
		m->dr7 = 0;
		putdr7(0);
	}
	if(p->state == Moribund)
		p->dr[7] = 0;

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
			fpsave(p->fpsave);
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
rebootjump(uintptr entry, uintptr code, ulong size)
{
	void (*f)(uintptr, uintptr, ulong);
	ulong *pdb;

	splhi();
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
	(*f)(entry, code, size);
	for(;;);
}


void
exit(int)
{
	cpushutdown();
	if(m->machno)
		rebootjump(0, 0, 0);
	arch->reset();
}

void
reboot(void *entry, void *code, ulong size)
{
	writeconf();
	vmxshutdown();

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
	cpushutdown();
	delay(1000);
	splhi();

	/* turn off buffered serial console */
	serialoq = nil;

	/* shutdown devices */
	chandevshutdown();

	rebootjump((ulong)entry & ~0xF0000000UL, PADDR(code), size);
}
