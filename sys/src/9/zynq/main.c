#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "init.h"
#include "pool.h"
#include "io.h"
#include "../port/error.h"
#include "tos.h"

Conf conf;
int normalprint, delaylink;
uchar *sp;

enum { MAXCONF = 64 };

char *confname[MAXCONF], *confval[MAXCONF];
int nconf;

void
exit(int)
{
	NOPE
}

void
reboot(void *, void *, ulong)
{
	NOPE
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
		fpsave(&up->fpsave);
		up->fpstate = FPinactive;
	case FPinactive:
		p->fpsave = up->fpsave;
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

int
cmpswap(long *a, long b, long c)
{
	extern int cas(int *, int, int);

	return cas((int *) a, b, c);
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

ulong *l2;

void
l2init(void)
{
	enum {
		CTRL = 0x100/4,
		AUX,
		TAGRAM,
		DATARAM,
		INVPA = 0x770/4,
		INVWAY = 0x77C/4,
		PREFETCH = 0xF60/4,
	};

	mpcore[0] |= 1;
	slcr[0xa1c/4] = 0x020202;
	l2 = vmap(L2_BASE, BY2PG);
	l2[CTRL] &= ~1;
	l2[TAGRAM] = l2[TAGRAM] & ~0x777 | 0x111;
	l2[DATARAM] = l2[DATARAM] & ~0x777 | 0x121;
	l2[PREFETCH] |= 3<<28 | 1<<24;
	l2[AUX] |= 3<<28 | 1<<20;
	l2[INVWAY] = 0xff;
	while((l2[INVPA] & 1) != 0)
		;
	l2[CTRL] = 1;
}

void
clean2pa(uintptr start, uintptr end)
{
	uintptr pa;
	
	start &= ~31;
	end = (end + 31) & ~31;
	for(pa = start; pa < end; pa += 32)
		l2[0x7b0/4] = pa;
}

void
inval2pa(uintptr start, uintptr end)
{
	uintptr pa;
	
	start &= ~31;
	end = (end + 31) & ~31;
	for(pa = start; pa < end; pa += 32)
		l2[0x770/4] = pa;
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
	conf.mem[0].limit = 1024*1024*1024;
	conf.npage = 0;
	for(i = 0; i < nelem(conf.mem); i++)
		conf.npage += conf.mem[i].npage = (conf.mem[i].limit - conf.mem[i].base) >> PGSHIFT;
	kmem = 100*1024*1024;
	conf.upages = conf.npage - kmem/BY2PG;
	kmem -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image);
	mainmem->maxsize = kmem;
	imagmem->maxsize = kmem - (kmem/10);
}

static uchar *
pusharg(char *p)
{
	int n;
	
	n = strlen(p) + 1;
	sp -= n;
	memmove(sp, p, n);
	return sp;
}

static void
bootargs(void *base)
{
	int i, ac;
	uchar *av[32];
	uchar **lsp;
	
	sp = (uchar *) base + BY2PG - sizeof(Tos);
	
	ac = 0;
	av[ac++] = pusharg("boot");
	sp = (uchar *) ((ulong) sp & ~3);
	sp -= (ac + 1) * sizeof(sp);
	lsp = (uchar **) sp;
	for(i = 0; i < ac; i++)
		lsp[i] = av[i] + ((USTKTOP - BY2PG) - (ulong) base);
	lsp[i] = 0;
	sp += (USTKTOP - BY2PG) - (ulong) base;
	sp -= BY2WD;
}

static void
init0(void)
{
	char buf[ERRMAX];
	int i;

	up->nerrlab = 0;
	spllo();
	
	up->slash = namec("#/", Atodir, 0, 0);
	pathclose(up->slash->path);
	up->slash->path = newpath("/");
	up->dot = cclone(up->slash);
	
	chandevinit();
	uartconsole();
	
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
	touser(sp);
}

void
userinit(void)
{
	Proc *p;
	Segment *s;
	void *v;
	Page *pg;
	
	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;
	
	kstrdup(&eve, "");
	kstrdup(&p->text, "*init*");
	kstrdup(&p->user, eve);
	
	procsetup(p);
	
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->kstack + KSTACK - (sizeof(Sargs) + BY2WD);
	
	s = newseg(SG_STACK, USTKTOP - USTKSIZE, USTKSIZE / BY2PG);
	p->seg[SSEG] = s;
	pg = newpage(0, 0, USTKTOP - BY2PG);
	v = tmpmap(pg->pa);
	memset(v, 0, BY2PG);
	segpage(s, pg);
	bootargs(v);
	tmpunmap(v);
	
	s = newseg(SG_TEXT, UTZERO, 1);
	s->flushme++;
	p->seg[TSEG] = s;
	pg = newpage(0, 0, UTZERO);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));

	segpage(s, pg);
	v = tmpmap(pg->pa);
	memset(v, 0, BY2PG);
	memmove(v, initcode, sizeof(initcode));
	tmpunmap(v);
	
	ready(p);
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
	return 1;
}

void
cpuidprint(void)
{
	print("\ncpu%d: %dMHz ARM Cortex-A9\n", m->machno, m->cpumhz);
}

void
mpinit(void)
{
	extern void mpbootstrap(void);	/* l.s */
	Mach *m1;
	ulong *v;
	int i;

	if(getconf("*nomp"))
		return;

	conf.nmach = 2;
	conf.copymode = 1;

	m1 = MACHP(1);
	memset(m1, 0, MACHSIZE);
	m1->machno = 1;
	m1->l1.pa = MACHL1(m1->machno)-KZERO;
	m1->l1.va = KADDR(m1->l1.pa);

	memset(m1->l1.va, 0, L1SZ);
	for(i=0; i<L1X(VMAPSZ); i++)
		m1->l1.va[L1X(VMAP)+i] = m->l1.va[L1X(VMAP)+i];
	for(i=0; i<L1X(-KZERO); i++)
		m1->l1.va[L1X(KZERO)+i] = m->l1.va[L1X(KZERO)+i];
	coherence();
	cleandse((uchar*)KZERO, (uchar*)0xFFFFFFFF);

	v = tmpmap(0xFFFFF000);
	v[0xFF0/4] = PADDR(mpbootstrap);
	coherence();
	cleandse(v, (uchar*)v+BY2PG);
	tmpunmap(v);

	sendevent();
	synccycles();
}

void
main(void)
{
	active.machs |= (1 << m->machno);
	if(m->machno != 0){
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
	l2init();
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
	swapinit();
	screeninit();
	userinit();
	mpinit();
	schedinit();
}
