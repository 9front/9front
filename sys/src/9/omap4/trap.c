#include "u.h"
#include "ureg.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "../port/systab.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "arm.h"
#include "tos.h"

extern uchar *periph;
ulong *intc, *intd;
void (*irqhandler[MAXMACH][256])(Ureg*);

static char *trapname[] = {
	"reset", /* wtf */
	"undefined instruction",
	"supervisor call",
	"prefetch abort",
	"data abort",
	"unknown trap",
	"IRQ",
	"FIQ",
};

void
trapinit(void)
{
	extern void _dataabort(), _undefined(), _prefabort(), _irq(), _fiq(), _reset(), _wtftrap(), _syscall();
	int i;
	ulong *trapv;

	trapv = (ulong *) 0xFFFF0000;
	for(i = 0; i < 8; i++)
		trapv[i] = 0xE59FF018;
	trapv[8] = (ulong) _reset;
	trapv[9] = (ulong) _undefined;
	trapv[10] = (ulong) _syscall;
	trapv[11] = (ulong) _prefabort;
	trapv[12] = (ulong) _dataabort;
	trapv[13] = (ulong) _wtftrap;
	trapv[14] = (ulong) _irq;
	trapv[15] = (ulong) _fiq;
	
	intc = (ulong *) (periph + 0x100);
	intc[1] = 0;
	intc[0] |= 1;
	intd = (ulong *) (periph + 0x1000);
	intd[0] |= 1;
}

void
intenable(int i, void (*fn)(Ureg *))
{
	intd[0x40 + (i / 32)] = 1 << (i % 32);
	irqhandler[m->machno][i] = fn;
}

void
irqroute(int i, void (*fn)(Ureg *))
{
	ulong x, y, z;
	
	intenable(32 + i, fn);
	x = intd[0x208 + i/4];
	y = 0xFF << ((i%4) * 8);
	z = 1 << (m->machno + (i%4) * 8);
	x = (x & ~y) | z;
	intd[0x208 + i/4] = x;
//	intd[0x200/4 + (i+32)/32] = 1 << (i % 32);
}

void
faultarm(Ureg *ureg)
{
	ulong addr, sr;
	int user, n, read, nsys;
	extern ulong getdfsr(void), getifsr(void), getdfar(void), getifar(void);
	char buf[ERRMAX];

	user = (ureg->psr & PsrMask) == PsrMusr;
	read = 1;
	if(ureg->type == 3){
		sr = getifsr();
		addr = getifar();
	}else{
		sr = getdfsr();
		addr = getdfar();
		if(sr & (1<<11))
			read = 0;
	}
	if(!user && addr >= KZERO){
	kernel:
		serialoq = nil;
		printureg(ureg);
		panic("kernel fault: addr=%#.8lux pc=%#.8lux lr=%#.8lux sr=%#.8lux", addr, ureg->pc, ureg->r14, sr);
	}
	if(up == nil){
		serialoq = nil;
		printureg(ureg);
		panic("%s fault: up=nil addr=%#.8lux pc=%#.8lux sr=%#.8lux", user ? "user" : "kernel", addr, ureg->pc, sr);
	}
	nsys = up->insyscall;
	up->insyscall = 1;
	n = fault(addr, read);
	if(n < 0){
		if(!user)
			goto kernel;
		spllo();
		sprint(buf, "sys: trap: fault %s addr=0x%lux", read ? "read" : "write", addr);
		postnote(up, 1, buf, NDebug);
	}
	up->insyscall = nsys;
}

void
updatetos(void)
{
	Tos *tos;
	uvlong t;
	
	tos = (Tos*) (USTKTOP - sizeof(Tos));
	cycles(&t);
	tos->kcycles += t - up->kentry;
	tos->pcycles = up->pcycles;
	tos->pid = up->pid;
}

void
trap(Ureg *ureg)
{
	int user, intn, x;
	char buf[ERRMAX];
	
	user = (ureg->psr & PsrMask) == PsrMusr;
	if(user){
		fillureguser(ureg);
		up->dbgreg = ureg;
		cycles(&up->kentry);
	}
	switch(ureg->type){
	case 3:
	case 4:
		faultarm(ureg);
		break;
	case 6:
		x = intc[3];
		intn = x & 0x3FF;
		if(irqhandler[m->machno][intn] != nil)
			irqhandler[m->machno][intn](ureg);
		intc[4] = x;
		if(intn != 29)
			preempted();
		if(up && up->delaysched && (intn == 29)){
			sched();
			splhi();
		}
		break;
	default:
		if(user){
			spllo();
			sprint("sys: trap: %s", trapname[ureg->type]);
			postnote(up, 1, buf, NDebug);
		}else{
			serialoq = nil;
			printureg(ureg);
			panic("%s", trapname[ureg->type]);
		}
	}
	if(user){
		updatetos();
		up->dbgreg = nil;
	}
}

void
syscall(Ureg *ureg)
{
	int scall, ret;
	ulong s, sp;
	char *e;

	m->syscall++;
	up->insyscall = 1;
	up->pc = ureg->pc;
	up->dbgreg = ureg;
	cycles(&up->kentry);
	if(up->procctl == Proc_tracesyscall){
		up->procctl = Proc_stopme;
		procctl(up);
	}
	scall = ureg->r0;
	up->scallnr = scall;
//	print("%s\n", sysctab[scall]);
	spllo();

	sp = ureg->sp;
	up->nerrlab = 0;
	ret = -1;
	if(!waserror()){
		if(scall >= nsyscall){
			postnote(up, 1, "sys: bad syscall", NDebug);
			error(Ebadarg);
		}
		validaddr(sp, sizeof(Sargs) + BY2WD, 0);
		up->s = *((Sargs*)(sp + BY2WD));
		up->psstate = sysctab[scall];
		ret = systab[scall](up->s.args);
		poperror();
	}else{
		e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
	}
	if(up->nerrlab != 0)
		panic("error stack");
	ureg->r0 = ret;

	if(up->procctl == Proc_tracesyscall){
		up->procctl = Proc_stopme;
		s = splhi();
		procctl(up);
		splx(s);
	}
	up->insyscall = 0;
	up->psstate = nil;
	splhi();
	if(up->delaysched){
		sched();
		splhi();
	}
	updatetos();
	up->dbgreg = nil;
}
