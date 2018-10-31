#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "init.h"
#include <pool.h>

#include "reboot.h"

/* Firmware compatibility */
#define	Minfirmrev	326770
#define	Minfirmdate	"22 Jul 2012"

/*
 * Where configuration info is left for the loaded programme.
 */
#define BOOTARGS	((char*)CONFADDR)
#define	BOOTARGSLEN	(REBOOTADDR-PADDR(CONFADDR))
#define	MAXCONF		64
#define MAXCONFLINE	160

uintptr kseg0 = KZERO;
Mach*	machaddr[MAXMACH];
Conf	conf;
ulong	memsize = 128*1024*1024;

/*
 * Option arguments from the command line.
 * oargv[0] is the boot file.
 */
static int oargc;
static char* oargv[20];
static char oargb[128];
static int oargblen;

static uintptr sp;		/* XXX - must go - user stack of init proc */

/* store plan9.ini contents here at least until we stash them in #ec */
static char confname[MAXCONF][KNAMELEN];
static char confval[MAXCONF][MAXCONFLINE];
static int nconf;

typedef struct Atag Atag;
struct Atag {
	u32int	size;	/* size of atag in words, including this header */
	u32int	tag;	/* atag type */
	union {
		u32int	data[1];	/* actually [size-2] */
		/* AtagMem */
		struct {
			u32int	size;
			u32int	base;
		} mem;
		/* AtagCmdLine */
		char	cmdline[1];	/* actually [4*(size-2)] */
	};
};

enum {
	AtagNone	= 0x00000000,
	AtagCore	= 0x54410001,
	AtagMem		= 0x54410002,
	AtagCmdline	= 0x54410009,
};

static int
findconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return i;
	return -1;
}

char*
getconf(char *name)
{
	int i;

	i = findconf(name);
	if(i >= 0)
		return confval[i];
	return nil;
}

void
addconf(char *name, char *val)
{
	int i;

	i = findconf(name);
	if(i < 0){
		if(val == nil || nconf >= MAXCONF)
			return;
		i = nconf++;
		strecpy(confname[i], confname[i]+sizeof(confname[i]), name);
	}
	strecpy(confval[i], confval[i]+sizeof(confval[i]), val);
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
	memmove(BOOTARGS, p, n);
	memset(BOOTARGS + n, '\n', BOOTARGSLEN - n);
	poperror();
	free(p);
}

static void
plan9iniinit(char *s, int cmdline)
{
	char *toks[MAXCONF];
	int i, c, n;
	char *v;

	if((c = *s) < ' ' || c >= 0x80)
		return;
	if(cmdline)
		n = tokenize(s, toks, MAXCONF);
	else
		n = getfields(s, toks, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(toks[i][0] == '#')
			continue;
		v = strchr(toks[i], '=');
		if(v == nil)
			continue;
		*v++ = '\0';
		addconf(toks[i], v);
	}
}

static void
ataginit(Atag *a)
{
	int n;

	if(a->tag != AtagCore){
		plan9iniinit((char*)a, 0);
		return;
	}
	while(a->tag != AtagNone){
		switch(a->tag){
		case AtagMem:
			/* use only first bank */
			if(conf.mem[0].limit == 0 && a->mem.size != 0){
				memsize = a->mem.size;
				conf.mem[0].base = a->mem.base;
				conf.mem[0].limit = a->mem.base + memsize;
			}
			break;
		case AtagCmdline:
			n = (a->size * sizeof(u32int)) - offsetof(Atag, cmdline[0]);
			if(a->cmdline + n < BOOTARGS + BOOTARGSLEN)
				a->cmdline[n] = 0;
			else
				BOOTARGS[BOOTARGSLEN-1] = 0;
			plan9iniinit(a->cmdline, 1);
			break;
		}
		a = (Atag*)((u32int*)a + a->size);
	}
}

void
machinit(void)
{
	Mach *m0;

	m->ticks = 1;
	m->perf.period = 1;
	m0 = MACHP(0);
	if (m->machno != 0) {
		/* synchronise with cpu 0 */
		m->ticks = m0->ticks;
	}
}

void
mach0init(void)
{
	m->mmul1 = (PTE*)L1;
	m->machno = 0;
	machaddr[m->machno] = m;

	m->ticks = 1;
	m->perf.period = 1;

	active.machs[0] = 1;
	active.exiting = 0;

	up = nil;
}

static void
launchinit(void)
{
	int mach;
	Mach *mm;
	PTE *l1;

	for(mach = 1; mach < conf.nmach; mach++){
		machaddr[mach] = mm = mallocalign(MACHSIZE, MACHSIZE, 0, 0);
		l1 = mallocalign(L1SIZE, L1SIZE, 0, 0);
		if(mm == nil || l1 == nil)
			panic("launchinit");
		memset(mm, 0, MACHSIZE);
		mm->machno = mach;

		memmove(l1, m->mmul1, L1SIZE);  /* clone cpu0's l1 table */
		cachedwbse(l1, L1SIZE);
		mm->mmul1 = l1;
		cachedwbse(mm, MACHSIZE);

	}
	cachedwbse(machaddr, sizeof machaddr);
	if((mach = startcpus(conf.nmach)) < conf.nmach)
		print("only %d cpu%s started\n", mach, mach == 1? "" : "s");
}

static void
optionsinit(char* s)
{
	strecpy(oargb, oargb+sizeof(oargb), s);

	oargblen = strlen(oargb);
	oargc = tokenize(oargb, oargv, nelem(oargv)-1);
	oargv[oargc] = nil;
}

void
main(void)
{
	extern char edata[], end[];
	uint fw, board;

	m = (Mach*)MACHADDR;
	memset(edata, 0, end - edata);	/* clear bss */
	mach0init();

	optionsinit("/boot/boot boot");
	quotefmtinstall();
	
	ataginit((Atag*)BOOTARGS);
	confinit();		/* figures out amount of memory */
	xinit();
	uartconsinit();
	screeninit();

	print("\nPlan 9 from Bell Labs\n");
	board = getboardrev();
	fw = getfirmware();
	print("board rev: %#ux firmware rev: %d\n", board, fw);
	if(fw < Minfirmrev){
		print("Sorry, firmware (start*.elf) must be at least rev %d"
		      " or newer than %s\n", Minfirmrev, Minfirmdate);
		for(;;)
			;
	}
	/* set clock rate to arm_freq from config.txt (default pi1:700Mhz pi2:900MHz) */
	setclkrate(ClkArm, 0);
	trapinit();
	clockinit();
	printinit();
	timersinit();
	cpuidprint();
	archreset();
	vgpinit();

	procinit0();
	initseg();
	links();
	chandevreset();			/* most devices are discovered here */
	pageinit();
	userinit();
	launchinit();
	mmuinit1(0);
	schedinit();
	assert(0);			/* shouldn't have returned */
}

/*
 *  starting place for first process
 */
void
init0(void)
{
	int i;
	char buf[2*KNAMELEN];

	up->nerrlab = 0;
	coherence();
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
		snprint(buf, sizeof(buf), "%s %s", "ARM", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "arm", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		snprint(buf, sizeof(buf), "-a %s", getethermac());
		ksetenv("etherargs", buf, 0);

		/* convert plan9.ini variables to #e and #ec */
		for(i = 0; i < nconf; i++) {
			ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	touser(sp);
	assert(0);			/* shouldn't have returned */
}

static void
bootargs(uintptr base)
{
	int i;
	ulong ssize;
	char **av, *p;

	/*
	 * Push the boot args onto the stack.
	 * The initial value of the user stack must be such
	 * that the total used is larger than the maximum size
	 * of the argument list checked in syscall.
	 */
	i = oargblen+1;
	p = UINT2PTR(STACKALIGN(base + BY2PG - sizeof(Tos) - i));
	memmove(p, oargb, i);

	/*
	 * Now push the argv pointers.
	 * The code jumped to by touser in lproc.s expects arguments
	 *	main(char* argv0, ...)
	 * and calls
	 * 	startboot("/boot/boot", &argv0)
	 * not the usual (int argc, char* argv[])
	 */
	av = (char**)(p - (oargc+1)*sizeof(char*));
	ssize = base + BY2PG - PTR2UINT(av);
	for(i = 0; i < oargc; i++)
		*av++ = (oargv[i] - oargb) + (p - base) + (USTKTOP - BY2PG);
	*av = nil;
	sp = USTKTOP - ssize;
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
	p->sched.pc = PTR2UINT(init0);
	p->sched.sp = PTR2UINT(p->kstack+KSTACK-sizeof(up->s.args)-sizeof(uintptr));
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
	s->flushme++;
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
	p->seg[TSEG] = s;
	pg = newpage(1, 0, UTZERO);
	pg->txtflush = ~0;
	segpage(s, pg);
	k = kmap(s->map[0]->pages[0]);
	memmove(UINT2PTR(VA(k)), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

void
confinit(void)
{
	int i, userpcnt;
	ulong kpages;
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

	if((p = getconf("*maxmem")) != nil){
		memsize = strtoul(p, 0, 0) - PHYSDRAM;
		if (memsize < 16*MB)		/* sanity */
			memsize = 16*MB;
	}

	getramsize(&conf.mem[0]);
	if(conf.mem[0].limit == 0){
		conf.mem[0].base = PHYSDRAM;
		conf.mem[0].limit = PHYSDRAM + memsize;
	}else if(p != nil)
		conf.mem[0].limit = conf.mem[0].base + memsize;

	conf.npage = 0;
	pa = PADDR(PGROUND(PTR2UINT(end)));

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
	 * (ulong)-KZERO is 2^32 - KZERO
	 */
	if(kpages > ((ulong)-KZERO)/BY2PG)
		kpages = ((ulong)-KZERO)/BY2PG;

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

static void
rebootjump(void *entry, void *code, ulong size)
{
	void (*f)(void*, void*, ulong);

	intrsoff();
	intrcpushutdown();

	/* redo identity map */
	mmuinit1(1);

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));
	cacheuwbinv();

	(*f)(entry, code, size);

	for(;;);
}

/*
 *  exit kernel either on a panic or user request
 */
void
exit(int)
{
	cpushutdown();
	splfhi();
	if(m->machno == 0)
		archreboot();
	rebootjump(0, 0, 0);
}

/*
 * stub for ../omap/devether.c
 */
int
isaconfig(char *, int, ISAConf *)
{
	return 0;
}

/*
 * the new kernel is already loaded at address `code'
 * of size `size' and entry point `entry'.
 */
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

void
setupwatchpts(Proc *, Watchpt *, int n)
{
	if(n > 0)
		error("no watchpoints");
}
