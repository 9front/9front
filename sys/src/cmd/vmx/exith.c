#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

int persist = 1;

typedef struct ExitInfo ExitInfo;
struct ExitInfo {
	char *raw;
	char *name;
	uvlong qual;
	uvlong pa, va;
	u32int ilen, iinfo;
};

char *x86reg[16] = {
	RAX, RCX, RDX, RBX,
	RSP, RBP, RSI, RDI,
	R8, R9, R10, R11,
	R12, R13, R14, R15
};
char *x86segreg[8] = {
	"cs", "ds", "es", "fs", "gs", "ss",
};

static void
skipinstr(ExitInfo *ei)
{
	rset(RPC, rget(RPC) + ei->ilen);
}

static void
iohandler(ExitInfo *ei)
{
	int port, len, inc, isin;
	int asz, seg;
	uintptr addr;
	u32int val;
	uvlong vval;
	uintptr cx;
	static int seglook[8] = {SEGES, SEGCS, SEGSS, SEGDS, SEGFS, SEGGS};
	TLB tlb;
	
	port = ei->qual >> 16 & 0xffff;
	len = (ei->qual & 7) + 1;
	isin = (ei->qual & 8) != 0;
	if((ei->qual & 1<<4) == 0){ /* not a string instruction */
		if(isin){
			val = io(1, port, 0, len);
			rsetsz(RAX, val, len);
		}else
			io(0, port, rget(RAX), len);
		skipinstr(ei);
		return;
	}
	if((rget("flags") & 0x400) != 0) inc = -len;
	else inc = len;
	switch(ei->iinfo >> 7 & 7){
	case 0: asz = 2; break;
	default: asz = 4; break;
	case 2: asz = 8; break;
	}
	if((ei->qual & 1<<5) != 0)
		cx = rgetsz(RCX, asz);
	else
		cx = 1;
	addr = isin ? rget(RDI) : rget(RSI);
	if(isin)
		seg = SEGES;
	else
		seg = seglook[ei->iinfo >> 15 & 7];
	memset(&tlb, 0, sizeof(TLB));
	for(; cx > 0; cx--){
		if(isin){
			vval = io(1, port, 0, len);
			if(x86access(seg, addr, asz, &vval, len, ACCW, &tlb) < 0)
				goto err;
		}else{
			if(x86access(seg, addr, asz, &vval, len, ACCR, &tlb) < 0)
				goto err;
			io(0, port, vval, len);
		}
		addr += inc;
	}
	skipinstr(ei);
err:
	if((ei->qual & 1<<5) != 0)
		rsetsz(RCX, cx, asz);
	if(isin)
		rsetsz(RDI, addr, asz);
	else
		rsetsz(RSI, addr, asz);
}

static uvlong
defaultmmio(int op, uvlong addr, uvlong val)
{
	switch(op){
	case MMIORD:
		vmerror("read from unmapped address %#ullx (pc=%#ullx)", addr, rget(RPC));
		break;
	case MMIOWR:
		vmerror("write to unmapped address %#ullx (val=%#ullx,pc=%#ullx)", addr, val, rget(RPC));
		break;
	}
	return 0;
}

static void
eptfault(ExitInfo *ei)
{
	if(x86step() > 0)
		skipinstr(ei);
}

typedef struct CPUID CPUID;
struct CPUID {
	u32int idx;
	u32int ax, bx, cx, dx;
};
static CPUID *cpuidf;
static int ncpuidf;

static void
auxcpuidproc(void *vpfd)
{
	int *pfd;
	
	pfd = vpfd;
	close(pfd[1]);
	close(0);
	open("/dev/null", OREAD);
	dup(pfd[0], 1);
	close(pfd[0]);
	procexecl(nil, "/bin/aux/cpuid", "cpuid", "-r", nil);
	threadexits("exec: %r");
}

void
cpuidinit(void)
{
	int pfd[2];
	Biobuf *bp;
	char *l, *f[5];
	CPUID *cp;
	
	pipe(pfd);
	procrfork(auxcpuidproc, pfd, 4096, RFFDG);
	close(pfd[0]);
	bp = Bfdopen(pfd[1], OREAD);
	if(bp == nil) sysfatal("Bopenfd: %r");
	for(; l = Brdstr(bp, '\n', 1), l != nil; free(l)){
		if(tokenize(l, f, 5) < 5) continue;
		cpuidf = realloc(cpuidf, (ncpuidf + 1) * sizeof(CPUID));
		cp = cpuidf + ncpuidf++;
		cp->idx = strtoul(f[0], nil, 16);
		cp->ax = strtoul(f[1], nil, 16);
		cp->bx = strtoul(f[2], nil, 16);
		cp->cx = strtoul(f[3], nil, 16);
		cp->dx = strtoul(f[4], nil, 16);
	}
	Bterm(bp);
	close(pfd[1]);
}

CPUID *
getcpuid(ulong idx)
{
	CPUID *cp;
	
	for(cp = cpuidf; cp < cpuidf + ncpuidf; cp++)
		if(cp->idx == idx)
			return cp;
	return nil;
}

int maxcpuid = 7;

static void
cpuid(ExitInfo *ei)
{
	u32int ax, bx, cx, dx;
	CPUID *cp;
	static CPUID def;
	
	ax = rget(RAX);
	cp = getcpuid(ax);
	if(cp == nil) cp = &def;
	switch(ax){
	case 0: /* highest register & GenuineIntel */
		ax = maxcpuid;
		bx = cp->bx;
		dx = cp->dx;
		cx = cp->cx;
		break;
	case 1: /* features */
		ax = cp->ax;
		bx = cp->bx & 0xffff;
		cx = cp->cx & 0x60de2203;
		dx = cp->dx & 0x0782a179;
		break;
	case 2: goto literal; /* cache stuff */
	case 3: goto zero; /* processor serial number */
	case 4: goto zero; /* cache stuff */
	case 5: goto zero; /* monitor/mwait */
	case 6: goto zero; /* thermal management */
	case 7: goto zero; /* more features */
	case 10: goto zero; /* performance counters */
	case 0x80000000: /* highest register */
		ax = 0x80000008;
		bx = cx = dx = 0;
		break;
	case 0x80000001: /* signature & ext features */
		ax = cp->ax;
		bx = 0;
		cx = cp->cx & 0x121;
		if(sizeof(uintptr) == 8)
			dx = cp->dx & 0x24100800;
		else
			dx = cp->dx & 0x04100000;
		break;
	case 0x80000002: goto literal; /* brand string */
	case 0x80000003: goto literal; /* brand string */
	case 0x80000004: goto literal; /* brand string */
	case 0x80000005: goto zero; /* reserved */
	case 0x80000006: goto literal; /* cache info */
	case 0x80000007: goto zero; /* invariant tsc */
	case 0x80000008: goto literal; /* address bits */
	literal:
		ax = cp->ax;
		bx = cp->bx;
		cx = cp->cx;
		dx = cp->dx;
		break;
	default:
		vmerror("unknown cpuid field eax=%#ux", ax);
	zero:
		ax = 0;
		bx = 0;
		cx = 0;
		dx = 0;
		break;
	}
	rset(RAX, ax);
	rset(RBX, bx);
	rset(RCX, cx);
	rset(RDX, dx);
	skipinstr(ei);
}

static void
rdwrmsr(ExitInfo *ei)
{
	u32int cx;
	u64int val;
	int rd;
	
	rd = ei->name[1] == 'r';
	cx = rget(RCX);
	val = (uvlong)rget(RDX) << 32 | rget(RAX);
	switch(cx){
	case 0x277:
		if(rd) val = rget("pat");
		else rset("pat", val);
		break;
	case 0x8B: val = 0; break; /* microcode update */
	default:
		if(rd){
			vmerror("read from unknown MSR %#ux ignored", cx);
			val = 0;
		}else
			vmerror("write to unknown MSR %#ux ignored (val=%#ullx)", cx, val);
		break;
	}
	if(rd){
		rset(RAX, (u32int)val);
		rset(RDX, (u32int)(val >> 32));
	}
	skipinstr(ei);
}

static void
movdr(ExitInfo *ei)
{
	static char *dr[8] = { "dr0", "dr1", "dr2", "dr3", nil, nil, "dr6", "dr7" };
	int q;
	
	q = ei->qual;
	if((q & 6) == 4){
		postexc("#gp", 0);
		return;
	}
	if((q & 16) != 0)
		rset(x86reg[q >> 8 & 15], rget(dr[q & 7]));
	else
		rset(dr[q & 7], rget(x86reg[q >> 8 & 15]));
	skipinstr(ei);
}

static void
movcr(ExitInfo *ei)
{
	u32int q;
	
	q = ei->qual;
	switch(q & 15){
	case 0:
		switch(q >> 4 & 3){
		case 0:
			vmdebug("illegal CR0 write, value %#ux", rget(x86reg[q >> 8 & 15]));
			rset("cr0real", rget(x86reg[q >> 8 & 15]));
			skipinstr(ei);
			break;
		case 1:
			vmerror("shouldn't happen: trap on MOV from CR0");
			rset(x86reg[q >> 8 & 15], rget("cr0fake"));
			skipinstr(ei);
			break;
		case 2:
			vmerror("shouldn't happen: trap on CLTS");
			rset("cr0real", rget("cr0real") & ~8);
			skipinstr(ei);
			break;
		case 3:
			vmerror("LMSW handler unimplemented");
			postexc("#ud", NOERRC);
		}
		break;
	case 4:
		switch(q >> 4 & 3){
		case 0:
			vmdebug("illegal CR4 write, value %#ux", rget(x86reg[q >> 8 & 15]));
			rset("cr4real", rget(x86reg[q >> 8 & 15]));
			skipinstr(ei);
			break;
		case 1:
			vmerror("shouldn't happen: trap on MOV from CR4");
			rset(x86reg[q >> 8 & 15], rget("cr3fake"));
			skipinstr(ei);
			break;
		default:
			vmerror("unknown CR4 operation %d", q);
			postexc("#ud", NOERRC);
		}
		break;
	default:
		vmerror("access to unknown control register CR%d", ei->qual & 15);
		postexc("#ud", NOERRC);
	}
}

static void
dbgexc(ExitInfo *ei)
{
	rset("dr6", rget("dr6") | ei->qual);
	postexc("#db", NOERRC);
}

static void
hlt(ExitInfo *ei)
{
	if(irqactive < 0)
		state = VMHALT;
	skipinstr(ei);
}

static void
irqackhand(ExitInfo *ei)
{
	irqack(ei->qual);
}

typedef struct ExitType ExitType;
struct ExitType {
	char *name;
	void (*f)(ExitInfo *);
};
static ExitType etypes[] = {
	{"io", iohandler},
	{".cpuid", cpuid},
	{".hlt", hlt},
	{"eptfault", eptfault},
	{"*ack", irqackhand},
	{".rdmsr", rdwrmsr},
	{".wrmsr", rdwrmsr},
	{".movdr", movdr},
	{"#db", dbgexc},
	{"movcr", movcr},
};

void
processexit(char *msg)
{
	static char msgc[1024];
	char *f[32];
	int nf;
	ExitType *et;
	int i;
	ExitInfo ei;
	extern int getexit;

	strcpy(msgc, msg);
	nf = tokenize(msgc, f, nelem(f));
	if(nf < 2) sysfatal("invalid wait message: %s", msg);
	memset(&ei, 0, sizeof(ei));
	ei.raw = msg;
	ei.name = f[0];
	ei.qual = strtoull(f[1], nil, 0);
	for(i = 2; i < nf; i += 2){
		if(strcmp(f[i], "pc") == 0)
			rpoke(RPC, strtoull(f[i+1], nil, 0), 1);
		else if(strcmp(f[i], "sp") == 0)
			rpoke(RSP, strtoull(f[i+1], nil, 0), 1);
		else if(strcmp(f[i], "ax") == 0)
			rpoke(RAX, strtoull(f[i+1], nil, 0), 1);
		else if(strcmp(f[i], "ilen") == 0)
			ei.ilen = strtoul(f[i+1], nil, 0);
		else if(strcmp(f[i], "iinfo") == 0)
			ei.iinfo = strtoul(f[i+1], nil, 0);
		else if(strcmp(f[i], "pa") == 0)
			ei.pa = strtoull(f[i+1], nil, 0);
		else if(strcmp(f[i], "va") == 0)
			ei.va = strtoull(f[i+1], nil, 0);
	}
	if(*f[0] == '*') getexit++;
	for(et = etypes; et < etypes + nelem(etypes); et++)
		if(strcmp(et->name, f[0]) == 0){
			et->f(&ei);
			return;
		}
	if(*f[0] == '.'){
		vmerror("vmx: unknown instruction %s", f[0]+1);
		postexc("#ud", NOERRC);
		return;
	}
	if(*f[0] == '*'){
		vmerror("vmx: unknown notification %s", f[0]+1);
		return;
	}
	if(persist){
		vmerror("unknown exit: %s", msg);
		state = VMDEAD;
	}else
		sysfatal("unknown exit: %s", msg);
}
