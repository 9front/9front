#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

typedef struct ExitInfo ExitInfo;
struct ExitInfo {
	char *raw;
	char *name;
	uvlong qual;
	uvlong pa, va;
	u32int ilen, iinfo;
};

static void
skipinstr(ExitInfo *ei)
{
	rset(RPC, rget(RPC) + ei->ilen);
}

static int
stepmmio(uvlong pa, uvlong *val, int size, ExitInfo *ei)
{
	extern uchar *tmp;
	extern uvlong tmpoff;
	void *targ;
	uvlong pc, si;
	char buf[ERRMAX];
	extern int getexit;
	
	memset(tmp, 0, BY2PG);
	targ = tmp + (pa & 0xfff);
	switch(size){
	case 1: *(u8int*)targ = *val; break;
	case 2: *(u16int*)targ = *val; break;
	case 4: *(u32int*)targ = *val; break;
	case 8: *(u64int*)targ = *val; break;
	}
	pc = rget(RPC);
	si = rget("si");
	rcflush(0);
	if(ctl("step -map %#ullx vm %#ullx", pa & ~0xfff, tmpoff) < 0){
		rerrstr(buf, sizeof(buf));
		if(strcmp(buf, "step failed") == 0){
			vmerror("vmx step failure (old pc=%#ullx, new pc=%#ullx, cause=%#q)", pc, rget(RPC), ei->raw);
			getexit++;
			return -1;
		}
		sysfatal("ctl(stepmmio): %r");
	}
	switch(size){
	case 1: *val = *(u8int*)targ; break;
	case 2: *val = *(u16int*)targ; break;
	case 4: *val = *(u32int*)targ; break;
	case 8: *val = *(u64int*)targ; break;
	}
	return 0;
}

extern u32int io(int, u16int, u32int, int);

static void
iohandler(ExitInfo *ei)
{
	int port, len, isin;
	u32int val;
	u64int ax;
	
	port = ei->qual >> 16 & 0xffff;
	len = (ei->qual & 7) + 1;
	isin = (ei->qual & 8) != 0;
	if((ei->qual & 1<<4) != 0){
		vmerror("i/o string instruction not implemented");
		postexc("#ud", 0);
		return;
	}
	if(isin){
		val = io(1, port, 0, len);
		ax = rget(RAX);
		if(len == 1) ax = ax & ~0xff | val & 0xff;
		else if(len == 2) ax = ax & ~0xffff | val & 0xffff;
		else ax = val;
		rset(RAX, ax);
	}else{
		ax = rget(RAX);
		if(len == 1) ax = (u8int) ax;
		else if(len == 2) ax = (u16int) ax;
		io(0, port, ax, len);
	}
	skipinstr(ei);
}

typedef struct MemHandler MemHandler;
struct MemHandler {
	uvlong lo, hi;
	uvlong (*f)(int, uvlong, uvlong);
};

MemHandler memh[32];
int nmemh;

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
	MemHandler *h;
	static MemHandler def = {.f defaultmmio};
	int size;
	uvlong val;
	
	for(h = memh; h < memh + nmemh; h++)
		if(ei->pa >= h->lo && ei->pa <= h->hi)
			break;
	if(h == memh + nmemh)
		h = &def;
	size = 8;
	if((ei->qual & 5) != 0){
		val = h->f(MMIORD, ei->pa, 0);
		stepmmio(ei->pa, &val, size, ei);
	}else{
		val = h->f(MMIOWRP, ei->pa, 0);
		if(stepmmio(ei->pa, &val, size, ei) < 0)
			return;
		h->f(MMIOWR, ei->pa, val);
	}
}

void
registermmio(uvlong lo, uvlong hi, uvlong (*f)(int, uvlong, uvlong))
{
	assert(nmemh < nelem(memh));
	memh[nmemh].lo = lo;
	memh[nmemh].hi = hi;
	memh[nmemh].f = f;
	nmemh++;
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
		ax = 7;
		bx = cp->bx;
		dx = cp->dx;
		cx = cp->cx;
		break;
	case 1: /* features */
		ax = cp->ax;
		bx = cp->bx & 0xffff;
		cx = cp->cx & 0x60de2203;
		dx = cp->dx & 0x0682a179;
		break;
	case 2: goto literal; /* cache stuff */
	case 3: goto zero; /* processor serial number */
	case 4: goto literal; /* cache stuff */
	case 5: goto zero; /* monitor/mwait */
	case 6: goto zero; /* thermal management */
	case 7: goto zero; /* more features */
	case 0x80000000: /* highest register */
		ax = 0x80000008;
		bx = cx = dx = 0;
		break;
	case 0x80000001: /* signature & ext features */
		ax = cp->ax;
		bx = 0;
		cx = cp->cx & 0x121;
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
	default:
		if(rd)
			vmerror("read from unknown MSR %#x ignored", cx);
		else
			vmerror("write to unknown MSR %#x ignored (val=%#ullx)", cx, val);
		break;
	}
	if(rd){
		rset(RAX, val);
		rset(RDX, val >> 32);
	}
	skipinstr(ei);
}

static void
hlt(ExitInfo *ei)
{
	if(irqactive == 0)
		halt = 1;
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
	if(strcmp(ei.name, "io") != 0 && strcmp(ei.name, "eptfault") != 0 && strcmp(ei.name, "*ack") != 0 && strcmp(ei.name, ".hlt") != 0) vmdebug("exit: %s", msg);
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
		postexc("#ud", 0);
		return;
	}
	if(*f[0] == '*'){
		vmerror("vmx: unknown notification %s", f[0]+1);
		return;
	}
	sysfatal("vmx: unknown exit: %s", msg);
}
