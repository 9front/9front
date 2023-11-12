#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	<pool.h>
#include	"../ip/ip.h"
#include	<../port/error.h>


FPsave initfp;

/*
 * software tlb simulation
 */
static Softtlb stlb[MAXMACH][STLBSIZE];

Conf	conf;
Mach* machaddr[MAXMACH];

static void
checkclock0(void)
{
	print("count=%luX compare=%luX %d\n", rdcount(), rdcompare(), m->speed);
	delay(20);
}


static void
checkconf0(void)
{
	iprint("frc0 check = %uX \n", getfcr0);
// for debug stuff
}

static void
prcpuid(void)
{
	ulong cpuid, cfg1;
	char *cpu;

	cpuid = prid();
	if (((cpuid>>16) & MASK(8)) == 0)		/* vendor */
		cpu = "old mips";
	else if (((cpuid>>16) & MASK(8)) == 1)
		switch ((cpuid>>8) & MASK(8)) {		/* processor */
		case 0x93:
			cpu = "mips 24k";
			break;
		case 0x96:
			cpu = "mips 24KEc";
			break;
		default:
			cpu = "mips";
			break;
		}
	else
		cpu = "other mips";
	delay(20);
	print("cpu%d: %ldMHz %s %s v%ld %ld rev %ld, ",
		m->machno, m->hz / Mhz, cpu, getconfig() & (1<<15)? "b": "l",
		(cpuid>>5) & MASK(3), (cpuid>>2) & MASK(3), cpuid & MASK(2));
	delay(200);
	cfg1 = getconfig1();
	print("%s fpu\n", (cfg1 & 1? "has": "no"));
	print("cpu%d: %ld tlb entries, using %dK pages\n", m->machno,
		((cfg1>>25) & MASK(6)) + 1, BY2PG/1024);
	delay(50);
	print("cpu%d: l1 i cache: %d sets 4 ways 32 bytes/line\n", m->machno,
		64 << ((cfg1>>22) & MASK(3)));
	delay(50);
	print("cpu%d: l1 d cache: %d sets 4 ways 32 bytes/line\n", m->machno,
		64 << ((cfg1>>13) & MASK(3)));
	delay(500);
/* i changed this if from 0 to 1 */
	if (1) 
		print("cpu%d: cycle counter res = %ld\n",
			m->machno, gethwreg3());
}


static void
fmtinit(void)
{
	printinit();

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
main(void)
{
	savefpregs(&initfp);

	uartconsinit();
	quotefmtinstall();

	confinit();
	machinit();			/* calls clockinit */
	active.exiting = 0;
	active.machs[0] = 1;

	kmapinit();
	xinit();
	timersinit();
	plan9iniinit();
	intrinit();

	iprint("\nPlan 9 \n");
	prcpuid();
	delay(50);
	checkclock0();
	print("(m)status %lub\n", getstatus());

	ckpagemask(PGSZ, BY2PG);
	if (PTECACHED == PTENONCOHERWT)
		print("caches configured as write-through\n");
	tlbinit();
	pageinit();
	delay(50);

	printinit();	/* what does this do? */
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

	MACHP(0) = (Mach*)MACHADDR;

	memset(m, 0, sizeof(Mach));
	m->machno = 0;
	machaddr[m->machno] = m;

	/*
	 *  set up CPU's mach structure
	 *  cpu0's was zeroed in l.s and our stack is in Mach, so don't zero it.
	 */
	m->speed = 580;			/* initial guess at MHz */
	m->hz = m->speed * Mhz;
	conf.nmach = 1;


	m->stb = stlb[m->machno];
	m->ticks = 1;
	m->perf.period = 1;


	/* install exception handlers */
	sbp[0x18/4] = utlbmiss;
	sbp[0x14/4] = gevector;

	/* we could install our own vectors directly, but we'll try to play nice */
	if(1){
		memmove((void*)(KSEG0+0x0), (void*)vector0, 0x80);
		memmove((void*)(KSEG0+0x180), (void*)vector180, 0x80);
		icflush((void*)(KSEG0+0x0), 0x80);
		icflush((void*)(KSEG0+0x180), 0x80);
	}

	setstatus(getstatus() & ~BEV);

	up = nil;

	/* Ensure CU1 is off */
	clrfpintr();
	clockinit();
}

void
init0(void)
{
	char buf[128], **sp;

	chandevinit();

	if(!waserror()){
		ksetenv("cputype", "spim", 0);
		snprint(buf, sizeof buf, "mips %s", conffile);
		ksetenv("terminal", buf, 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);

		setconfenv();

		poperror();
	}

	checkconf0();

	kproc("alarm", alarmkproc, 0);

	sp = (char**)(USTKTOP-sizeof(Tos) - 8 - sizeof(sp[0])*4);
	sp[3] = sp[2] = sp[1] = nil;
	strcpy(sp[0] = (char*)&sp[4], "boot");

	touser(sp);
}

void
exit(int)
{
	cpushutdown();
	splhi();
	if(m->machno == 0){
		/* clear secrets */
		zeroprivatepages();
		poolreset(secrmem);
	}
	for(;;);
}

void
reboot(void *, void *, ulong)
{
}


void
confinit(void)
{
	ulong kpages, ktop;

	/*
	 *  divide memory twixt user pages and kernel.
	 */
	conf.mem[0].base = ktop = PADDR(PGROUND((ulong)end));
	/* fixed memory on routerboard */
	conf.mem[0].npage = MEMSIZE/BY2PG - ktop/BY2PG;
	conf.npage = conf.mem[0].npage;

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
		+ conf.nswppo*sizeof(Page);
	mainmem->maxsize = kpages;
//	mainmem->flags |= POOL_PARANOIA;


	/* set up other configuration parameters */
	conf.nproc = 2000;
	conf.nswap = 262144;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = 0;		/* copy on write */


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


