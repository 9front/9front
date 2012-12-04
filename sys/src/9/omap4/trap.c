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
void (*irqhandler[MAXMACH][256])(Ureg *, void *);
void *irqaux[MAXMACH][256];

static char *trapname[] = {
	"reset", /* wtf */
	"undefined instruction",
	"supervisor call",
	"prefetch abort",
	"data abort",
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
intenable(int i, void (*fn)(Ureg *, void *), void *aux)
{
	intd[0x40 + (i / 32)] = 1 << (i % 32);
	irqhandler[m->machno][i] = fn;
	irqaux[m->machno][i] = aux;
}

void
irqroute(int i, void (*fn)(Ureg *, void *), void *aux)
{
	ulong x, y, z;

	if(irqhandler[m->machno][i] != nil){
		print("irqroute: irq already used: i=%d pc=%#p newfn=%#p oldfn=%#p\n", i, getcallerpc(&i), fn, irqhandler[m->machno][i]);
		return;
	}
	intenable(32 + i, fn, aux);
	x = intd[0x208 + i/4];
	y = 0xFF << ((i%4) * 8);
	z = 1 << (m->machno + (i%4) * 8);
	x = (x & ~y) | z;
	intd[0x208 + i/4] = x;
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
	tos->pcycles = t + up->pcycles;
	tos->pid = up->pid;
}

int
notify(Ureg *ureg)
{
	int l;
	ulong s, sp;
	Note *n;

	if(up->procctl)
		procctl(up);
	if(up->nnote == 0)
		return 0;
	s = spllo();
	qlock(&up->debug);
	up->notepending = 0;
	n = &up->note[0];
	if(strncmp(n->msg, "sys:", 4) == 0){
		l = strlen(n->msg);
		if(l > ERRMAX-15)
			l = ERRMAX-15;
		sprint(n->msg + l, " pc=0x%.8lux", ureg->pc);
	}
	if(n->flag != NUser && (up->notified || up->notify == 0)){
		if(n->flag == NDebug)
			pprint("suicide: %s\n", n->msg);
		qunlock(&up->debug);
		pexit(n->msg, n->flag != NDebug);
	}
	if(up->notified){
		qunlock(&up->debug);
		splhi();
		return 0;
	}
	if(!up->notify){
		qunlock(&up->debug);
		pexit(n->msg, n->flag != NDebug);
	}
	sp = ureg->sp;
	sp -= 256 + sizeof(Ureg);
	if(!okaddr((ulong)up->notify, 1, 0)
	|| !okaddr(sp - ERRMAX - 4 * BY2WD, sizeof(Ureg) + ERRMAX + 4 * BY2WD, 1)){
		qunlock(&up->debug);
		pprint("suicide: bad address in notify\n");
		pexit("Suicide", 0);
	}
	
	memmove((void *) sp, ureg, sizeof(Ureg));
	((void**)sp)[-1] = up->ureg;
	up->ureg = (void *) sp;
	sp -= BY2WD + ERRMAX;
	memmove((void *) sp, up->note[0].msg, ERRMAX);
	sp -= 3 * BY2WD;
	((ulong*)sp)[2] = sp + 3 * BY2WD;
	((Ureg**)sp)[1] = up->ureg;
	((ulong*)sp)[0] = 0;
	memset(ureg, 0, sizeof *ureg);
	ureg->psr = PsrMusr;
	ureg->sp = sp;
	ureg->pc = (ulong) up->notify;
	up->notified = 1;
	up->nnote--;
	memmove(&up->lastnote, &up->note[0], sizeof(Note));
	memmove(&up->note[0], &up->note[1], up->nnote * sizeof(Note));
	
	qunlock(&up->debug);
	splx(s);
	return 1;
}

void
noted(Ureg *ureg, ulong arg0)
{
	Ureg *nureg;
	ulong oureg, sp;
	
	qlock(&up->debug);
	if(arg0 != NRSTR && !up->notified){
		qunlock(&up->debug);
		pprint("call to noted() when not notified\n");
		pexit("Suicide", 0);
	}
	up->notified = 0;
	
	nureg = up->ureg;
	oureg = (ulong) nureg;
	if(!okaddr((ulong) oureg - BY2WD, BY2WD + sizeof(Ureg), 0)){
		qunlock(&up->debug);
		pprint("bad ureg in noted or call to noted when not notified\n");
		pexit("Suicide", 0);
	}
	nureg->psr = nureg->psr & PsrOK | ureg->psr & ~PsrOK;
	memmove(ureg, nureg, sizeof(Ureg));

	if(!okaddr(nureg->pc, 1, 0) && !okaddr(nureg->sp, BY2WD, 0)
	&& (arg0 == NCONT || arg0 == NRSTR || arg0 == NSAVE)){
		qunlock(&up->debug);
		pprint("suicide: trap in noted\n");
		pexit("Suicide", 0);
	}
	switch(arg0){
	case NCONT:
	case NRSTR:
		up->ureg = (Ureg *) (*(ulong *)(oureg - BY2WD));
		qunlock(&up->debug);
		break;

	case NSAVE:
		qunlock(&up->debug);
		sp = oureg - 4 * BY2WD - ERRMAX;
		splhi();
		ureg->sp = sp;
		((ulong *) sp)[1] = oureg;
		((ulong *) sp)[0] = 0;
		break;
	
	default:
		pprint("unknown noted arg 0x%lux\n", arg0);
		up->lastnote.flag = NDebug;
		/* fallthrough */
	
	case NDFLT:
		qunlock(&up->debug);
		if(up->lastnote.flag == NDebug)
			pprint("suicide: %s\n", up->lastnote.msg);
		pexit(up->lastnote.msg, up->lastnote.flag != NDebug);
	}
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
			irqhandler[m->machno][intn](ureg, irqaux[m->machno][intn]);
		else
			print("unexpected interrupt %d\n", intn);
		intc[4] = x;
		if(intn != 29)
			preempted();
		splhi();
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
		if(up->procctl || up->nnote)
			notify(ureg);
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
	vlong startns, stopns;

	m->syscall++;
	up->insyscall = 1;
	up->pc = ureg->pc;
	up->dbgreg = ureg;
	cycles(&up->kentry);
	scall = ureg->r0;
	up->scallnr = scall;
	spllo();

	sp = ureg->sp;
	up->nerrlab = 0;
	ret = -1;
	if(!waserror()){
		validaddr(sp, sizeof(Sargs) + BY2WD, 0);
		up->s = *((Sargs*)(sp + BY2WD));
		if(up->procctl == Proc_tracesyscall){
			syscallfmt(scall, ureg->pc, (va_list)up->s.args);
			s = splhi();
			up->procctl = Proc_stopme;
			procctl(up);
			splx(s);
			startns = todget(nil);
		}
		if(scall >= nsyscall){
			postnote(up, 1, "sys: bad syscall", NDebug);
			error(Ebadarg);
		}
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
		stopns = todget(nil);
		sysretfmt(scall, (va_list)up->s.args, ret, startns, stopns);
		s = splhi();
		up->procctl = Proc_stopme;
		procctl(up);
		splx(s);
	}

	up->insyscall = 0;
	up->psstate = nil;

	if(scall == NOTED)
		noted(ureg, up->s.args[0]);
	if(scall != RFORK && (up->procctl || up->nnote)){
		splhi();
		notify(ureg);
	}
	splhi();
	if(up->delaysched){
		sched();
		splhi();
	}
	updatetos();
	up->dbgreg = nil;
}
