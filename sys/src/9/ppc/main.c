#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"pool.h"

#define	MAXCONF		64

typedef struct Plan9ini Plan9ini;
struct Plan9ini
{
	char	*name;
	char	*val;
};

char *plan9inistr;
Plan9ini plan9ini[MAXCONF];
int nconf;

Conf conf;
FPsave initfp;
Lock testlock;

static void plan9iniinit(void);

char *
cpuid(void)
{
	char *id;

	id = "unknown PowerPC";
	switch(m->cputype) {
	case 8:
		id = "PowerPC 750";
		break;
	case 9:
		id = "PowerPC 604e";
		break;
	case 0x81:
		id = "PowerPC 8260";
		break;
	case 0x8081:
		id = "PowerPC 826xA";
		break;
	default:
		break;
	}
	return id;
}

void
cpuidprint(void)
{
	print("cpu0: %s, rev 0x%lux, cpu hz %lld, bus hz %ld\n", 
		cpuid(), getpvr()&0xffff, m->cpuhz, m->bushz);
}

void
main(void)
{
	memset(edata, 0, (ulong)end-(ulong)edata);
	conf.nmach = 1;
	machinit();
	confinit();
	xinit();
	trapinit();
	mmuinit();
	plan9iniinit();
	hwintrinit();
	clockinit();
	timerinit();
	console();
	quotefmtinstall();
	printinit();
	cpuidprint();
	print("\nPlan 9 from Bell Labs\n");
	procinit0();
	initseg();
	timersinit();
	links();
	chandevreset();
	pageinit();
	sharedseginit();
	fpsave(&initfp);
	initfp.fpscr = 0;
	userinit();
	schedinit();
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(name, plan9ini[i].name) == 0)
			return plan9ini[i].val;
	return nil;
}

static void
plan9iniinit(void)
{
	long i;
	int c;
	char *cp, line[MAXCONF], *p, *q;

	/*
	 *  parse configuration args from dos file plan9.ini
	 */

	cp = plan9inistr;
	for(i = 0; i < MAXCONF; i++){
		/*
		 * Strip out '\r', change '\t' -> ' ', test for 0xff which is end of file
		 */
		p = line;
		for(q = cp; c = (uchar)*q; q++){
			if(c == '\r')
				continue;
			if(c == '\t')
				c = ' ';
			if(c == 0xff || c == '\n')
				break;
			*p++ = c;
		}
		*p = 0;
		if (*line == 0)
			break;
		if(*line != '#' && (cp = strchr(line, '='))){
			*cp++ = '\0';
			kstrdup(&plan9ini[nconf].name, line);
			kstrdup(&plan9ini[nconf].val, cp);
			nconf++;
		}
		if (c == 0xff)
			break;

		cp = q + 1;
	}
}

void
init0(void)
{
	char buf[2*KNAMELEN];
	int i;

	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "power %s mtx", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "power", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);

		for(i = 0; i < nconf; i++){
			if(plan9ini[i].name[0] != '*')
				ksetenv(plan9ini[i].name, plan9ini[i].val, 0);
			ksetenv(plan9ini[i].name, plan9ini[i].val, 1);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	kproc("mmusweep", mmusweep, 0);
	touser((void*)(USTKTOP - sizeof(Tos)));
}

void
exit(int)
{
	cpushutdown();
	for(;;) idlehands();
}

/*
 *  set up floating point for a new process
 */
void
procsetup(Proc *p)
{
	p->fpstate = FPinit;

	cycles(&p->kentry);
	p->pcycles = -p->kentry;
}

void
procfork(Proc *p)
{
	p->kentry = up->kentry;
	p->pcycles = -p->kentry;
}

void
procrestore(Proc *p)
{
	uvlong t;

	if(p->kp)
		return;
	cycles(&t);
	p->pcycles -= t;
	p->kentry += t;
}

/*
 *  Save the mach dependent part of the process state.
 */
void
procsave(Proc *p)
{
	uvlong t;

	cycles(&t);
	p->pcycles += t;
	p->kentry -= t;
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
	/* passed in from ROM monitor: */

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

	pa = PGROUND(PADDR(end));

	/* Blast Board specific */
	conf.mem[0].npage = (MEM1SIZE - pa)/BY2PG;
	conf.mem[0].base = pa;
	
	conf.mem[1].npage = MEM2SIZE/BY2PG;
	conf.mem[1].base = MEM2BASE;

	conf.npage = conf.mem[0].npage + conf.mem[1].npage;

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
	int i;
	char cc[KNAMELEN], *p;

	sprint(cc, "%s%d", class, ctlrno);

	p = getconf(cc);
	if(p == 0)
		return 0;
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
