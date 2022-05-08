#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "pool.h"
#include "io.h"
#include "sysreg.h"

Conf conf;

/*
 *  starting place for first process
 */
void
init0(void)
{
	char **sp;

	chandevinit();

	if(!waserror()){
		ksetenv("cputype", "arm64", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		ksetenv("console", "0", 0);
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	sp = (char**)(USTKTOP-sizeof(Tos) - 8 - sizeof(sp[0])*4);
	sp[3] = sp[2] = sp[1] = nil;
	strcpy(sp[1] = (char*)&sp[4], "boot");
	sp[0] = (void*)&sp[1];
	touser((uintptr)sp);
}

void
confinit(void)
{
	int userpcnt;
	ulong kpages;
	char *p;
	int i;

	conf.nmach = 1;

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

	if(userpcnt < 10)
		userpcnt = 60 + cpuserver*10;

	conf.npage = 0;
	for(i = 0; i < nelem(conf.mem); i++)
		conf.npage += conf.mem[i].npage;

	kpages = conf.npage - (conf.npage*userpcnt)/100;
	if(kpages > ((uintptr)-VDRAM)/BY2PG)
		kpages = ((uintptr)-VDRAM)/BY2PG;

	conf.upages = conf.npage - kpages;
	conf.ialloc = (kpages/2)*BY2PG;

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
main(void)
{
	machinit();
	if(m->machno){
		trapinit();
		fpuinit();
		intrinit();
		clockinit();
		// cpuidprint();
		synccycles();
		timersinit();
		flushtlb();
		mmu1init();
		m->ticks = MACHP(0)->ticks;
		schedinit();
		return;
	}
	quotefmtinstall();
	meminit();
	confinit();
	xinit();
	uartconsinit();
	printinit();
	print("\nPlan 9\n");
	trapinit();
	fpuinit();
	intrinit();
	clockinit();
	timersinit();
	pageinit();
	procinit0();
	initseg();
	links();
	chandevreset();
	userinit();
//	mpinit();
	mmu0clear((uintptr*)L1);
	flushtlb();
	mmu1init();
	schedinit();
}

void
exit(int)
{
	cpushutdown();
	splfhi();
	for(;;);
}

int
isaconfig(char *, int, ISAConf *)
{
	return 0;
}

char*
getconf(char *)
{
	return nil;
}

void
writeconf(void)
{
}

void
reboot(void *, void *, ulong)
{
}

void
dmaflush(int clean, void *p, ulong len)
{
	uintptr s = (uintptr)p;
	uintptr e = (uintptr)p + len;

	if(clean){
		s &= ~(BLOCKALIGN-1);
		e += BLOCKALIGN-1;
		e &= ~(BLOCKALIGN-1);
		cachedwbse((void*)s, e - s);
		return;
	}
	if(s & BLOCKALIGN-1){
		s &= ~(BLOCKALIGN-1);
		cachedwbinvse((void*)s, BLOCKALIGN);
		s += BLOCKALIGN;
	}
	if(e & BLOCKALIGN-1){
		e &= ~(BLOCKALIGN-1);
		if(e < s)
			return;
		cachedwbinvse((void*)e, BLOCKALIGN);
	}
	if(s < e)
		cachedinvse((void*)s, e - s);
}
