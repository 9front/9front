#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "init.h"
#include "sysreg.h"
#include "reboot.h"

#include <pool.h>
#include <libsec.h>

Conf conf;

/*
 *  starting place for first process
 */
void
init0(void)
{
	char buf[2*KNAMELEN], **sp;

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
		snprint(buf, sizeof(buf), "%s %s", "ARM64", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "arm64", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		snprint(buf, sizeof(buf), "-a %s", getethermac());
		ksetenv("etherargs", buf, 0);

		/* convert plan9.ini variables to #e and #ec */
		setconfenv();
		poperror();
	}
	kproc("alarm", alarmkproc, 0);

	sp = (char**)(USTKTOP-sizeof(Tos) - 8 - sizeof(sp[0])*4);
	sp[3] = sp[2] = sp[1] = nil;
	strcpy(sp[1] = (char*)&sp[4], "boot");
	sp[0] = (void*)&sp[1];

	touser((uintptr)sp);

	assert(0);			/* shouldn't have returned */
}

/*
 *  create the first process
 */
void
userinit(void)
{
	Proc *p;
	Segment *s;
	KMap *k;
	Page *pg;

	/* no processes yet */
	up = nil;

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

	/*
	 * Kernel Stack
	 */
	p->sched.pc = (uintptr)init0;
	p->sched.sp = (uintptr)p->kstack+KSTACK-sizeof(up->s.args)-sizeof(uintptr);
	p->sched.sp = STACKALIGN(p->sched.sp);
	*(void**)p->sched.sp = kproc; // fake

	/*
	 * User Stack
	 *
	 * Technically, newpage can't be called here because it
	 * should only be called when in a user context as it may
	 * try to sleep if there are no pages available, but that
	 * shouldn't be the case here.
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	s->flushme++;
	p->seg[SSEG] = s;
	pg = newpage(1, 0, USTKTOP-BY2PG);
	segpage(s, pg);
	k = kmap(pg);
	memset((void*)VA(k), 0, BY2PG);
	kunmap(k);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, 1);
	p->seg[TSEG] = s;
	pg = newpage(1, 0, UTZERO);
	pg->txtflush = ~0;
	segpage(s, pg);
	k = kmap(pg);
	memmove((void*)VA(k), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

void
confinit(void)
{
	int i, userpcnt;
	ulong kpages, memsize = 0;
	uintptr pa;
	char *p;

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

	if(p = getconf("*maxmem"))
		memsize = strtoul(p, 0, 0) - PHYSDRAM;
	if (memsize < 16*MB)		/* sanity */
		memsize = 16*MB;
	getramsize(&conf.mem[0]);
	if(conf.mem[0].limit == 0){
		conf.mem[0].base = PHYSDRAM;
		conf.mem[0].limit = PHYSDRAM + memsize;
	}else if(p != nil)
		conf.mem[0].limit = conf.mem[0].base + memsize;

	conf.npage = 0;
	pa = PADDR(PGROUND((uintptr)end));

	/*
	 *  we assume that the kernel is at the beginning of one of the
	 *  contiguous chunks of memory and fits therein.
	 */
	for(i=0; i<nelem(conf.mem); i++){
		/* take kernel out of allocatable space */
		if(pa > conf.mem[i].base && pa < conf.mem[i].limit)
			conf.mem[i].base = pa;

		conf.mem[i].npage = (conf.mem[i].limit - conf.mem[i].base)/BY2PG;
		conf.npage += conf.mem[i].npage;
	}

	if(userpcnt < 10)
		userpcnt = 60 + cpuserver*10;
	kpages = conf.npage - (conf.npage*userpcnt)/100;

	/*
	 * can't go past the end of virtual memory
	 * (uintptr)-KZERO is 2^32 - KZERO
	 */
	if(kpages > ((uintptr)-KZERO)/BY2PG)
		kpages = ((uintptr)-KZERO)/BY2PG;

	conf.upages = conf.npage - kpages;
	conf.ialloc = (kpages/2)*BY2PG;

	conf.nmach = getncpus();

	/* set up other configuration parameters */
	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = conf.nmach > 1;

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for.
	 */
	kpages = conf.npage - conf.upages;
	kpages *= BY2PG;
	kpages -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap
		+ conf.nswppo*sizeof(Page*);
	mainmem->maxsize = kpages;
	if(!cpuserver)
		/*
		 * give terminals lots of image memory, too; the dynamic
		 * allocation will balance the load properly, hopefully.
		 * be careful with 32-bit overflow.
		 */
		imagmem->maxsize = kpages;
}

void
machinit(void)
{
	m->ticks = 1;
	m->perf.period = 1;
	active.machs[m->machno] = 1;
}

void
mpinit(void)
{
	extern void _start(void);
	int i;

	for(i = 0; i < MAXMACH; i++)
		((uintptr*)SPINTABLE)[i] = 0;

	for(i = 1; i < conf.nmach; i++)
		MACHP(i)->machno = i;

	coherence();

	for(i = 1; i < conf.nmach; i++)
		((uintptr*)SPINTABLE)[i] = PADDR(_start);

	cachedwbinvse((void*)SPINTABLE, MAXMACH*8);
	sev();
	delay(100);
	sev();
	synccycles();

	for(i = 0; i < MAXMACH; i++)
		((uintptr*)SPINTABLE)[i] = 0;
}

void
main(void)
{
	machinit();
	if(m->machno){
		trapinit();
		fpuinit();
		clockinit();
		cpuidprint();
		synccycles();
		timersinit();
		flushtlb();
		mmu1init();
		m->ticks = MACHP(0)->ticks;
		schedinit();
		return;
	}
	quotefmtinstall();
	bootargsinit();
	confinit();
	xinit();
	printinit();
	uartconsinit();
	screeninit();
	print("\nPlan 9\n");
	xsummary();

	/* set clock rate to arm_freq from config.txt */
	setclkrate(ClkArm, 0);

	trapinit();
	fpuinit();
	clockinit();
	cpuidprint();
	timersinit();
	pageinit();
	procinit0();
	initseg();
	links();
	chandevreset();
	userinit();
	mpinit();
	mmu0clear((uintptr*)L1);
	flushtlb();
	mmu1init();
	schedinit();
}

static void
rebootjump(void *entry, void *code, ulong size)
{
	void (*f)(void*, void*, ulong);

	intrsoff();
	intrcpushutdown();

	/* redo identity map */
	mmuidmap((uintptr*)L1);

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));
	cachedwbinvse(f, sizeof(rebootcode));
	cacheiinvse(f, sizeof(rebootcode));

	(*f)(entry, code, size);

	for(;;);
}

void
exit(int)
{
	cpushutdown();
	splfhi();
	if(m->machno == 0)
		archreboot();
	rebootjump(0, 0, 0);
}

void
reboot(void *entry, void *code, ulong size)
{
	writeconf();
	while(m->machno != 0){
		procwired(up, 0);
		sched();
	}

	cpushutdown();
	delay(2000);

	splfhi();

	/* turn off buffered serial console */
	serialoq = nil;

	/* shutdown devices */
	chandevshutdown();

	/* stop the clock (and watchdog if any) */
	clockshutdown();
	wdogoff();

	/* off we go - never to return */
	rebootjump(entry, code, size);
}

/*
 * stub for ../omap/devether.c
 */
int
isaconfig(char *, int, ISAConf *)
{
	return 0;
}
