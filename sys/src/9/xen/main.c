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

#define BOOTARGS	(xenstart->cmd_line)
#define	BOOTARGSLEN	(sizeof xenstart->cmd_line)
#define	MAXCONF		64

Conf conf;
char *confname[MAXCONF];
char *confval[MAXCONF];
int nconf;
int idle_spin;

static void
options(void)
{
	long i, n;
	char *cp, *line[MAXCONF], *p, *q;

	/*
	 *  parse configuration args from dos file plan9.ini
	 */
	cp = BOOTARGS;	/* where b.com leaves its config */
	cp[BOOTARGSLEN-1] = 0;

	/*
	 * Strip out '\r', change '\t' -> ' '.
	 */
	p = cp;
	for(q = cp; *q; q++){
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}
	*p = 0;

	n = getfields(cp, line, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = strchr(line[i], '=');
		if(cp == nil)
			continue;
		*cp++ = '\0';
		confname[nconf] = line[i];
		confval[nconf] = cp;
		nconf++;
	}
}

void
main(void)
{
	mach0init();
	options();
	ioinit();
	xenconsinit();
	quotefmtinstall();

	//consdebug = rdb;
	print("\nPlan 9 (%s)\n", xenstart->magic);

	cpuidentify();
	// meminit() is not for us
	confinit();
	archinit();
	if(arch->clockinit)
		arch->clockinit();
	xinit();
	trapinit();
	printinit();
	cpuidprint();
	mmuinit();
	if(arch->intrinit)	/* launches other processors on an mp */
		arch->intrinit();
	timersinit();
	mathinit();
	kbdenable();
	xengrantinit();
	if(arch->clockenable)
		arch->clockenable();
	procinit0();
	initseg();

	links();
//	conf.monitor = 1;
	chandevreset();
	pageinit();
	userinit();
	schedinit();
}

void
mach0init(void)
{
	m = (Mach*)MACHADDR;
	m->machno = 0;
	conf.nmach = 1;
	MACHP(0) = (Mach*)CPU0MACH;
	m->pdb = (ulong*)xenstart->pt_base;

	machinit();

	active.machs[0] = 1;
	active.exiting = 0;
}

void
machinit(void)
{
	int machno;
	ulong *pdb;

	machno = m->machno;
	pdb = m->pdb;
	memset(m, 0, sizeof(Mach));
	m->machno = machno;
	m->pdb = pdb;
	m->perf.period = 1;

	/*
	 * For polled uart output at boot, need
	 * a default delay constant. 100000 should
	 * be enough for a while. Cpuidentify will
	 * calculate the real value later.
	 */
	m->loopconst = 100000;
	m->cpumhz = 1000;				// XXX! 

	HYPERVISOR_shared_info = (shared_info_t*)mmumapframe(XENSHARED, (xenstart->shared_info)>>PGSHIFT);
	
	// XXX m->shared = &HYPERVISOR_shared_info->vcpu_data[m->machno];
}

void
init0(void)
{
	char buf[2*KNAMELEN], **sp;
	int i;

	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", arch->id, conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "386", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		ksetenv("readparts", "1", 0);
		for(i = 0; i < nconf; i++){
			if(confname[i][0] != '*')
				ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);

	sp = (char**)(USTKTOP - sizeof(Tos) - 8 - sizeof(sp[0])*4);
	sp[3] = sp[2] = nil;
	strcpy(sp[1] = (char*)&sp[4], "boot");
	sp[0] = nil;
	touser(sp);
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return confval[i];
	return 0;
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

void
confinit(void)
{
	char *p;
	int i, userpcnt;
	ulong kpages;

	for(i = 0; i < nconf; i++)
		print("%s=%s\n", confname[i], confval[i]);
	/* 
	 * all ram above xentop is free, but must be mappable
	 * to virt addrs less than VIRT_START.
	 */
	kpages = PADDR(hypervisor_virt_start)>>PGSHIFT;
	if(xenstart->nr_pages <= kpages)
		kpages = xenstart->nr_pages;
	else
		print("Warning: Plan 9 / Xen limitation - "
			  "using only %lud of %lud available RAM pages\n",
			  kpages, xenstart->nr_pages);
	xentop = PGROUND(PADDR(xentop));
	conf.mem[0].npage = kpages - (xentop>>PGSHIFT);
	conf.mem[0].base = xentop;

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
	if(conf.nproc > 4000)
		conf.nproc = 4000;
	conf.nimage = 200;
	conf.nswap = conf.nproc*80;
	conf.nswppo = 4096;

	if(cpuserver) {
		if(userpcnt < 10)
			userpcnt = 70;
		kpages = conf.npage - (conf.npage*userpcnt)/100;

		/*
		 * Hack for the big boys. Only good while physmem < 4GB.
		 * Give the kernel fixed max + enough to allocate the
		 * page pool.
		 * This is an overestimate as conf.upages < conf.npages.
		 * The patch of nimage is a band-aid, scanning the whole
		 * page list in imagereclaim just takes too long.
		 */
		if(kpages > (64*MB + conf.npage*sizeof(Page))/BY2PG){
			kpages = (64*MB + conf.npage*sizeof(Page))/BY2PG;
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
		+ conf.nproc*sizeof(Proc*)
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
}

void
procsetup(Proc *p)
{
	fpuprocsetup(p);
}

void
procfork(Proc *p)
{
	fpuprocfork(p);
}

void
procrestore(Proc *p)
{
	fpuprocrestore(p);
}

/*
 *  Save the mach dependent part of the process state.
 */
void
procsave(Proc *p)
{
	fpuprocsave(p);

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
	mmuflushtlb(0);
}

void
reboot(void *entry, void *code, ulong size)
{
	void (*f)(ulong, ulong, ulong);

	writeconf();
	cpushutdown();

	splhi();

	/* turn off buffered serial console */
	serialoq = nil;

	/* shutdown devices */
	chandevshutdown();

	/* clear secrets */
	zeroprivatepages();
	poolreset(secrmem);

	/* reboot(0, ...) on Xen causes domU shutdown */
	if(entry == 0)
		HYPERVISOR_shutdown(0);

	mmuflushtlb(0);

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));

	/* off we go - never to return */
	(*f)(PADDR(entry), PADDR(code), size);
}

void
exit(int)
{
	cpushutdown();
	splhi();

	/* clear secrets */
	zeroprivatepages();
	poolreset(secrmem);

	arch->reset();
}
