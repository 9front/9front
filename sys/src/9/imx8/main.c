#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "pool.h"
#include "io.h"
#include "../arm64/sysreg.h"
#include "ureg.h"

#include "rebootcode.i"

Conf conf;

#define	MAXCONF 64
static char *confname[MAXCONF];
static char *confval[MAXCONF];
static int nconf = -1;

void
bootargsinit(void)
{
	int i, j, n;
	char *cp, *line[MAXCONF], *p, *q;

	/*
	 *  parse configuration args from dos file plan9.ini
	 */
	cp = BOOTARGS;
	cp[BOOTARGSLEN-1] = 0;

	/*
	 * Strip out '\r', change '\t' -> ' '.
	 */
	p = cp;
	for(q = cp; *q; q++){
		if(*q == -1)
			break;
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}
	*p = 0;

	n = getfields(cp, line, MAXCONF, 1, "\n");
	if(n <= 0){
		/* empty plan9.ini, no configuration passed */
		return;
	}

	nconf = 0;
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = strchr(line[i], '=');
		if(cp == nil)
			continue;
		*cp++ = '\0';
		for(j = 0; j < nconf; j++){
			if(cistrcmp(confname[j], line[i]) == 0)
				break;
		}
		confname[j] = line[i];
		confval[j] = cp;
		if(j == nconf)
			nconf++;
	}
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return confval[i];
	return nil;
}

void
setconfenv(void)
{
	int i;

	if(nconf < 0){
		/* use defaults when there was no configuration */
		ksetenv("console", "0", 1);
		return;
	}

	for(i = 0; i < nconf; i++){
		if(confname[i][0] != '*')
			ksetenv(confname[i], confval[i], 0);
		ksetenv(confname[i], confval[i], 1);
	}
}

void
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
	memset(BOOTARGS+n, 0, BOOTARGSLEN-n);
	poperror();
	free(p);
}

int
isaconfig(char *, int, ISAConf *)
{
	return 0;
}

/*
 *  starting place for first process
 */
void
init0(void)
{
	char buf[2*KNAMELEN], **sp;

	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", "ARM64", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "arm64", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		setconfenv();
		poperror();
	}
	kproc("alarm", alarmkproc, 0);

	sp = (char**)(USTKTOP-sizeof(Tos) - 8 - sizeof(sp[0])*4);
	sp[3] = sp[2] = sp[1] = nil;
	strcpy(sp[1] = (char*)&sp[4], "boot");
	sp[0] = (void*)&sp[1];

	splhi();
	fpukexit(nil, nil);
	touser((uintptr)sp);
}

void
confinit(void)
{
	int userpcnt;
	ulong kpages;
	char *p;
	int i;

	conf.nmach = MAXMACH;
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
	if(conf.nproc > 4000)
		conf.nproc = 4000;
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
		+ conf.nproc*sizeof(Proc*)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap
		+ conf.nswppo*sizeof(Page*);
	mainmem->maxsize = kpages;
	imagmem->maxsize = kpages;
}

void
machinit(void)
{
	m->ticks = 1;
	m->perf.period = 1;
	active.machs[m->machno] = 1;
}

static uvlong
machmpid(int machno)
{
	uvlong mpid = 0;
	int i;

	for(i = 0; i < 64; i++){
		if(MPIDMASK & (1ULL<<i)){
			mpid |= (machno & 1ULL) << i;
			machno >>= 1;
		}
	}
	return mpid;
}

void
mpinit(void)
{
	extern int mpidindex(uvlong);
	extern void _start(void);
	int i;

	for(i = 1; i < conf.nmach; i++){
		Ureg u = {0};

		assert(mpidindex(machmpid(i)) == i);

		MACHP(i)->machno = i;
		cachedwbinvse(MACHP(i), MACHSIZE);

		u.r0 = 0x84000003;	/* CPU_ON */
		u.r1 = (sysrd(MPIDR_EL1) & ~MPIDMASK) | machmpid(i);
		u.r2 = PADDR(_start);
		u.r3 = i;
		smccall(&u);
	}
	synccycles();
}

void
cpuidprint(void)
{
	iprint("cpu%d: %dMHz ARM Cortex A53\n", m->machno, m->cpumhz);
}

static void
tmuinit(void)
{
	Physseg seg;

	setclkgate("tmu.clk", 1);
	memset(&seg, 0, sizeof(seg));
	seg.attr = SG_PHYSICAL | SG_DEVICE | SG_NOEXEC;
	seg.name = "tmu";
	seg.pa = VIRTIO + 0x260000 - KZERO;
	seg.size = BY2PG;
	addphysseg(&seg);
}

static void
lpcspiinit(void)
{
	Physseg seg;

	iomuxpad("pad_ecspi2_sclk", "ecspi2_sclk", "~LVTTL ~HYS ~PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_ecspi2_mosi", "ecspi2_mosi", "~LVTTL ~HYS ~PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_ecspi2_miso", "ecspi2_miso", "~LVTTL ~HYS ~PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_ecspi2_ss0", "ecspi2_ss0", "~LVTTL ~HYS ~PUE ~ODE FAST 45_OHM");

	setclkgate("ecspi2.ipg_clk", 0);
	setclkgate("ecspi2.ipg_clk_per", 0);
	setclkrate("ecspi2.ipg_clk_per", "osc_25m_ref_clk", 25*Mhz);
	setclkgate("ecspi2.ipg_clk_per", 1);
	setclkgate("ecspi2.ipg_clk", 1);

	memset(&seg, 0, sizeof(seg));
	seg.attr = SG_PHYSICAL | SG_DEVICE | SG_NOEXEC;
	seg.name = "ecspi2";
	seg.pa = VIRTIO + 0x830000 - KZERO;
	seg.size = BY2PG;
	addphysseg(&seg);
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
		cpuidprint();
		synccycles();
		timersinit();
		mmu1init();
		m->ticks = MACHP(0)->ticks;
		schedinit();
		return;
	}
	uartconsinit();
	quotefmtinstall();
	bootargsinit();
	meminit();
	confinit();
	xinit();
	printinit();
	print("\nPlan 9\n");
	trapinit();
	fpuinit();
	intrinit();
	clockinit();
	cpuidprint();
	timersinit();
	pageinit();
	procinit0();
	initseg();
	links();
	lcdinit();
	tmuinit();
	lpcspiinit();
	chandevreset();
	userinit();
	mpinit();
	mmu1init();
	schedinit();
}

void
exit(int)
{
	Ureg u = { .r0 = 0x84000002 };	/* CPU_OFF */

	cpushutdown();
	splfhi();

	if(m->machno == 0){
		/* clear secrets */
		zeroprivatepages();
		poolreset(secrmem);

		u.r0 = 0x84000009;	/* SYSTEM RESET */
	}
	smccall(&u);
}

static void
rebootjump(void *entry, void *code, ulong size)
{
	void (*f)(void*, void*, ulong);

	intrcpushutdown();

	/* redo identity map */
	setttbr(PADDR(L1BOT));

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));

	cachedwbinvse(f, sizeof(rebootcode));
	cacheiinvse(f, sizeof(rebootcode));

	(*f)(entry, code, size);

	for(;;);
}

void
reboot(void*, void *code, ulong size)
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

	/* stop the clock */
	clockshutdown();
	intrsoff();

	/* clear secrets */
	zeroprivatepages();
	poolreset(secrmem);

	/* off we go - never to return */
	rebootjump((void*)(KTZERO-KZERO), code, size);
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
