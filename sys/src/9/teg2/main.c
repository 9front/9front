#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include <pool.h>

#include "arm.h"
#include "rebootcode.i"

/*
 * Where configuration info is left for the loaded programme.
 * This will turn into a structure as more is done by the boot loader
 * (e.g. why parse the .ini file twice?).
 * There are 3584 bytes available at CONFADDR.
 */
#define BOOTARGS	((char*)CONFADDR)
#define	BOOTARGSLEN	(16*KiB)		/* limit in devenv.c */
#define	MAXCONF		64
#define MAXCONFLINE	160

enum {
	Minmem	= 256*MB,			/* conservative default */
};

#define isascii(c) ((uchar)(c) > 0 && (uchar)(c) < 0177)

extern char bdata[], edata[], end[], etext[];

uintptr kseg0 = KZERO;
Mach* machaddr[MAXMACH];
uchar *l2pages;

Memcache cachel[8];		/* arm arch v7 supports 1-7 */
/*
 * these are used by the cache.v7.s routines.
 */
Lowmemcache *cacheconf;

int vflag;
int normalprint;
char debug[256];

static Lock testlock;

/* store plan9.ini contents here at least until we stash them in #ec */
static char confname[MAXCONF][KNAMELEN];
static char confval[MAXCONF][MAXCONFLINE];
static int nconf;

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
//	confval[i] = val;
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

/*
 * assumes that we have loaded our /cfg/pxe/mac file at CONFADDR
 * (usually 0x1000) with tftp in u-boot.  no longer uses malloc, so
 * can be called early.
 */
static void
plan9iniinit(void)
{
	char *k, *v, *next;

	k = (char *)CONFADDR;
	if(!isascii(*k))
		return;

	for(; k && *k != '\0'; k = next) {
		if (!isascii(*k))		/* sanity check */
			break;
		next = strchr(k, '\n');
		if (next)
			*next++ = '\0';

		if (*k == '\0' || *k == '\n' || *k == '#')
			continue;
		v = strchr(k, '=');
		if(v == nil)
			continue;		/* mal-formed line */
		*v++ = '\0';

		addconf(k, v);
	}
}

/* enable scheduling of this cpu */
void
machon(uint cpu)
{
	lock(&active);
	if (active.machs[cpu] == 0) {	/* currently off? */
		active.machs[cpu] = 1;
		conf.nmach++;
	}
	unlock(&active);
}

/* disable scheduling of this cpu */
void
machoff(uint cpu)
{
	lock(&active);
	if (active.machs[cpu]) {		/* currently on? */
		active.machs[cpu] = 0;
		conf.nmach--;
	}
	unlock(&active);
}

void
machinit(void)
{
	Mach *m0;

	if (m == 0) {
		serialputc('?');
		serialputc('m');
		serialputc('0');
	}
	if(machaddr[m->machno] != m) {
		serialputc('?');
		serialputc('m');
		serialputc('m');
	}

	if (canlock(&testlock)) {
		serialputc('?');
		serialputc('l');
		panic("cpu%d: locks don't work", m->machno);
	}

	m->ticks = 1;
	m->perf.period = 1;
	m0 = MACHP(0);
	if (m->machno != 0) {
		/* synchronise with cpu 0 */
		m->ticks = m0->ticks;
		m->fastclock = m0->fastclock;
		m->cpuhz = m0->cpuhz;
		m->delayloop = m0->delayloop;
	}
	if (m->machno != 0 &&
	    (m->fastclock == 0 || m->cpuhz == 0 || m->delayloop == 0))
		panic("buggered cpu 0 Mach");

	machon(m->machno);
	fpoff();
}

/* l.s has already zeroed Mach, which now contains our stack. */
void
mach0init(void)
{
	if (m == 0) {
		serialputc('?');
		serialputc('m');
	}
	conf.nmach = 0;

	m->machno = 0;
	machaddr[0] = m;

	lock(&testlock);		/* hold this forever */
	machinit();

	active.exiting = 0;
	l1cache->wbse(&active, sizeof active);
	up = nil;
}

/*
 *  count CPU's, set up their mach structures and l1 ptes.
 *  we're running on cpu 0 and our data structures were
 *  statically allocated.
 */
void
launchinit(void)
{
	int mach;
	Mach *mm;
	PTE *l1;

	for(mach = 1; mach < MAXMACH; mach++){
		machaddr[mach] = mm = mallocalign(MACHSIZE, MACHSIZE, 0, 0);
		l1 = mallocalign(L1SIZE, L1SIZE, 0, 0);
		if(mm == nil || l1 == nil)
			panic("launchinit");
		memset(mm, 0, MACHSIZE);
		mm->machno = mach;

		memmove(l1, (void *)L1, L1SIZE);  /* clone cpu0's l1 table */
		l1cache->wbse(l1, L1SIZE);

		mm->mmul1 = l1;
		l1cache->wbse(mm, MACHSIZE);
	}
	l1cache->wbse(machaddr, sizeof machaddr);
	conf.nmach = 1;
}

void
dump(void *vaddr, int words)
{
	ulong *addr;

	addr = vaddr;
	while (words-- > 0)
		iprint("%.8lux%c", *addr++, words % 8 == 0? '\n': ' ');
}

static void
cacheinit(void)
{
	allcacheinfo(cachel);
	cacheconf = (Lowmemcache *)CACHECONF;
	cacheconf->l1waysh = cachel[1].waysh;
	cacheconf->l1setsh = cachel[1].setsh;
	/* on the tegra 2, l2 is unarchitected */
	cacheconf->l2waysh = cachel[2].waysh;
	cacheconf->l2setsh = cachel[2].setsh;

	l2pl310init();
	allcacheson();
	allcache->wb();
}

void
l2pageinit(void)
{
	l2pages = KADDR(PHYSDRAM + DRAMSIZE - RESRVDHIMEM);
}

/*
 * at entry, l.s has set m for cpu0 and printed "Plan 9 from Be"
 * but has not zeroed bss.
 */
void
main(void)
{
	int cpu;
	static ulong vfy = 0xcafebabe;

	up = nil;
	if (vfy != 0xcafebabe) {
		serialputc('?');
		serialputc('d');
		panic("data segment misaligned");
	}

	memset(edata, 0, end - edata);

	/*
	 * we can't lock until smpon has run, but we're supposed to wait
	 * until l1 & l2 are on.  too bad.  l1 is on, l2 will soon be.
	 */
	smpon();
	iprint("ll Labs ");
	cacheinit();

	/*
	 * data segment is aligned, bss is zeroed, caches' characteristics
	 * are known.  begin initialisation.
	 */
	mach0init();
	l2pageinit();
	mmuinit();

	quotefmtinstall();

	/* want plan9.ini to be able to affect memory sizing in confinit */
	plan9iniinit();		/* before we step on plan9.ini in low memory */

	/* l2 looks for *l2off= in plan9.ini */
	l2cache->on();		/* l2->on requires locks to work, thus smpon */
	l2cache->info(&cachel[2]);
	allcache->on();

	cortexa9cachecfg();

	trapinit();		/* so confinit can probe memory to size it */
	confinit();		/* figures out amount of memory */
	/* xinit prints (if it can), so finish up the banner here. */
	delay(100);
	navailcpus = getncpus();
	iprint("(mp arm; %d cpus)\n\n", navailcpus);
	delay(100);

	for (cpu = 1; cpu < navailcpus; cpu++)
		stopcpu(cpu);

	xinit();
	irqtooearly = 0;	/* now that xinit and trapinit have run */

	mainmem->flags |= POOL_ANTAGONISM /* | POOL_PARANOIA */ ;

	/*
	 * Printinit will cause the first malloc call.
	 * (printinit->qopen->malloc) unless any of the
	 * above (like clockinit) do an irqenable, which
	 * will call malloc.
	 * If the system dies here it's probably due
	 * to malloc(->xalloc) not being initialised
	 * correctly, or the data segment is misaligned
	 * (it's amazing how far you can get with
	 * things like that completely broken).
	 *
	 * (Should be) boilerplate from here on.
	 */

	archreset();			/* cfg clock signals, print cache cfg */
	clockinit();			/* start clocks */
	timersinit();

	delay(50);			/* let uart catch up */
	printinit();

	cpuidprint();
	chkmissing();

	procinit0();
	initseg();

//	dmainit();
	links();
	conf.monitor = 1;
//	screeninit();

	iprint("pcireset...");
	pcireset();			/* this tends to hang after a reboot */
	iprint("ok\n");

	chandevreset();			/* most devices are discovered here */
//	i8250console();			/* too early; see init0 */

	pageinit();			/* prints "1020M memory: ⋯ */
	userinit();

	/*
	 * starting a cpu will eventually result in it calling schedinit,
	 * so everything necessary to run user processes should be set up
	 * before starting secondary cpus.
	 */
	launchinit();
	/* SMP & FW are already on when we get here; u-boot set them? */
	for (cpu = 1; cpu < navailcpus; cpu++)
		if (startcpu(cpu) < 0)
			panic("cpu%d didn't start", cpu);
	l1diag();

	schedinit();
	panic("cpu%d: schedinit returned", m->machno);
}

/*
 *  exit kernel either on a panic or user request
 */
void
exit(int)
{
	cpushutdown();
	splhi();
	if (m->machno == 0)
		archreboot();
	else {
		intrcpushutdown();
		stopcpu(m->machno);
		for (;;)
			idlehands();
	}
}

int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	char cc[32], *p, *x;
	int i;

	snprint(cc, sizeof cc, "%s%d", class, ctlrno);
	p = getconf(cc);
	if(p == nil)
		return 0;

	x = nil;
	kstrdup(&x, p);
	p = x;

	isa->type = "";
	isa->nopt = tokenize(p, isa->opt, NISAOPT);
	for(i = 0; i < isa->nopt; i++){
		p = isa->opt[i];
		if(cistrncmp(p, "type=", 5) == 0)
			isa->type = p + 5;
		else if(cistrncmp(p, "port=", 5) == 0)
			isa->port = strtoul(p+5, &p, 0);
		else if(cistrncmp(p, "irq=", 4) == 0)
			isa->irq = strtoul(p+4, &p, 0);
		else if(cistrncmp(p, "dma=", 4) == 0)
			isa->dma = strtoul(p+4, &p, 0);
		else if(cistrncmp(p, "mem=", 4) == 0)
			isa->mem = strtoul(p+4, &p, 0);
		else if(cistrncmp(p, "size=", 5) == 0)
			isa->size = strtoul(p+5, &p, 0);
		else if(cistrncmp(p, "freq=", 5) == 0)
			isa->freq = strtoul(p+5, &p, 0);
	}
	return 1;
}

/*
 * the new kernel is already loaded at address `code'
 * of size `size' and entry point `entry'.
 */
void
reboot(void *entry, void *code, ulong size)
{
	void (*f)(ulong, ulong, ulong);

	writeconf();

	/*
	 * the boot processor is cpu0.  execute this function on it
	 * so that the new kernel has the same cpu0.
	 */
	if (m->machno != 0) {
		procwired(up, 0);
		sched();
	}
	cpushutdown();

	/*
	 * should be the only processor running now
	 */
	pcireset();

	/* turn off buffered serial console */
	serialoq = nil;
	kprintoq = nil;
	screenputs = nil;

	/* shutdown devices */
	chandevshutdown();

	/* call off the dog */
	clockshutdown();

	splhi();
	intrshutdown();

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));
	cachedwb();
	l2cache->wbinv();
	l2cache->off();
	cacheuwbinv();

	/* off we go - never to return */
	(*f)(PADDR(entry), PADDR(code), size);
}

/*
 *  starting place for first process
 */
void
init0(void)
{
	char buf[2*KNAMELEN], **sp;
	int i;

	chandevinit();
	i8250console();		/* might be redundant, but harmless */
	if(serialoq == nil)
		panic("init0: nil serialoq");
	normalprint = 1;

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", "ARM", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "arm", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);

		/* convert plan9.ini variables to #e and #ec */
		for(i = 0; i < nconf; i++) {
			ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);

	sp = (char**)(USTKTOP - sizeof(Tos) - 8 - sizeof(sp[0])*4);
	sp[3] = sp[2] = sp[1] = nil;
	strcpy(sp[0] = (char*)&sp[4], "boot");
	touser((uintptr)sp);
}

Conf conf;			/* XXX - must go - gag */

Confmem tsmem[nelem(conf.mem)] = {
	/*
	 * Memory available to Plan 9:
	 */
	{ .base = PHYSDRAM, .limit = PHYSDRAM + Minmem, },
};
ulong memsize = DRAMSIZE;

static int
gotmem(uintptr sz)
{
	uintptr addr;

	/* back off a little from the end */
	addr = (uintptr)KADDR(PHYSDRAM + sz - BY2WD);
	if (probeaddr(addr) >= 0) {	/* didn't trap? memory present */
		memsize = sz;
		return 0;
	}
	return -1;
}

void
confinit(void)
{
	int i;
	ulong kpages;
	uintptr pa;
	char *p;

	if(p = getconf("service")){
		if(strcmp(p, "cpu") == 0)
			cpuserver = 1;
		else if(strcmp(p,"terminal") == 0)
			cpuserver = 0;
	}

	/*
	 * Copy the physical memory configuration to Conf.mem.
	 */
	if(nelem(tsmem) > nelem(conf.mem)){
		iprint("memory configuration botch\n");
		exit(1);
	}
	if(0 && (p = getconf("*maxmem")) != nil) {
		memsize = strtoul(p, 0, 0) - PHYSDRAM;
		if (memsize < 16*MB)		/* sanity */
			memsize = 16*MB;
	}

	/*
	 * see if all that memory exists; if not, find out how much does.
	 * trapinit must have been called first.
	 */
	if (gotmem(memsize - RESRVDHIMEM) < 0)
		panic("can't find 1GB of memory");

	tsmem[0].limit = PHYSDRAM + memsize;
	memmove(conf.mem, tsmem, sizeof(tsmem));

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

	conf.upages = (conf.npage*80)/100;
	conf.ialloc = ((conf.npage-conf.upages)/2)*BY2PG;

	/* set up other configuration parameters */
	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;

	/*
	 * it's simpler on mp systems to take page-faults early,
	 * on reference, rather than later, on write, which might
	 * require tlb shootdowns.
	 */
	conf.copymode = 1;		/* copy on reference */

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
advertwfi(void)			/* advertise my wfi status */
{
	ilock(&active);
	active.wfi |= 1 << m->machno;
	iunlock(&active);
}

void
unadvertwfi(void)		/* do not advertise my wfi status */
{
	ilock(&active);
	active.wfi &= ~(1 << m->machno);
	iunlock(&active);
}

void
idlehands(void)
{
#ifdef use_ipi
	int advertised;

	/* don't go into wfi until my local timer is ticking */
	if (m->ticks <= 1)
		return;

	advertised = 0;
	m->inidlehands++;
	/* avoid recursion via ilock, advertise iff this cpu is initialised */
	if (m->inidlehands == 1 && m->syscall > 0) {
		advertwfi();
		advertised = 1;
	}

	wfi();

	if (advertised)
		unadvertwfi();
	m->inidlehands--;
#endif
}

void
wakewfi(void)
{
#ifdef use_ipi
	uint cpu;

	/*
	 * find any cpu other than me currently in wfi.
	 * need not be exact.
	 */
	cpu = BI2BY*BY2WD - 1 - clz(active.wfi & ~(1 << m->machno));
	if (cpu < MAXMACH)
		intrcpu(cpu);
#endif
}

void
setupwatchpts(Proc *, Watchpt *, int n)
{
	if(n > 0)
		error("no watchpoints");
}
