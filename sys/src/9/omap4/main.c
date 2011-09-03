#include "u.h"
#include "ureg.h"
#include "pool.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "init.h"
#include "tos.h"
#include "arm.h"

ulong *uart = (ulong *) 0x48020000;
#define wave(x) (*uart = (char) (x))
uintptr kseg0 = KZERO;
uchar *sp;

Mach *m;
Mach *machaddr[MAXMACH];
Conf conf;

void
machinit(void)
{
	machaddr[0] = m = KADDR(FIRSTMACH);
	memset(m, 0, sizeof(Mach));
	active.machs = conf.nmach = 1;
	active.exiting = 0;
	up = nil;

	conf.mem[0].base = ROUNDUP(PADDR(end), BY2PG);
	conf.mem[0].limit = ROUNDDN(PHYSDRAM + DRAMSIZ, BY2PG);
	conf.mem[0].npage = (conf.mem[0].limit - conf.mem[0].base) / BY2PG;
	conf.npage = conf.mem[0].npage;
	conf.upages = conf.npage - 64 * MiB / BY2PG;
	conf.nproc = 100;
	conf.pipeqsize = 32768;
	conf.nimage = 200;
	conf.ialloc = 65536;
}

void
init0(void)
{
	Ureg ureg;

	spllo();
	up->nerrlab = 0;
	up->slash = namec("#/", Atodir, 0, 0);
	pathclose(up->slash->path);
	up->slash->path = newpath("/");
	up->dot = cclone(up->slash);
	chandevinit();
	if(!waserror()){
		ksetenv("terminal", "generic /sys/src/9/omap4/panda", 0);
		ksetenv("cputype", "arm", 0);
		ksetenv("service", "cpu", 0);
		ksetenv("console", "0", 0);
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	memset(&ureg, 0, sizeof ureg);
	ureg.pc = UTZERO + 32;
	ureg.r13 = (ulong) sp;
	ureg.psr = PsrMusr;
	touser(&ureg);
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
	int ac, i;
	uchar *av[32], **lsp;
	sp = (uchar*)base + BY2PG - sizeof(Tos);

	ac = 0;
	av[ac++] = pusharg("boot");
	sp = (uchar *) ROUNDDN((ulong)sp, 4);
	sp -= (ac + 1) * 4;
	lsp = (uchar **) sp;
	for(i = 0; i < ac; i++)
		lsp[i] = av[i] + ((USTKTOP - BY2PG) - (ulong) base);
	lsp[i] = 0;
	sp += (USTKTOP - BY2PG) - (ulong)base;
	sp -= BY2WD;
}

void
userinit(void)
{
	Proc *p;
	Segment *s;
	Page *pg;
	void *v;
	
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
	p->sched.sp = (ulong)p->kstack + KSTACK - sizeof(Sargs) - BY2WD;
	
	s = newseg(SG_STACK, USTKTOP - USTKSIZE, USTKSIZE / BY2PG);
	p->seg[SSEG] = s;
	pg = newpage(0, 0, USTKTOP - BY2PG);
	v = vmap(pg->pa, BY2PG);
	memset(v, 0, BY2PG);
	segpage(s, pg);
	bootargs(v);
	vunmap(v, BY2PG);
	
	s = newseg(SG_TEXT, UTZERO, 1);
	s->flushme++;
	p->seg[TSEG] = s;
	pg = newpage(0, 0, UTZERO);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	segpage(s, pg);
	v = vmap(pg->pa, BY2PG);
	memset(v, 0, BY2PG);
	memmove(v, initcode, sizeof initcode);
	vunmap(v, BY2PG);
	
	ready(p);
}

void
main()
{
	wave('f');
	memset(edata, 0, end - edata);
	wave('r');
	machinit();
	wave('o');
	mmuinit();
	wave('m');
	trapinit();
	uartinit();
	print(" Bell Labs\n");
	xinit();
	globalclockinit();
	localclockinit();
	timersinit();
	procinit0();
	pageinit();
	swapinit();
	initseg();
	quotefmtinstall();
	chandevreset();
	links();
	userinit();
	schedinit();
}

void
exit(int)
{
	uartputs("resting\n", 9);
	splhi();
	while(1)
		idlehands();
}

void
reboot()
{
	exit(0);
}

void
rdb()
{
	panic("rdb");
}

