#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include <pool.h>

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

uintptr kseg0 = KZERO;
Mach* machaddr[MAXMACH];

int vflag;
int normalprint;
char debug[256];

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
 * assumes that we have loaded our /cfg/pxe/mac file at 0x1000 with
 * tftp in u-boot.  no longer uses malloc, so can be called early.
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

void
main(void)
{
//	int i;
	extern char bdata[], edata[], end[], etext[];
	static ulong vfy = 0xcafebabe;

	/* l.s has already printed "Plan 9 from Be" */
//	m = mach;					/* now done in l.s */

	/* realign data seg; apparently -H0 -R4096 does not pad the text seg */
	if (vfy != 0xcafebabe) {
//		wave('<'); wave('-');
		memmove(bdata, etext, edata - bdata);
	}
	/*
	 * once data segment is in place, always zero bss since we may
	 * have been loaded by another Plan 9 kernel.
	 */
	memset(edata, 0, end - edata);		/* zero BSS */
	cacheuwbinv();
	l2cacheuwbinv();

	if (vfy != 0xcafebabe)
		panic("data segment misaligned");
	vfy = 0;

wave('l');
	machinit();
	mmuinit();

	quotefmtinstall();

	/* want plan9.ini to be able to affect memory sizing in confinit */
	plan9iniinit();		/* before we step on plan9.ini in low memory */

	trapinit();		/* so confinit can probe memory to size it */
	confinit();		/* figures out amount of memory */
	/* xinit prints (if it can), so finish up the banner here. */
	delay(500);
	iprint("l Labs\n\n");
	delay(500);
	xinit();

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

	archreset();			/* configure clock signals */
	clockinit();			/* start clocks */
	timersinit();
	watchdoginit();

	delay(250);			/* let uart catch up */
	printinit();
//	kbdenable();

	cpuidprint();
//	chkmissing();

	procinit0();
	initseg();

	dmainit();
	links();
	conf.monitor = 1;
	screeninit();
	chandevreset();			/* most devices are discovered here */

//	i8250console();			/* too early; see init0 */

	pageinit();
	userinit();
	schedinit();
}

void
machinit(void)
{
	if (m == 0)
		wave('?');
//	memset(m, 0, sizeof(Mach));	/* done by l.s, now contains stack */
	m->machno = 0;
	machaddr[m->machno] = m;

	m->ticks = 1;
	m->perf.period = 1;

	conf.nmach = 1;

	active.machs[0] = 1;
	active.exiting = 0;

	up = nil;
}

/*
 *  exit kernel either on a panic or user request
 */
void
exit(int)
{
	cpushutdown();
	splhi();
	archreboot();
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
	cpushutdown();

	/* turn off buffered serial console */
	serialoq = nil;
	kprintoq = nil;
	screenputs = nil;

	/* shutdown devices */
	chandevshutdown();

	/* call off the dog */
	clockshutdown();

	splhi();
	intrsoff();

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));
	cacheuwbinv();
	l2cacheuwbinv();

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

	dmatest();		/* needs `up' set, so can't do it earlier */
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

Confmem omapmem[nelem(conf.mem)] = {
	/*
	 * Memory available to Plan 9:
	 */
	{ .base = PHYSDRAM, .limit = PHYSDRAM + Minmem, },
};
ulong memsize = Minmem;

static int
gotmem(uintptr sz)
{
	uintptr addr;

	addr = PHYSDRAM + sz - BY2WD;
	mmuidmap(addr, 1);
	if (probeaddr(addr) >= 0) {
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

	/*
	 * Copy the physical memory configuration to Conf.mem.
	 */
	if(nelem(omapmem) > nelem(conf.mem)){
		iprint("memory configuration botch\n");
		exit(1);
	}
	if((p = getconf("*maxmem")) != nil) {
		memsize = strtoul(p, 0, 0) - PHYSDRAM;
		if (memsize < 16*MB)		/* sanity */
			memsize = 16*MB;
	}

	/*
	 * see if all that memory exists; if not, find out how much does.
	 * trapinit must have been called first.
	 */
	if (gotmem(memsize) < 0 && gotmem(256*MB) < 0 && gotmem(128*MB) < 0) {
		iprint("can't find any memory, assuming %dMB\n", Minmem / MB);
		memsize = Minmem;
	}

	omapmem[0].limit = PHYSDRAM + memsize;
	memmove(conf.mem, omapmem, sizeof(omapmem));

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

	/* only one processor */
	conf.nmach = 1;

	/* set up other configuration parameters */
	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = 0;		/* copy on write */

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

//	archconfinit();
}

int
cmpswap(long *addr, long old, long new)
{
	return cas32(addr, old, new);
}

void
setupwatchpts(Proc *, Watchpt *, int n)
{
	if(n > 0)
		error("no watchpoints");
}
