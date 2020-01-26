#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "arm.h"
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

#define isascii(c) ((uchar)(c) > 0 && (uchar)(c) < 0177)

uintptr kseg0 = KZERO;
Mach* machaddr[MAXMACH];

int vflag;
char debug[256];

/* store plan9.ini contents here at least until we stash them in #ec */
static char confname[MAXCONF][KNAMELEN];
static char confval[MAXCONF][MAXCONFLINE];
static int nconf;

#ifdef CRYPTOSANDBOX
uchar sandbox[64*1024+BY2PG];
#endif

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

#include "io.h"

typedef struct Spiregs Spiregs;
struct Spiregs {
	ulong	ictl;		/* interface ctl */
	ulong	icfg;		/* interface config */
	ulong	out;		/* data out */
	ulong	in;		/* data in */
	ulong	ic;		/* interrupt cause */
	ulong	im;		/* interrupt mask */
	ulong	_pad[2];
	ulong	dwrcfg;		/* direct write config */
	ulong	dwrhdr;		/* direct write header */
};

enum {
	/* ictl bits */
	Csnact	= 1<<0,		/* serial memory activated */

	/* icfg bits */
	Bytelen	= 1<<5,		/* 2^(this_bit) bytes per transfer */
	Dirrdcmd= 1<<10,	/* flag: fast read */
};

static void
dumpbytes(uchar *bp, long max)
{
	iprint("%#p: ", bp);
	for (; max > 0; max--)
		iprint("%02.2ux ", *bp++);
	iprint("...\n");
}

static void
spiprobe(void)
{
	Spiregs *rp = (Spiregs *)soc.spi;

	rp->ictl |= Csnact;
	coherence();
	rp->icfg |= Dirrdcmd | 3<<8;	/* fast reads, 4-byte addresses */
	rp->icfg &= ~Bytelen;		/* one-byte reads */
	coherence();

//	print("spi flash at %#ux: memory reads enabled\n", PHYSSPIFLASH);
}

void	archconsole(void);

/* dummy for usb */
int
isaconfig(char *, int, ISAConf *)
{
	return 0;
}

/*
 * entered from l.s with mmu enabled.
 *
 * we may have to realign the data segment; apparently 5l -H0 -R4096
 * does not pad the text segment.  on the other hand, we may have been
 * loaded by another kernel.
 *
 * be careful not to touch the data segment until we know it's aligned.
 */
void
main(Mach* mach)
{
	extern char bdata[], edata[], end[], etext[];
	static ulong vfy = 0xcafebabe;

	m = mach;
	if (vfy != 0xcafebabe)
		memmove(bdata, etext, edata - bdata);
	if (vfy != 0xcafebabe) {
		wave('?');
		panic("misaligned data segment");
	}
	memset(edata, 0, end - edata);		/* zero bss */
	vfy = 0;

wave('9');
	machinit();
	archreset();
	mmuinit();

	quotefmtinstall();
	archconsole();
wave(' ');

	/* want plan9.ini to be able to affect memory sizing in confinit */
	plan9iniinit();		/* before we step on plan9.ini in low memory */

	confinit();
	/* xinit would print if it could */
	xinit();

	/*
	 * Printinit will cause the first malloc call.
	 * (printinit->qopen->malloc) unless any of the
	 * above (like clockintr) do an irqenable, which
	 * will call malloc.
	 * If the system dies here it's probably due
	 * to malloc(->xalloc) not being initialised
	 * correctly, or the data segment is misaligned
	 * (it's amazing how far you can get with
	 * things like that completely broken).
	 *
	 * (Should be) boilerplate from here on.
	 */
	trapinit();
	clockinit();

	printinit();
	uartkirkwoodconsole();
	/* only now can we print */
	print("from Bell Labs\n\n");

#ifdef CRYPTOSANDBOX
	print("sandbox: 64K at physical %#lux, mapped to 0xf10b0000\n",
		PADDR((uintptr)sandbox & ~(BY2PG-1)));
#endif

	archconfinit();
	cpuidprint();
	timersinit();

	procinit0();
	initseg();
	links();
	chandevreset();			/* most devices are discovered here */

	pageinit();
	userinit();
	schedinit();
	panic("schedinit returned");
}

void
cpuidprint(void)
{
	char name[64];

	cputype2name(name, sizeof name);
	print("cpu%d: %lldMHz ARM %s\n", m->machno, m->cpuhz/1000000, name);
}

void
machinit(void)
{
	memset(m, 0, sizeof(Mach));
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

	/* shutdown devices */
	chandevshutdown();

	/* call off the dog */
	clockshutdown();

	splhi();

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));
	cacheuwbinv();
	l2cacheuwb();

	/* off we go - never to return */
	cacheuwbinv();
	l2cacheuwb();
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

Confmem sheevamem[] = {
	/*
	 * Memory available to Plan 9:
	 * the 8K is reserved for ethernet dma access violations to scribble on.
	 */
	{ .base = 0, .limit = 512*MB - 8*1024, },
};

void
confinit(void)
{
	int i;
	ulong kpages;
	uintptr pa;

	/*
	 * Copy the physical memory configuration to Conf.mem.
	 */
	if(nelem(sheevamem) > nelem(conf.mem)){
		iprint("memory configuration botch\n");
		exit(1);
	}
	memmove(conf.mem, sheevamem, sizeof(sheevamem));

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

	conf.upages = (conf.npage*90)/100;
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
