/*
 * traps, exceptions, faults and interrupts on ar7161
 */
#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"ureg.h"
#include	"io.h"
#include	"../port/error.h"


int	intr(Ureg*);
void	kernfault(Ureg*, int);
void	noted(Ureg*, ulong);
void	rfnote(Ureg**);

char *excname[] =
{
	"trap: external interrupt",
	"trap: TLB modification (store to unwritable)",
	"trap: TLB miss (load or fetch)",
	"trap: TLB miss (store)",
	"trap: address error (load or fetch)",
	"trap: address error (store)",
	"trap: bus error (fetch)",
	"trap: bus error (data load or store)",
	"trap: system call",
	"breakpoint",
	"trap: reserved instruction",
	"trap: coprocessor unusable",
	"trap: arithmetic overflow",
	"trap: TRAP exception",
	"trap: VCE (instruction)",
	"trap: floating-point exception",
	"trap: coprocessor 2 implementation-specific", /* used as sys call for debugger */
	"trap: corextend unusable",
	"trap: precise coprocessor 2 exception",
	"trap: TLB read-inhibit",
	"trap: TLB execute-inhibit",
	"trap: undefined 21",
	"trap: undefined 22",
	"trap: WATCH exception",
	"trap: machine checkcore",
	"trap: undefined 25",
	"trap: undefined 26",
	"trap: undefined 27",
	"trap: undefined 28",
	"trap: undefined 29",
	"trap: cache error",
	"trap: VCE (data)",
};

char *fpcause[] =
{
	"inexact operation",
	"underflow",
	"overflow",
	"division by zero",
	"invalid operation",
};
char	*fpexcname(Ureg*, ulong, char*, uint);
#define FPEXPMASK	(0x3f<<12)	/* Floating exception bits in fcr31 */

struct {
	char	*name;
	uint	off;
} regname[] = {
	"STATUS", offsetof(Ureg, status),
	"PC",	offsetof(Ureg, pc),
	"SP",	offsetof(Ureg, sp),
	"CAUSE",offsetof(Ureg, cause),
	"BADADDR", offsetof(Ureg, badvaddr),
	"TLBVIRT", offsetof(Ureg, tlbvirt),
	"HI",	offsetof(Ureg, hi),
	"LO",	offsetof(Ureg, lo),
	"R31",	offsetof(Ureg, r31),
	"R30",	offsetof(Ureg, r30),
	"R28",	offsetof(Ureg, r28),
	"R27",	offsetof(Ureg, r27),
	"R26",	offsetof(Ureg, r26),
	"R25",	offsetof(Ureg, r25),
	"R24",	offsetof(Ureg, r24),
	"R23",	offsetof(Ureg, r23),
	"R22",	offsetof(Ureg, r22),
	"R21",	offsetof(Ureg, r21),
	"R20",	offsetof(Ureg, r20),
	"R19",	offsetof(Ureg, r19),
	"R18",	offsetof(Ureg, r18),
	"R17",	offsetof(Ureg, r17),
	"R16",	offsetof(Ureg, r16),
	"R15",	offsetof(Ureg, r15),
	"R14",	offsetof(Ureg, r14),
	"R13",	offsetof(Ureg, r13),
	"R12",	offsetof(Ureg, r12),
	"R11",	offsetof(Ureg, r11),
	"R10",	offsetof(Ureg, r10),
	"R9",	offsetof(Ureg, r9),
	"R8",	offsetof(Ureg, r8),
	"R7",	offsetof(Ureg, r7),
	"R6",	offsetof(Ureg, r6),
	"R5",	offsetof(Ureg, r5),
	"R4",	offsetof(Ureg, r4),
	"R3",	offsetof(Ureg, r3),
	"R2",	offsetof(Ureg, r2),
	"R1",	offsetof(Ureg, r1),
};


void
kvce(Ureg *ur, int ecode)
{
	char c;
	Pte **p;
	Page **pg;
	Segment *s;
	ulong addr, soff;

	c = 'D';
	if(ecode == CVCEI)
		c = 'I';
	print("Trap: VCE%c: addr=%#lux\n", c, ur->badvaddr);
	if(up && !(ur->badvaddr & KSEGM)) {
		addr = ur->badvaddr;
		s = seg(up, addr, 0);
		if(s == nil){
			print("kvce: no seg for %#lux\n", addr);
			for(;;);
		}
		addr &= ~(BY2PG-1);
		soff = addr - s->base;
		p = &s->map[soff/PTEMAPMEM];
		if(*p){
			pg = &(*p)->pages[(soff&(PTEMAPMEM-1))/BY2PG];
			if(*pg)
				print("kvce: pa=%#lux, va=%#lux\n",
					(*pg)->pa, (*pg)->va);
			else
				print("kvce: no *pg\n");
		}else
			print("kvce: no *p\n");
	}
}

void
trap(Ureg *ur)
{
	int ecode, user, cop, x, fpchk;
	ulong fpfcr31;
	char buf[2*ERRMAX], buf1[ERRMAX], *fpexcep;
	static int dumps;

	if (up && (char *)(ur) - ((char *)up - KSTACK) < 1024 && dumps++ == 0) {
		iprint("trap: proc %ld kernel stack getting full\n", up->pid);
		dumpregs(ur);
		dumpstack();
		for(;;);
	}
	if (up == nil &&
	    (char *)(ur) - (char *)m->stack < 1024 && dumps++ == 0) {
		iprint("trap: cpu%d kernel stack getting full\n", m->machno);
		dumpregs(ur);
		dumpstack();
		for(;;);
	}
	user = kenter(ur);
	if (ur->cause & TS)
		panic("trap: tlb shutdown");
	ecode = (ur->cause>>2)&EXCMASK;
	fpchk = 0;
	switch(ecode){
	case CINT:
		preempted(intr(ur));
		break;

	case CFPE:
		panic("FP exception but no FPU");	/* no fpu on 24KEc */
		break;

	case CTLBM:
	case CTLBL:
	case CTLBS:
		if(up == nil || !user && (ur->badvaddr & KSEGM) == KSEG3) {
			kfault(ur);
			break;
		}
		x = up->insyscall;
		up->insyscall = 1;
		spllo();
		faultmips(ur, user, ecode);
		up->insyscall = x;
		break;

	case CVCEI:
	case CVCED:
		kvce(ur, ecode);
		goto Default;

	case CWATCH:
		if(!user)
			panic("watchpoint trap from kernel mode pc=%#p",
				ur->pc);
		fpwatch(ur);
		break;

	case CCPU:
		cop = (ur->cause>>28)&3;
		if(user && up && cop == 1) {
			if(up->fpstate & FPillegal) {
				/* someone used floating point in a note handler */
				postnote(up, 1,
					"sys: floating point in note handler",
					NDebug);
				break;
			}
			/* no fpu, so we can only emulate fp ins'ns */
			if (fpuemu(ur) < 0)
				postnote(up, 1,
					"sys: fp instruction not emulated",
					NDebug);
			else
				fpchk = 1;
			break;
		}
		/* Fallthrough */

	Default:
	default:
		if(user) {
			spllo();
			snprint(buf, sizeof buf, "sys: %s", excname[ecode]);
			postnote(up, 1, buf, NDebug);
			break;
		}
		if (ecode == CADREL || ecode == CADRES)
			iprint("kernel addr exception for va %#p pid %#ld %s\n",
				ur->badvaddr, (up? up->pid: 0),
				(up? up->text: ""));
		print("cpu%d: kernel %s pc=%#lux\n",
			m->machno, excname[ecode], ur->pc);
		dumpregs(ur);
		dumpstack();
		if(m->machno == 0)
			spllo();
		exit(1);
	}

	if(fpchk) {
		fpfcr31 = up->fpsave->fpstatus;
		if((fpfcr31>>12) & ((fpfcr31>>7)|0x20) & 0x3f) {
			spllo();
			fpexcep	= fpexcname(ur, fpfcr31, buf1, sizeof buf1);
			snprint(buf, sizeof buf, "sys: fp: %s", fpexcep);
			postnote(up, 1, buf, NDebug);
		}
	}

	splhi();

	if(user){
		notify(ur);
		/* replicate fpstate to ureg status */
	//	if(up->fpstate != FPactive)
	//		ur->status &= ~CU1;
		kexit(ur);
	}
}


char*
fpexcname(Ureg *ur, ulong fcr31, char *buf, uint size)
{
	int i;
	char *s;
	ulong fppc;

	fppc = ur->pc;
	if(ur->cause & BD)	/* branch delay */
		fppc += 4;
	s = 0;
	if(fcr31 & (1<<17))
		s = "unimplemented operation";
	else{
		fcr31 >>= 7;		/* trap enable bits */
		fcr31 &= (fcr31>>5);	/* anded with exceptions */
		for(i=0; i<5; i++)
			if(fcr31 & (1<<i))
				s = fpcause[i];
	}

	if(s == 0)
		return "no floating point exception";

	snprint(buf, size, "%s fppc=%#lux", s, fppc);
	return buf;
}


static void
getpcsp(ulong *pc, ulong *sp)
{
	*pc = getcallerpc(&pc);
	*sp = (ulong)&pc-4;
}

void
callwithureg(void (*fn)(Ureg*))
{
	Ureg ureg;

	memset(&ureg, 0, sizeof ureg);
	getpcsp((ulong*)&ureg.pc, (ulong*)&ureg.sp);
	ureg.r31 = getcallerpc(&fn);
	fn(&ureg);
}

static void
_dumpstack(Ureg *ureg)
{
	ulong l, v, top, i;
	extern ulong etext;

	iprint("ktrace /kernel/path %.8lux %.8lux %.8lux\n",
		ureg->pc, ureg->sp, ureg->r31);
	if(up == nil)
		top = (ulong)MACHADDR + MACHSIZE;
	else
		top = (ulong)up;
	i = 0;
	for(l=ureg->sp; l < top; l += BY2WD) {
		v = *(ulong*)l;
		if(KTZERO < v && v < (ulong)&etext) {
			iprint("%.8lux=%.8lux ", l, v);
			if((++i%4) == 0){
				print("\n");
				delay(200);
			}
		}
	}
	print("\n");
}

void
dumpstack(void)
{
	callwithureg(_dumpstack);
}

static ulong
R(Ureg *ur, int i)
{
	uchar *s;

	s = (uchar*)ur;
	return *(ulong*)(s + regname[i].off);
}

void
dumpregs(Ureg *ur)
{
	int i;

	if(up)
		iprint("registers for %s %lud\n", up->text, up->pid);
	else
		iprint("registers for kernel\n");

	for(i = 0; i < nelem(regname); i += 2)
		iprint("%s\t%#.8lux\t%s\t%#.8lux\n",
			regname[i].name,   R(ur, i),
			regname[i+1].name, R(ur, i+1));
}
