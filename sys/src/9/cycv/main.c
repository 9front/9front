#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "pool.h"
#include "io.h"
#include "../port/error.h"

Conf conf;
int normalprint, delaylink;

enum { MAXCONF = 64 };

char *confname[MAXCONF], *confval[MAXCONF];
int nconf;

void
exit(int)
{
	cpushutdown();
	for(;;) idlehands();
}

void
reboot(void *, void *, ulong)
{
}

void
evenaddr(uintptr va)
{
	if((va & 3) != 0){
		dumpstack();
		postnote(up, 1, "sys: odd address", NDebug);
		error(Ebadarg);
	}
}

void
procfork(Proc *p)
{
	ulong s;

	p->kentry = up->kentry;
	p->pcycles = -p->kentry;
	
	s = splhi();
	switch(up->fpstate & ~FPillegal){
	case FPactive:
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	case FPinactive:
		memmove(p->fpsave, up->fpsave, sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	splx(s);
}

void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
	fpoff();
	
	cycles(&p->kentry);
	p->pcycles = -p->kentry;
}

void
kexit(Ureg *)
{
	Tos *tos;
	uvlong t;

	tos = (Tos*)(USTKTOP-sizeof(Tos));
	cycles(&t);
	tos->kcycles += t - up->kentry;
	tos->pcycles = t + up->pcycles;
	tos->pid = up->pid;
}

static void
options(void)
{
	long i, n;
	char *cp, *line[MAXCONF], *p, *q;

	cp = (char *) CONFADDR;

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
confinit(void)
{
	ulong kmem;
	int i;

	conf.nmach = 1;
	conf.nproc = 2000;
	conf.ialloc = 16*1024*1024;
	conf.nimage = 200;
	conf.mem[0].base = PGROUND((ulong)end - KZERO);
	conf.mem[0].npage = (1024*1024*1024 - conf.mem[0].base) >> PGSHIFT;

	ramdiskinit();

	conf.npage = 0;
	for(i = 0; i < nelem(conf.mem); i++)
		conf.npage += conf.mem[i].npage;

	kmem = 200*1024*1024;
	conf.upages = conf.npage - kmem/BY2PG;
	kmem -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image);
	mainmem->maxsize = kmem;
	imagmem->maxsize = kmem - (kmem/10);
}

void
init0(void)
{
	char buf[ERRMAX], **sp;
	int i;

	chandevinit();

	if(!waserror()){
		ksetenv("cputype", "arm", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		ksetenv("console", "0", 0);
		snprint(buf, sizeof(buf), "zynq %s", conffile);
		ksetenv("terminal", buf, 0);
		for(i = 0; i < nconf; i++){
			if(*confname[i] != '*')
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

void
sanity(void)
{
	static int dat = 0xdeadbeef;
	extern ulong vectors[];

	assert(dat == 0xdeadbeef);
	assert(((uintptr)vectors & 31) == 0);
	assert(sizeof(Mach) + KSTACK <= MACHSIZE);
	assert((KZERO & SECSZ - 1) == 0);
}

char *
getconf(char *n)
{
	int i;
	
	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], n) == 0)
			return confval[i];
	return nil;
}

int
isaconfig(char *, int, ISAConf*)
{
	return 0;
}

void
cpuidprint(void)
{
	print("cpu%d: %dMHz ARM Cortex-A9\n", m->machno, m->cpumhz);
}

void
main(void)
{
	active.machs[m->machno] = 1;
	if(m->machno != 0){
		uartputs("\n", 1);
		mmuinit();
		intrinit();
		timerinit();
		cpuidprint();
		synccycles();
		timersinit();
		schedinit();
		return;
	}
	uartinit();
	mmuinit();
	intrinit();
	options();
	confinit();
	timerinit();
	uartputs(" from Bell Labs\n", 16);
	xinit();
	printinit();
	quotefmtinstall();
	cpuidprint();
	sanity();
	todinit();
	timersinit();
	procinit0();
	initseg();
	if(delaylink)
		bootlinks();
	else
		links();
	archinit();
	chandevreset();
	pageinit();
	userinit();
	schedinit();
}

void
setupwatchpts(Proc *, Watchpt *, int n)
{
	if(n > 0)
		error("no watchpoints");
}
