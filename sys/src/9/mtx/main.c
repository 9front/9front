#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"pool.h"

Conf	conf;
FPsave	initfp;

void
main(void)
{
	memset(edata, 0, (ulong)end-(ulong)edata);
	conf.nmach = 1;
	machinit();
	ioinit();
	i8250console();
	quotefmtinstall();
	print("\nPlan 9\n");
	confinit();
	xinit();
	raveninit();
	trapinit();
	printinit();
	cpuidprint();
	mmuinit();
	hwintrinit();
	clockinit();
	procinit0();
	initseg();
	timersinit();
	links();
	chandevreset();
	pageinit();
	fpsave(&initfp);
	initfp.fpscr = 0;
	userinit();
	schedinit();
}

void
machinit(void)
{
	memset(m, 0, sizeof(Mach));
	m->cputype = getpvr()>>16;

	/*
	 * For polled uart output at boot, need
	 * a default delay constant. 100000 should
	 * be enough for a while. Cpuidentify will
	 * calculate the real value later.
	 */
	m->loopconst = 100000;

	/* turn on caches */
	puthid0(gethid0() | BIT(16) | BIT(17));

	active.machs[0] = 1;
	active.exiting = 0;
}

void
cpuidprint(void)
{
	char *id;

	id = "unknown PowerPC";
	switch(m->cputype) {
	case 9:
		id = "PowerPC 604e";
		break;
	}
	print("cpu0: %s\n", id);
}

static struct
{
	char	*name;
	char *val;
}
plan9ini[] =
{
	{ "console", "0" },
	{ "ether0", "type=2114x" },
};

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nelem(plan9ini); i++)
		if(cistrcmp(name, plan9ini[i].name) == 0)
			return plan9ini[i].val;
	return nil;
}

void
init0(void)
{
	char buf[2*KNAMELEN];

	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "power %s mtx", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "power", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	kproc("mmusweep", mmusweep, 0);
	touser((void*)(USTKTOP - sizeof(Tos)));
}

/* still to do */
void
reboot(void*, void*, ulong)
{
}

void
exit(int)
{
	cpushutdown();
	watchreset();
}

/*
 *  set up floating point for a new process
 */
void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
}

void
procfork(Proc *)
{
}

/*
 *  Save the mach dependent part of the process state.
 */
void
procsave(Proc *p)
{
	if(p->fpstate == FPactive){
		if(p->state != Moribund)
			fpsave(up->fpsave);
		p->fpstate = FPinactive;
	}
}

void
confinit(void)
{
	char *p;
	int userpcnt;
	ulong pa, kpages;
	extern ulong memsize;	/* passed in from ROM monitor */

	if(p = getconf("*kernelpercent"))
		userpcnt = 100 - strtol(p, 0, 0);
	else
		userpcnt = 0;

	pa = PGROUND(PADDR(end));

	conf.mem[0].npage = memsize/BY2PG;
	conf.mem[0].base = pa;
	conf.npage = conf.mem[0].npage;

	conf.nmach = 1;
	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nimage = 200;
	conf.nswap = conf.nproc*80;
	conf.nswppo = 4096;
	conf.copymode = 0;			/* copy on write */

	if(cpuserver) {
		if(userpcnt < 10)
			userpcnt = 70;
		kpages = conf.npage - (conf.npage*userpcnt)/100;

		/*
		 * Hack for the big boys. Only good while physmem < 4GB.
		 * Give the kernel a max. of 16MB + enough to allocate the
		 * page pool.
		 * This is an overestimate as conf.upages < conf.npages.
		 * The patch of nimage is a band-aid, scanning the whole
		 * page list in imagereclaim just takes too long.
		 */
		if(kpages > (16*MB + conf.npage*sizeof(Page))/BY2PG){
			kpages = (16*MB + conf.npage*sizeof(Page))/BY2PG;
			conf.nimage = 2000;
			kpages += (conf.nproc*KSTACK)/BY2PG;
		}
	} else {
		if(userpcnt < 10) {
			if(conf.npage*BY2PG < 16*MB)
				userpcnt = 40;
			else
				userpcnt = 60;
		}
		kpages = conf.npage - (conf.npage*userpcnt)/100;

		/*
		 * Make sure terminals with low memory get at least
		 * 4MB on the first Image chunk allocation.
		 */
		if(conf.npage*BY2PG < 16*MB)
			imagmem->minarena = 4*1024*1024;
	}
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
	if(!cpuserver){
		/*
		 * give terminals lots of image memory, too; the dynamic
		 * allocation will balance the load properly, hopefully.
		 * be careful with 32-bit overflow.
		 */
		imagmem->maxsize = kpages;
	}

//	conf.monitor = 1;	/* BUG */
}

static int
getcfields(char* lp, char** fields, int n, char* sep)
{
	int i;

	for(i = 0; lp && *lp && i < n; i++){
		while(*lp && strchr(sep, *lp) != 0)
			*lp++ = 0;
		if(*lp == 0)
			break;
		fields[i] = lp;
		while(*lp && strchr(sep, *lp) == 0){
			if(*lp == '\\' && *(lp+1) == '\n')
				*lp++ = ' ';
			lp++;
		}
	}

	return i;
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

void
setupwatchpts(Proc *, Watchpt *, int n)
{
	if(n > 0)
		error("no watchpoints");
}
