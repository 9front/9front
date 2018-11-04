#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "ureg.h"

extern int vmxon(u64int);
extern int vmxoff(void);
extern int vmclear(u64int);
extern int vmptrld(u64int);
extern int vmlaunch(Ureg *, int);
extern int vmread(u32int, uintptr *);
extern int vmwrite(u32int, uintptr);
extern int invept(u32int, uvlong, uvlong);
extern int invvpid(u32int, uvlong, uvlong);

static int gotvmx;
static vlong procb_ctls, pinb_ctls;

enum {
	VMX_BASIC_MSR = 0x480,
	VMX_PINB_CTLS_MSR = 0x481,
	VMX_PROCB_CTLS_MSR = 0x482,
	VMX_VMEXIT_CTLS_MSR = 0x483,
	VMX_VMENTRY_CTLS_MSR = 0x484,
	VMX_MISC_MSR = 0x485,
	VMX_CR0_FIXED0 = 0x486,
	VMX_CR0_FIXED1 = 0x487,
	VMX_CR4_FIXED0 = 0x488,
	VMX_CR4_FIXED1 = 0x489,
	VMX_VMCS_ENUM = 0x48A,
	VMX_PROCB_CTLS2_MSR = 0x48B,
	VMX_TRUE_PINB_CTLS_MSR = 0x48D,
	VMX_TRUE_PROCB_CTLS_MSR = 0x48E,
	VMX_TRUE_EXIT_CTLS_MSR = 0x48F,
	VMX_TRUE_ENTRY_CTLS_MSR = 0x490,
	VMX_VMFUNC_MSR = 0x491,
	
	PINB_CTLS = 0x4000,
	PINB_EXITIRQ = 1<<0,
	PINB_EXITNMI = 1<<3,
	
	PROCB_CTLS = 0x4002,
	PROCB_IRQWIN = 1<<2,
	PROCB_EXITHLT = 1<<7,
	PROCB_EXITINVLPG = 1<<9,
	PROCB_EXITMWAIT = 1<<10,
	PROCB_EXITRDPMC = 1<<11,
	PROCB_EXITRDTSC = 1<<12,
	PROCB_EXITCR3LD = 1<<15,
	PROCB_EXITCR3ST = 1<<16,
	PROCB_EXITCR8LD = 1<<19,
	PROCB_EXITCR8ST = 1<<20,
	PROCB_EXITMOVDR = 1<<23,
	PROCB_EXITIO = 1<<24,
	PROCB_MONTRAP = 1<<27,
	PROCB_MSRBITMAP = 1<<28,
	PROCB_EXITMONITOR = 1<<29,
	PROCB_EXITPAUSE = 1<<30,
	PROCB_USECTLS2 = 1<<31,
	
	PROCB_CTLS2 = 0x401E,
	PROCB_EPT = 1<<1,
	PROCB_EXITGDT = 1<<2,
	PROCB_VPID = 1<<5,
	PROCB_UNRESTR = 1<<7,

	EXC_BITMAP = 0x4004,
	PFAULT_MASK = 0x4006,
	PFAULT_MATCH = 0x4008,
	CR3_TARGCNT = 0x400a,
	MSR_BITMAP = 0x2004,
	
	VMEXIT_CTLS = 0x400c,
	VMEXIT_ST_DEBUG = 1<<2,
	VMEXIT_HOST64 = 1<<9,
	VMEXIT_LD_IA32_PERF_GLOBAL_CTRL = 1<<12,
	VMEXIT_ST_IA32_PAT = 1<<18,
	VMEXIT_LD_IA32_PAT = 1<<19,
	VMEXIT_ST_IA32_EFER = 1<<20,
	VMEXIT_LD_IA32_EFER = 1<<21,	
	
	VMEXIT_MSRSTCNT = 0x400e,
	VMEXIT_MSRLDCNT = 0x4010,
	VMEXIT_MSRSTADDR = 0x2006,
	VMEXIT_MSRLDADDR = 0x2008,
	VMENTRY_MSRLDADDR = 0x200A,
	
	VMENTRY_CTLS = 0x4012,
	VMENTRY_LD_DEBUG = 1<<2,
	VMENTRY_GUEST64 = 1<<9,
	VMENTRY_LD_IA32_PERF_GLOBAL_CTRL = 1<<13,
	VMENTRY_LD_IA32_PAT = 1<<14,
	VMENTRY_LD_IA32_EFER = 1<<15,
	
	VMENTRY_MSRLDCNT = 0x4014,
	VMENTRY_INTRINFO = 0x4016,
	VMENTRY_INTRCODE = 0x4018,
	VMENTRY_INTRILEN = 0x401a,
	
	VMCS_LINK = 0x2800,
	
	GUEST_ES = 0x800,
	GUEST_CS = 0x802,
	GUEST_SS = 0x804,
	GUEST_DS = 0x806,
	GUEST_FS = 0x808,
	GUEST_GS = 0x80A,
	GUEST_LDTR = 0x80C,
	GUEST_TR = 0x80E,
	GUEST_CR0 = 0x6800,
	GUEST_CR3 = 0x6802,
	GUEST_CR4 = 0x6804,
	GUEST_ESLIMIT = 0x4800,
	GUEST_CSLIMIT = 0x4802,
	GUEST_SSLIMIT = 0x4804,
	GUEST_DSLIMIT = 0x4806,
	GUEST_FSLIMIT = 0x4808,
	GUEST_GSLIMIT = 0x480A,
	GUEST_LDTRLIMIT = 0x480C,
	GUEST_TRLIMIT = 0x480E,
	GUEST_GDTRLIMIT = 0x4810,
	GUEST_IDTRLIMIT = 0x4812,
	GUEST_ESPERM = 0x4814,
	GUEST_CSPERM = 0x4816,
	GUEST_SSPERM = 0x4818,
	GUEST_DSPERM = 0x481A,
	GUEST_FSPERM = 0x481C,
	GUEST_GSPERM = 0x481E,
	GUEST_LDTRPERM = 0x4820,
	GUEST_TRPERM = 0x4822,
	GUEST_CR0MASK = 0x6000,
	GUEST_CR4MASK = 0x6002,
	GUEST_CR0SHADOW = 0x6004,
	GUEST_CR4SHADOW = 0x6006,
	GUEST_ESBASE = 0x6806,
	GUEST_CSBASE = 0x6808,
	GUEST_SSBASE = 0x680A,
	GUEST_DSBASE = 0x680C,
	GUEST_FSBASE = 0x680E,
	GUEST_GSBASE = 0x6810,
	GUEST_LDTRBASE = 0x6812,
	GUEST_TRBASE = 0x6814,
	GUEST_GDTRBASE = 0x6816,
	GUEST_IDTRBASE = 0x6818,
	GUEST_DR7 = 0x681A,
	GUEST_RSP = 0x681C,
	GUEST_RIP = 0x681E,
	GUEST_RFLAGS = 0x6820,
	GUEST_IA32_DEBUGCTL = 0x2802,
	GUEST_IA32_PAT = 0x2804,
	GUEST_IA32_EFER = 0x2806,
	GUEST_IA32_PERF_GLOBAL_CTRL = 0x2808,
	
	HOST_ES = 0xC00,
	HOST_CS = 0xC02,
	HOST_SS = 0xC04,
	HOST_DS = 0xC06,
	HOST_FS = 0xC08,
	HOST_GS = 0xC0A,
	HOST_TR = 0xC0C,
	HOST_CR0 = 0x6C00,
	HOST_CR3 = 0x6C02,
	HOST_CR4 = 0x6C04,
	HOST_FSBASE = 0x6C06,
	HOST_GSBASE = 0x6C08,
	HOST_TRBASE = 0x6C0A,
	HOST_GDTR = 0x6C0C,
	HOST_IDTR = 0x6C0E,
	HOST_RSP = 0x6C14,
	HOST_RIP = 0x6C16,
	HOST_IA32_PAT = 0x2C00,
	HOST_IA32_EFER = 0x2C02,
	HOST_IA32_PERF_GLOBAL_CTRL = 0x2C04,
	
	GUEST_CANINTR = 0x4824,
	
	VM_INSTRERR = 0x4400,
	VM_EXREASON = 0x4402,
	VM_EXINTRINFO = 0x4404,
	VM_EXINTRCODE = 0x4406,
	VM_IDTVECINFO = 0x4408,
	VM_IDTVECCODE = 0x440A,
	VM_EXINSTRLEN = 0x440C,
	VM_EXINSTRINFO = 0x440E,
	VM_EXQUALIF = 0x6400,
	VM_IORCX = 0x6402,
	VM_IORSI = 0x6404,
	VM_IORDI = 0x6406,
	VM_IORIP = 0x6408,
	VM_GUESTVA = 0x640A,
	VM_GUESTPA = 0x2400,
	
	VM_VPID = 0x000,
	VM_EPTPIDX = 0x0004,
	
	VM_EPTP = 0x201A,
	VM_EPTPLA = 0x2024,
	
	INVLOCAL = 1,
};

enum {
	CR0RSVD = 0x1ffaffc0,
	CR4RSVD = 0xff889000,
	CR4MCE = 1<<6,
	CR4VMXE = 1<<13,
	CR4SMXE = 1<<14,
	CR4PKE = 1<<22,
	
	CR0KERNEL = CR0RSVD | 0x30 | (uintptr)0xFFFFFFFF00000000ULL,
	CR4KERNEL = CR4RSVD | CR4VMXE | CR4SMXE | CR4MCE | CR4PKE | (uintptr)0xFFFFFFFF00000000ULL
};

enum {
	MAXMSR = 512,
};

typedef struct VmxMach VmxMach;
typedef struct Vmx Vmx;
typedef struct VmCmd VmCmd;
typedef struct VmMem VmMem;
typedef struct VmIntr VmIntr;

struct VmMem {
	uvlong addr;
	Segment *seg;
	uintptr off;
	char *name;
	VmMem *next, *prev;
	u16int attr;
};

struct VmxMach {
	char vmxon[4096]; /* has to be at the start for alignment */
	QLock;
	int vms;
	Vmx *active;
};
#define vmxmach ((VmxMach*)((m)->vmx))
#define vmxmachp(n) ((VmxMach*)(MACHP(n)->vmx))

struct VmIntr {
	u32int info, code, ilen;
};

struct Vmx {
	uchar vmcs[4096]; /* page aligned */
	uvlong pml4[512]; /* page aligned */
	u32int msrbits[1024]; /* page aligned */
	FPsave fp; /* page aligned */
	u64int msrhost[MAXMSR*2]; /* 16 byte aligned */
	u64int msrguest[MAXMSR*2]; /* 16 byte aligned */

	enum {
		VMXINIT,
		VMXREADY,
		VMXRUNNING,
		VMXDEAD,
		VMXENDING,
	} state;
	int index, machno;
	char errstr[ERRMAX];
	Ureg ureg;
	uintptr cr2;
	uintptr dr[8]; /* DR7 is also kept in VMCS */
	u8int launched;
	u8int vpid;
	enum {
		FLUSHVPID = 1,
		FLUSHEPT = 2,
		STEP = 4,
		POSTEX = 8,
		POSTIRQ = 16,
	} onentry;
	
	Rendez cmdwait;
	Lock cmdlock;
	VmCmd *firstcmd, **lastcmd;
	VmCmd *postponed;
	VmMem mem;
	
	enum {
		GOTEXIT = 1,
		GOTIRQACK = 2,
	} got;
	VmIntr exc, irq, irqack;

	int nmsr;
};

struct VmCmd {
	enum {
		CMDFDONE = 1,
		CMDFFAIL = 2,
		CMDFPOSTP = 4,
	} flags;
	u8int scratched;
	Rendez;
	Lock;
	int (*cmd)(VmCmd *, va_list);
	int retval;
	char *errstr;
	va_list va;
	VmCmd *next;
	Vmx *vmx;
};

static char Equit[] = "vmx: ending";

static char *statenames[] = {
	[VMXINIT] "init",
	[VMXREADY] "ready",
	[VMXRUNNING] "running",
	[VMXDEAD] "dead",
	[VMXENDING]"ending"
};

static Vmx *moribund;
static QLock vmxtablock;
static Vmx **vmxtab;
static int nvmxtab;

void
vmxprocrestore(Proc *p)
{
	int s;
	Vmx *vmx;
	
	s = splhi();
	vmx = p->vmx;
	if(vmxmach->active != vmx){
		if(vmx != nil && vmptrld(PADDR(vmx->vmcs)) < 0)
			panic("VMPTRLD(%p) failed", vmx->vmcs);
		vmxmach->active = vmx;
	}
	splx(s);
}

static u64int
vmcsread(u32int addr)
{
	int rc;
	u64int val;

	val = 0;
	rc = vmread(addr, (uintptr *) &val);
	if(rc >= 0 && sizeof(uintptr) == 4 && (addr & 0x6000) == 0x2000)
		rc = vmread(addr | 1, (uintptr *) &val + 1);
	if(rc < 0){
		char errbuf[128];
		snprint(errbuf, sizeof(errbuf), "vmcsread failed (%#.4ux)", addr);
		error(errbuf);
	}
	return val;
}

static void
vmcswrite(u32int addr, u64int val)
{
	int rc;
	
	rc = vmwrite(addr, val);
	if(rc >= 0 && sizeof(uintptr) == 4 && (addr & 0x6000) == 0x2000)
		rc = vmwrite(addr | 1, val >> 32);
	if(rc < 0){
		char errbuf[128];
		snprint(errbuf, sizeof(errbuf), "vmcswrite failed (%#.4ux = %#.16ullx)", addr, val);
		error(errbuf);
	}
}

static uvlong
parseval(char *s)
{
	uvlong v;
	char *p;

	v = strtoull(s, &p, 0);
	if(p == s || *p != 0) error("invalid value");
	return v;
}

static char *
cr0fakeread(Vmx *, char *p, char *e)
{
	uvlong guest, mask, shadow;
	
	guest = vmcsread(GUEST_CR0);
	mask = vmcsread(GUEST_CR0MASK);
	shadow = vmcsread(GUEST_CR0SHADOW);
	return seprint(p, e, "%#.*ullx", sizeof(uintptr) * 2, guest & ~mask | shadow & mask);
}

static char *
cr4fakeread(Vmx *, char *p, char *e)
{
	uvlong guest, mask, shadow;
	
	guest = vmcsread(GUEST_CR4);
	mask = vmcsread(GUEST_CR4MASK);
	shadow = vmcsread(GUEST_CR4SHADOW);
	return seprint(p, e, "%#.*ullx", sizeof(uintptr) * 2, guest & ~mask | shadow & mask);
}

static void
updatelma(void)
{
	uvlong cr0, efer, nefer, ectrl;

	if(sizeof(uintptr) != 8) return;
	cr0 = vmcsread(GUEST_CR0);
	efer = vmcsread(GUEST_IA32_EFER);
	nefer = efer & ~0x400 | efer << 2 & cr0 >> 21 & 0x400;
	if(efer == nefer) return;
	vmcswrite(GUEST_IA32_EFER, nefer);
	ectrl = vmcsread(VMENTRY_CTLS);
	ectrl = ectrl & ~0x200 | nefer >> 1 & 0x200;
	vmcswrite(VMENTRY_CTLS, ectrl);
}

static int
cr0realwrite(Vmx *, char *s)
{
	uvlong v;
	
	v = parseval(s);
	vmcswrite(GUEST_CR0, vmcsread(GUEST_CR0) & CR0KERNEL | v & ~CR0KERNEL);
	updatelma();
	return 0;
}

static int
cr0maskwrite(Vmx *, char *s)
{
	uvlong v;
	
	v = parseval(s);
	vmcswrite(GUEST_CR0MASK, v | CR0KERNEL);
	return 0;
}

static int
eferwrite(Vmx *, char *s)
{
	uvlong v;
	
	v = parseval(s);
	vmcswrite(GUEST_IA32_EFER, v);
	updatelma();
	return 0;
}

static int
cr4realwrite(Vmx *, char *s)
{
	uvlong v;
	
	v = parseval(s);
	vmcswrite(GUEST_CR4, vmcsread(GUEST_CR4) & CR4KERNEL | v & ~CR4KERNEL);
	return 0;
}

static int
cr4maskwrite(Vmx *, char *s)
{
	uvlong v;
	
	v = parseval(s);
	vmcswrite(GUEST_CR4MASK, v | CR4KERNEL);
	return 0;
}

static int
dr7write(Vmx *vmx, char *s)
{
	uvlong v;
	
	v = (u32int) parseval(s);
	vmcswrite(GUEST_DR7, vmx->dr[7] = (u32int) v);
	return 0;
}

static int
readonly(Vmx *, char *)
{
	return -1;
}

static int
dr6write(Vmx *vmx, char *s)
{
	uvlong v;
	
	v = parseval(s);
	vmx->dr[6] = (u32int) v;
	return 0;
}

typedef struct GuestReg GuestReg;
struct GuestReg {
	int offset;
	u8int size; /* in bytes; 0 means == uintptr */
	char *name;
	char *(*read)(Vmx *, char *, char *);
	int (*write)(Vmx *, char *);
};
#define VMXVAR(x) ~(ulong)&(((Vmx*)0)->x)
#define UREG(x) VMXVAR(ureg.x)
static GuestReg guestregs[] = {
	{GUEST_RIP, 0, "pc"},
	{GUEST_RSP, 0, "sp"},
	{GUEST_RFLAGS, 0, "flags"},
	{UREG(ax), 0, "ax"},
	{UREG(bx), 0, "bx"},
	{UREG(cx), 0, "cx"},
	{UREG(dx), 0, "dx"},
	{UREG(bp), 0, "bp"},
	{UREG(si), 0, "si"},
	{UREG(di), 0, "di"},
#ifdef RMACH
	{UREG(r8), 0, "r8"},
	{UREG(r9), 0, "r9"},
	{UREG(r10), 0, "r10"},
	{UREG(r11), 0, "r11"},
	{UREG(r12), 0, "r12"},
	{UREG(r13), 0, "r13"},
	{UREG(r14), 0, "r14"},
	{UREG(r15), 0, "r15"},
#endif
	{GUEST_GDTRBASE, 0, "gdtrbase"},
	{GUEST_GDTRLIMIT, 4, "gdtrlimit"},
	{GUEST_IDTRBASE, 0, "idtrbase"},
	{GUEST_IDTRLIMIT, 4, "idtrlimit"},
	{GUEST_CS, 2, "cs"},
	{GUEST_CSBASE, 0, "csbase"},
	{GUEST_CSLIMIT, 4, "cslimit"},
	{GUEST_CSPERM, 4, "csperm"},
	{GUEST_DS, 2, "ds"},
	{GUEST_DSBASE, 0, "dsbase"},
	{GUEST_DSLIMIT, 4, "dslimit"},
	{GUEST_DSPERM, 4, "dsperm"},
	{GUEST_ES, 2, "es"},
	{GUEST_ESBASE, 0, "esbase"},
	{GUEST_ESLIMIT, 4, "eslimit"},
	{GUEST_ESPERM, 4, "esperm"},
	{GUEST_FS, 2, "fs"},
	{GUEST_FSBASE, 0, "fsbase"},
	{GUEST_FSLIMIT, 4, "fslimit"},
	{GUEST_FSPERM, 4, "fsperm"},
	{GUEST_GS, 2, "gs"},
	{GUEST_GSBASE, 0, "gsbase"},
	{GUEST_GSLIMIT, 4, "gslimit"},
	{GUEST_GSPERM, 4, "gsperm"},
	{GUEST_SS, 2, "ss"},
	{GUEST_SSBASE, 0, "ssbase"},
	{GUEST_SSLIMIT, 4, "sslimit"},
	{GUEST_SSPERM, 4, "ssperm"},
	{GUEST_TR, 2, "tr"},
	{GUEST_TRBASE, 0, "trbase"},
	{GUEST_TRLIMIT, 4, "trlimit"},
	{GUEST_TRPERM, 4, "trperm"},
	{GUEST_LDTR, 2, "ldtr"},
	{GUEST_LDTRBASE, 0, "ldtrbase"},
	{GUEST_LDTRLIMIT, 4, "ldtrlimit"},
	{GUEST_LDTRPERM, 4, "ldtrperm"},
	{GUEST_CR0, 0, "cr0real", nil, cr0realwrite},
	{GUEST_CR0SHADOW, 0, "cr0fake", cr0fakeread},
	{GUEST_CR0MASK, 0, "cr0mask", nil, cr0maskwrite},
	{VMXVAR(cr2), 0, "cr2"},
	{GUEST_CR3, 0, "cr3"},
	{GUEST_CR4, 0, "cr4real", nil, cr4realwrite},
	{GUEST_CR4SHADOW, 0, "cr4fake", cr4fakeread},
	{GUEST_CR4MASK, 0, "cr4mask", nil, cr4maskwrite},
	{GUEST_IA32_PAT, 8, "pat"},
	{GUEST_IA32_EFER, 8, "efer", nil, eferwrite},
	{VMXVAR(dr[0]), 0, "dr0"},
	{VMXVAR(dr[1]), 0, "dr1"},
	{VMXVAR(dr[2]), 0, "dr2"},
	{VMXVAR(dr[3]), 0, "dr3"},
	{VMXVAR(dr[6]), 0, "dr6", nil, dr6write},
	{GUEST_DR7, 0, "dr7", nil, dr7write},
	{VM_INSTRERR, 4, "instructionerror", nil, readonly},
	{VM_EXREASON, 4, "exitreason", nil, readonly},
	{VM_EXQUALIF, 0, "exitqualification", nil, readonly},
	{VM_EXINTRINFO, 4, "exitinterruptinfo", nil, readonly},
	{VM_EXINTRCODE, 4, "exitinterruptcode", nil, readonly},
	{VM_EXINSTRLEN, 4, "exitinstructionlen", nil, readonly},
	{VM_EXINSTRINFO, 4, "exitinstructioninfo", nil, readonly},
	{VM_GUESTVA, 0, "exitva", nil, readonly},
	{VM_GUESTPA, 0, "exitpa", nil, readonly},
	{VM_IDTVECINFO, 4, "idtinterruptinfo", nil, readonly},
	{VM_IDTVECCODE, 4, "idtinterruptcode", nil, readonly},
};

static int
vmokpage(u64int addr)
{
	return (addr & 0xfff) == 0 && addr >> 48 == 0;
}

static uvlong *
eptwalk(Vmx *vmx, uvlong addr)
{
	uvlong *tab, *nt;
	uvlong v;
	int i;
	
	tab = vmx->pml4;
	if(tab == nil) error(Egreg);
	for(i = 3; i >= 1; i--){
		tab += addr >> 12 + 9 * i & 0x1ff;
		v = *tab;
		if((v & 3) == 0){
			nt = mallocalign(BY2PG, BY2PG, 0, 0);
			if(nt == nil) error(Enomem);
			memset(nt, 0, BY2PG);
			v = PADDR(nt) | 0x407;
			*tab = v;
		}
		tab = KADDR(v & ~0xfff);
	}
	return tab + (addr >> 12 & 0x1ff);
}

static void
eptfree(uvlong *tab, int level)
{
	int i;
	uvlong v, *t;
	
	if(tab == nil) error(Egreg);
	if(level < 3){
		for(i = 0; i < 512; i++){
			v = tab[i];
			if((v & 3) == 0) continue;
			t = KADDR(v & ~0xfff);
			eptfree(t, level + 1);
			tab[i] = 0;
		}
	}
	if(level > 0)
		free(tab);		
}

static void
epttranslate(Vmx *vmx, VmMem *mp, uvlong end)
{
	uvlong p, v;

	if((mp->addr & 0xfff) != 0 || (end & 0xfff) != 0 || (uint)mp->attr >= 0x1000)
		error(Egreg);
	if(mp->seg != nil){
		switch(mp->seg->type & SG_TYPE){
		default:
			error(Egreg);
		case SG_FIXED:
		case SG_STICKY:
			break;
		}
		if(mp->seg->base + mp->off + (end - mp->addr) > mp->seg->top)
			error(Egreg);
		for(p = mp->addr, v = mp->off; p != end; p += BY2PG, v += BY2PG)
			*eptwalk(vmx, p) = mp->seg->map[v/PTEMAPMEM]->pages[(v & PTEMAPMEM-1)/BY2PG]->pa | mp->attr;
	}else {
		for(p = mp->addr; p != end; p += BY2PG)
			*eptwalk(vmx, p) = 0;
	}
	vmx->onentry |= FLUSHEPT;
}

static char *mtype[] = {"uc", "wc", "02", "03", "wt", "wp", "wb", "07"};

static int
cmdgetmeminfo(VmCmd *cmd, va_list va)
{
	VmMem *mp;
	char *p0, *e, *p;
	char attr[4];
	char mt[4];
	
	p0 = va_arg(va, char *);
	e = va_arg(va, char *);
	p = p0;
	if(p < e) *p = 0;
	for(mp = cmd->vmx->mem.next; mp != &cmd->vmx->mem; mp = mp->next){
		if(mp->seg == nil)
			continue;
		attr[0] = (mp->attr & 1) != 0 ? 'r' : '-';
		attr[1] = (mp->attr & 2) != 0 ? 'w' : '-';
		attr[2] = (mp->attr & 4) != 0 ? 'x' : '-';
		attr[3] = 0;
		*(ushort*)mt = *(u16int*)mtype[mp->attr >> 3 & 7];
		mt[2] = (mp->attr & 0x40) != 0 ? '!' : 0;
		mt[3] = 0;
		p = seprint(p, e, "%s %s %#llux %#llux %s %#llux\n", attr, mt, mp->addr, mp->next->addr, mp->name, (uvlong)mp->off);
	}
	return p - p0;
}

static void
vmmeminsert(VmMem *l, VmMem *p)
{
	p->prev = l->prev;
	p->next = l;
	p->prev->next = p;
	p->next->prev = p;
}

static VmMem *
vmmemremove(VmMem *p)
{
	VmMem *r;
	
	r = p->next;
	p->next->prev = p->prev;
	p->prev->next = p->next;
	free(p->name);
	putseg(p->seg);
	free(p);
	return r;
}

static int
cmdclearmeminfo(VmCmd *cmd, va_list)
{
	VmMem *mp;
	Vmx *vmx;

	vmx = cmd->vmx;
	eptfree(cmd->vmx->pml4, 0);
	for(mp = vmx->mem.next; mp != &vmx->mem; )
		mp = vmmemremove(mp);
	vmx->mem.prev = &vmx->mem;
	vmx->mem.next = &vmx->mem;
	vmx->onentry |= FLUSHEPT;
	return 0;
}


static void
vmmemupdate(Vmx *vmx, VmMem *mp, uvlong end)
{
	VmMem *p, *q;
	
	for(p = vmx->mem.prev; p != &vmx->mem; p = p->prev)
		if(p->addr <= end || end == 0)
			break;
	if(p == &vmx->mem || p->addr < mp->addr){
		q = smalloc(sizeof(VmMem));
		*q = *p;
		if(p->seg != nil){
			incref(q->seg);
			kstrdup(&q->name, p->name);
		}
		vmmeminsert(p->next, q);
	}else
		q = p;
	if(q->seg != nil)
		q->off += end - q->addr;
	q->addr = end;
	for(p = vmx->mem.next; p != &vmx->mem; p = p->next)
		if(p->addr >= mp->addr)
			break;
	vmmeminsert(p, mp);
	while(p != q)
		p = vmmemremove(p);
	for(p = vmx->mem.next; p != &vmx->mem; )
		if(p->seg == p->prev->seg && (p->seg == nil || p->addr - p->prev->addr == p->off - p->prev->off))
			p = vmmemremove(p);
		else
			p = p->next;
}

extern Segment* (*_globalsegattach)(char*);

static int
cmdsetmeminfo(VmCmd *cmd, va_list va)
{
	char *p0, *p, *q, *r;
	int j;
	char *f[10];
	VmMem *mp;
	int rc;
	uvlong end;
	
	p0 = va_arg(va, char *);
	p = p0;
	mp = nil;
	for(;;){
		q = strchr(p, '\n');
		if(q == 0) break;
		*q = 0;
		if(mp == nil){
			mp = malloc(sizeof(VmMem));
			if(mp == nil)
				error(Enomem);
		}
		memset(mp, 0, sizeof(VmMem));
		if(waserror()){
			putseg(mp->seg);
			free(mp->name);
			free(mp);
			nexterror();
		}
		rc = tokenize(p, f, nelem(f));
		p = q + 1;
		if(rc == 0){
			poperror();
			continue;
		}
		if(rc != 4 && rc != 6) error("number of fields wrong");
		for(q = f[0]; *q != 0; q++)
			switch(*q){
			case 'r': if((mp->attr & 1) != 0) goto tinval; mp->attr |= 1; break;
			case 'w': if((mp->attr & 2) != 0) goto tinval; mp->attr |= 2; break;
			case 'x': if((mp->attr & 4) != 0) goto tinval; mp->attr |= 0x404; break;
			case '-': break;
			default: tinval: error("invalid access field");
			}
		for(j = 0; j < 8; j++)
			if(strncmp(mtype[j], f[1], 2) == 0){
				mp->attr |= j << 3;
				break;
			}
		if(j == 8 || strlen(f[1]) > 3) error("invalid memory type");
		if(f[1][2] == '!') mp->attr |= 0x40;
		else if(f[1][2] != 0) error("invalid memory type");
		mp->addr = strtoull(f[2], &r, 0);
		if(*r != 0 || !vmokpage(mp->addr)) error("invalid low guest physical address");
		end = strtoull(f[3], &r, 0);
		if(*r != 0 || !vmokpage(end) || end <= mp->addr) error("invalid high guest physical address");
		if((mp->attr & 7) != 0){
			if(rc != 6) error("number of fields wrong");
			mp->seg = _globalsegattach(f[4]);
			if(mp->seg == nil) error("no such segment");
			if(mp->seg->base + mp->off + (end - mp->addr) > mp->seg->top) error("out of bounds");
			kstrdup(&mp->name, f[4]);
			mp->off = strtoull(f[5], &r, 0);
			if(*r != 0 || !vmokpage(mp->off)) error("invalid offset");
		}
		poperror();
		epttranslate(cmd->vmx, mp, end);
		vmmemupdate(cmd->vmx, mp, end);
		mp = nil;
	}
	free(mp);
	return p - p0;
}

static void
vmxreset(void)
{
	ulong regs[4];
	vlong msr;
	int i;

	cpuid(1, regs);
	if((regs[2] & 1<<5) == 0) return;
	/* check if disabled by BIOS */
	if(rdmsr(0x3a, &msr) < 0) return;
	if((msr & 5) != 5){
		if((msr & 1) == 0){ /* msr still unlocked */
			wrmsr(0x3a, msr | 5);
			if(rdmsr(0x3a, &msr) < 0)
				return;
		}
		if((msr & 5) != 5)
			return;
	}
	if(rdmsr(VMX_PROCB_CTLS_MSR, &msr) < 0) return;
	if((vlong)msr >= 0) return;
	if(rdmsr(VMX_PROCB_CTLS2_MSR, &msr) < 0) return;
	if((msr >> 32 & PROCB_EPT) == 0 || (msr >> 32 & PROCB_VPID) == 0) return;
	for(i = 0; i < conf.nmach; i++){
		MACHP(i)->vmx = mallocalign(sizeof(VmxMach), 4096, 0, 0);
		if(vmxmachp(i) == nil)
			error(Enomem);
	}
	gotvmx = 1;
}

static void
vmxaddmsr(Vmx *vmx, u32int msr, u64int gval)
{
	int i;

	if(vmx->nmsr >= MAXMSR)
		error("too many MSRs");
	i = 2 * vmx->nmsr++;
	vmx->msrhost[i] = msr;
	rdmsr(msr, (vlong *) &vmx->msrhost[i+1]);
	vmx->msrguest[i] = msr;
	vmx->msrguest[i+1] = gval;
	vmcswrite(VMENTRY_MSRLDCNT, vmx->nmsr);
	vmcswrite(VMEXIT_MSRSTCNT, vmx->nmsr);
	vmcswrite(VMEXIT_MSRLDCNT, vmx->nmsr);
}

static void
vmxtrapmsr(Vmx *vmx, u32int msr, enum { TRAPRD = 1, TRAPWR = 2 } state)
{
	u32int m;
	
	if(msr >= 0x2000 && (u32int)(msr - 0xc0000000) >= 0x2000)
		return;
	msr = msr & 0x1fff | msr >> 18 & 0x2000;
	m = 1<<(msr & 31);
	if((state & TRAPRD) != 0)
		vmx->msrbits[msr / 32] |= m;
	else
		vmx->msrbits[msr / 32] &= ~m;
	if((state & TRAPWR) != 0)
		vmx->msrbits[msr / 32 + 512] |= m;
	else
		vmx->msrbits[msr / 32 + 512] &= ~m;
}

static void
vmcsinit(Vmx *vmx)
{
	vlong msr;
	u32int x;
	
	memset(&vmx->ureg, 0, sizeof(vmx->ureg));
	vmx->launched = 0;
	vmx->onentry = 0;	
	
	if(rdmsr(VMX_BASIC_MSR, &msr) < 0) error("rdmsr(VMX_BASIC_MSR) failed");
	if((msr & 1ULL<<55) != 0){
		if(rdmsr(VMX_TRUE_PROCB_CTLS_MSR, &procb_ctls) < 0) error("rdmsr(VMX_TRUE_PROCB_CTLS_MSR) failed");
		if(rdmsr(VMX_TRUE_PINB_CTLS_MSR, &pinb_ctls) < 0) error("rdmsr(VMX_TRUE_PINB_CTLS_MSR) failed");
	}else{
		if(rdmsr(VMX_PROCB_CTLS_MSR, &procb_ctls) < 0) error("rdmsr(VMX_PROCB_CTLS_MSR) failed");
		if(rdmsr(VMX_PINB_CTLS_MSR, &pinb_ctls) < 0) error("rdmsr(VMX_PINB_CTLS_MSR) failed");
	}

	if(rdmsr(VMX_PINB_CTLS_MSR, &msr) < 0) error("rdmsr(VMX_PINB_CTLS_MSR failed");
	x = (u32int)pinb_ctls | 1<<1 | 1<<2 | 1<<4; /* currently reserved default1 bits */
	x |= PINB_EXITIRQ | PINB_EXITNMI;
	x &= pinb_ctls >> 32;
	vmcswrite(PINB_CTLS, x);
	
	if(rdmsr(VMX_PROCB_CTLS_MSR, &msr) < 0) error("rdmsr(VMX_PROCB_CTLS_MSR failed");
	x = (u32int)procb_ctls | 1<<1 | 7<<4 | 1<<8 | 1<<13 | 1<<14 | 1<<26; /* currently reserved default1 bits */
	x |= PROCB_EXITHLT | PROCB_EXITMWAIT;
	x |= PROCB_EXITMOVDR | PROCB_EXITIO | PROCB_EXITMONITOR | PROCB_MSRBITMAP;
	x |= PROCB_USECTLS2;
	x &= msr >> 32;
	vmcswrite(PROCB_CTLS, x);
	
	if(rdmsr(VMX_PROCB_CTLS2_MSR, &msr) < 0) error("rdmsr(VMX_PROCB_CTLS2_MSR failed");
	x = PROCB_EPT | PROCB_VPID | PROCB_UNRESTR;
	x &= msr >> 32;
	vmcswrite(PROCB_CTLS2, x);
	
	if(rdmsr(VMX_VMEXIT_CTLS_MSR, &msr) < 0) error("rdmsr(VMX_VMEXIT_CTLS_MSR failed");
	x = (u32int)msr;
	if(sizeof(uintptr) == 8) x |= VMEXIT_HOST64;
	x |= VMEXIT_LD_IA32_PAT | VMEXIT_LD_IA32_EFER | VMEXIT_ST_DEBUG | VMEXIT_ST_IA32_EFER;
	x &= msr >> 32;
	vmcswrite(VMEXIT_CTLS, x);
	
	if(rdmsr(VMX_VMENTRY_CTLS_MSR, &msr) < 0) error("rdmsr(VMX_VMENTRY_CTLS_MSR failed");
	x = (u32int)msr;
	x |= VMENTRY_LD_IA32_PAT | VMENTRY_LD_IA32_EFER | VMENTRY_LD_DEBUG;
	x &= msr >> 32;
	vmcswrite(VMENTRY_CTLS, x);
	
	vmcswrite(CR3_TARGCNT, 0);
	vmcswrite(VMENTRY_INTRINFO, 0);
	vmcswrite(VMCS_LINK, -1);
	
	vmcswrite(HOST_CS, KESEL);
	vmcswrite(HOST_DS, KDSEL);
	vmcswrite(HOST_ES, KDSEL);
	vmcswrite(HOST_FS, KDSEL);
	vmcswrite(HOST_GS, KDSEL);
	vmcswrite(HOST_SS, KDSEL);
	vmcswrite(HOST_TR, TSSSEL);
	vmcswrite(HOST_CR0, getcr0() & ~0xe);
	vmcswrite(HOST_CR3, getcr3());
	vmcswrite(HOST_CR4, getcr4());
	rdmsr(FSbase, &msr);
	vmcswrite(HOST_FSBASE, msr);
	rdmsr(GSbase, &msr);
	vmcswrite(HOST_GSBASE, msr);
	vmcswrite(HOST_TRBASE, (uintptr) m->tss);
	vmcswrite(HOST_GDTR, (uintptr) m->gdt);
	vmcswrite(HOST_IDTR, IDTADDR);
	if(rdmsr(0x277, &msr) < 0) error("rdmsr(IA32_PAT) failed");
	vmcswrite(HOST_IA32_PAT, msr);
	if(rdmsr(Efer, &msr) < 0) error("rdmsr(IA32_EFER) failed");
	vmcswrite(HOST_IA32_EFER, msr);
	
	vmcswrite(EXC_BITMAP, 1<<18|1<<1);
	vmcswrite(PFAULT_MASK, 0);
	vmcswrite(PFAULT_MATCH, 0);
	
	vmcswrite(GUEST_CSBASE, 0);
	vmcswrite(GUEST_DSBASE, 0);
	vmcswrite(GUEST_ESBASE, 0);
	vmcswrite(GUEST_FSBASE, 0);
	vmcswrite(GUEST_GSBASE, 0);
	vmcswrite(GUEST_SSBASE, 0);
	vmcswrite(GUEST_CSLIMIT, -1);
	vmcswrite(GUEST_DSLIMIT, -1);
	vmcswrite(GUEST_ESLIMIT, -1);
	vmcswrite(GUEST_FSLIMIT, -1);
	vmcswrite(GUEST_GSLIMIT, -1);
	vmcswrite(GUEST_SSLIMIT, -1);
	vmcswrite(GUEST_CSPERM, (SEGG|SEGD|SEGP|SEGPL(0)|SEGEXEC|SEGR) >> 8 | 1);
	vmcswrite(GUEST_DSPERM, (SEGG|SEGB|SEGP|SEGPL(0)|SEGDATA|SEGW) >> 8 | 1);
	vmcswrite(GUEST_ESPERM, (SEGG|SEGB|SEGP|SEGPL(0)|SEGDATA|SEGW) >> 8 | 1);
	vmcswrite(GUEST_FSPERM, (SEGG|SEGB|SEGP|SEGPL(0)|SEGDATA|SEGW) >> 8 | 1);
	vmcswrite(GUEST_GSPERM, (SEGG|SEGB|SEGP|SEGPL(0)|SEGDATA|SEGW) >> 8 | 1);
	vmcswrite(GUEST_SSPERM, (SEGG|SEGB|SEGP|SEGPL(0)|SEGDATA|SEGW) >> 8 | 1);
	vmcswrite(GUEST_LDTRPERM, 1<<16);

	vmcswrite(GUEST_CR0MASK, CR0KERNEL);
	vmcswrite(GUEST_CR4MASK, CR4KERNEL);
	vmcswrite(GUEST_CR0, getcr0() & CR0KERNEL | 0x31);
	vmcswrite(GUEST_CR3, 0);
	vmcswrite(GUEST_CR4, getcr4() & CR4KERNEL);
	vmcswrite(GUEST_CR0SHADOW, getcr0() & CR0KERNEL | 0x31);
	vmcswrite(GUEST_CR4SHADOW, getcr4() & ~CR4VMXE & CR4KERNEL);
	
	vmcswrite(GUEST_IA32_PAT, 0x0007040600070406ULL);
	vmcswrite(GUEST_IA32_EFER, 0);
	
	vmcswrite(GUEST_TRBASE, 0);
	vmcswrite(GUEST_TRLIMIT, 0xffff);
	vmcswrite(GUEST_TRPERM, (SEGTSS|SEGPL(0)|SEGP) >> 8 | 2);

	vmcswrite(VM_EPTP, PADDR(vmx->pml4) | 3<<3);
	vmx->vpid = 1;
	vmcswrite(VM_VPID, vmx->vpid);
	
	vmcswrite(GUEST_RFLAGS, 2);
	
	vmx->onentry = FLUSHVPID | FLUSHEPT;
	fpinit();
	fpsave(&vmx->fp);
	
	memset(vmx->msrbits, -1, 4096);
	vmxtrapmsr(vmx, Efer, 0);
	vmcswrite(VMENTRY_MSRLDADDR, PADDR(vmx->msrguest));
	vmcswrite(VMEXIT_MSRSTADDR, PADDR(vmx->msrguest));
	vmcswrite(VMEXIT_MSRLDADDR, PADDR(vmx->msrhost));
	vmcswrite(MSR_BITMAP, PADDR(vmx->msrbits));
	
	if(sizeof(uintptr) == 8){
		vmxaddmsr(vmx, Star, 0);
		vmxaddmsr(vmx, Lstar, 0);
		vmxaddmsr(vmx, Cstar, 0);
		vmxaddmsr(vmx, Sfmask, 0);
		vmxaddmsr(vmx, KernelGSbase, 0);
		vmxtrapmsr(vmx, Star, 0);
		vmxtrapmsr(vmx, Lstar, 0);
		vmxtrapmsr(vmx, Cstar, 0);
		vmxtrapmsr(vmx, Sfmask, 0);
		vmxtrapmsr(vmx, FSbase, 0);
		vmxtrapmsr(vmx, GSbase, 0);
		vmxtrapmsr(vmx, KernelGSbase, 0);
	}
}

static void
vmxstart(Vmx *vmx)
{
	vlong msr, msr2;
	uintptr cr;
	vlong x;

	putcr4(getcr4() | 0x2000); /* set VMXE */
	putcr0(getcr0() | 0x20); /* set NE */
	cr = getcr0();
	if(rdmsr(VMX_CR0_FIXED0, &msr) < 0) error("rdmsr(VMX_CR0_FIXED0) failed");
	if(rdmsr(VMX_CR0_FIXED1, &msr2) < 0) error("rdmsr(VMX_CR0_FIXED1) failed");
	if((cr & ~msr & ~msr2 | ~cr & msr & msr2) != 0) error("invalid CR0 value");
	cr = getcr4();
	if(rdmsr(VMX_CR4_FIXED0, &msr) < 0) error("rdmsr(VMX_CR4_FIXED0) failed");
	if(rdmsr(VMX_CR4_FIXED1, &msr2) < 0) error("rdmsr(VMX_CR4_FIXED1) failed");
	if((cr & ~msr & ~msr2 | ~cr & msr & msr2) != 0) error("invalid CR4 value");
	
	rdmsr(VMX_BASIC_MSR, &x);
	qlock(vmxmach);
	if(waserror()){
		qunlock(vmxmach);
		nexterror();
	}
	if(vmxmach->vms == 0){
		memset(vmxmach->vmxon, 0, sizeof(vmxmach->vmxon));
		*(ulong*)vmxmach->vmxon = x;
		if(vmxon(PADDR(vmxmach->vmxon)) < 0)
			error("vmxon failed");
	}
	vmxmach->vms++;
	qunlock(vmxmach);
	poperror();

	memset(vmx->vmcs, 0, sizeof(vmx->vmcs));
	*(ulong*)vmx->vmcs = x;
	if(vmclear(PADDR(vmx->vmcs)) < 0)
		error("vmclear failed");
	up->vmx = vmx;
	vmxprocrestore(up);
	vmcsinit(vmx);
}

static void
cmdrelease(VmCmd *p, int f)
{
	lock(p);
	p->flags |= CMDFDONE | f;
	wakeup(p);
	unlock(p);
}

static void
killcmds(Vmx *vmx, VmCmd *notme)
{
	VmCmd *p, *pn;
	
	for(p = vmx->postponed; p != nil; p = pn){
		pn = p->next;
		p->next = nil;
		if(p == notme) continue;
		kstrcpy(p->errstr, Equit, ERRMAX);
		cmdrelease(p, CMDFFAIL);
	}
	vmx->postponed = nil;
	ilock(&vmx->cmdlock);
	for(p = vmx->firstcmd; p != nil; p = pn){
		pn = p->next;
		p->next = nil;
		if(p == notme) continue;
		kstrcpy(p->errstr, Equit, ERRMAX);
		cmdrelease(p, CMDFFAIL);
	}
	vmx->firstcmd = nil;
	vmx->lastcmd = &vmx->firstcmd;
	iunlock(&vmx->cmdlock);
}

static int
cmdquit(VmCmd *p, va_list va)
{
	Vmx *vmx;
	
	vmx = p->vmx;
	vmx->state = VMXENDING;
	killcmds(vmx, p);

	cmdclearmeminfo(p, va);
	
	up->vmx = nil;
	vmxprocrestore(up);
	vmclear(PADDR(vmx->vmcs));
	
	qlock(vmxmach);
	if(--vmxmach->vms == 0)
		vmxoff();
	qunlock(vmxmach);
	
	qlock(&vmxtablock);
	if(moribund == vmx)
		moribund = nil;
	vmxtab[vmx->index] = nil;
	qunlock(&vmxtablock);
	free(vmx);
	
	cmdrelease(p, 0);
	pexit(Equit, 1);
	return 0;
}

static void
processexit(Vmx *vmx)
{
	u32int reason;
	
	reason = vmcsread(VM_EXREASON);
	if((reason & 1<<31) == 0)
		switch(reason & 0xffff){
		case 1: /* external interrupt */
		case 3: /* INIT */
		case 4: /* SIPI */
		case 5: /* IO SMI */
		case 6: /* SMI */
		case 7: /* IRQ window */
		case 8: /* NMI window */
			return;
		}
	vmx->state = VMXREADY;
	vmx->got |= GOTEXIT;
	vmx->onentry &= ~STEP;
}

static int
cmdgetregs(VmCmd *cmd, va_list va)
{
	char *p0, *e;
	GuestReg *r;
	uvlong val;
	int s;
	char *p;
	
	p0 = va_arg(va, char *);
	e = va_arg(va, char *);
	p = p0;
	for(r = guestregs; r < guestregs + nelem(guestregs); r++)
		if(r->read != nil){
			p = seprint(p, e, "%s ", r->name);
			p = r->read(cmd->vmx, p, e);
			p = strecpy(p, e, "\n");
		}else{
			if(r->offset >= 0)
				val = vmcsread(r->offset);
			else
				val = *(uintptr*)((uchar*)cmd->vmx + ~r->offset);
			s = r->size;
			if(s == 0) s = sizeof(uintptr);
			p = seprint(p, e, "%s %#.*llux\n", r->name, s * 2, val);
		}
	return p - p0;
}

static int
setregs(Vmx *vmx, char *p0, char rs, char *fs)
{
	char *p, *q, *rp;
	char *f[10];
	GuestReg *r;
	uvlong val;
	int sz;
	int rc;

	p = p0;
	for(;;){
		q = strchr(p, rs);
		if(q == 0) break;
		*q = 0;
		rc = getfields(p, f, nelem(f), 1, fs);
		p = q + 1;
		if(rc == 0) continue;
		if(rc != 2) error("number of fields wrong");
		
		for(r = guestregs; r < guestregs + nelem(guestregs); r++)
			if(strcmp(r->name, f[0]) == 0)
				break;
		if(r == guestregs + nelem(guestregs))
			error("unknown register");
		if(r->write != nil){
			r->write(vmx, f[1]);
			continue;
		}
		val = strtoull(f[1], &rp, 0);
		sz = r->size;
		if(sz == 0) sz = sizeof(uintptr);
		if(rp == f[1] || *rp != 0) error("invalid value");
		if(r->offset >= 0)
			vmcswrite(r->offset, val);
		else{
			assert((u32int)~r->offset + sz <= sizeof(Vmx)); 
			switch(sz){
			case 1: *(u8int*)((u8int*)vmx + (u32int)~r->offset) = val; break;
			case 2: *(u16int*)((u8int*)vmx + (u32int)~r->offset) = val; break;
			case 4: *(u32int*)((u8int*)vmx + (u32int)~r->offset) = val; break;
			case 8: *(u64int*)((u8int*)vmx + (u32int)~r->offset) = val; break;
			default: error(Egreg);
			}
		}
	}
	return p - p0;
}

static int
cmdsetregs(VmCmd *cmd, va_list va)
{
	return setregs(cmd->vmx, va_arg(va, char *), '\n', " \t");
}

static int
cmdgetfpregs(VmCmd *cmd, va_list va)
{
	uchar *p;
	
	p = va_arg(va, uchar *);
	memmove(p, &cmd->vmx->fp, sizeof(FPsave));
	return sizeof(FPsave);
}

static int
cmdsetfpregs(VmCmd *cmd, va_list va)
{
	uchar *p;
	ulong n;
	vlong off;
	
	p = va_arg(va, uchar *);
	n = va_arg(va, ulong);
	off = va_arg(va, vlong);
	if(off < 0 || off >= sizeof(FPsave)) n = 0;
	else if(off + n > sizeof(FPsave)) n = sizeof(FPsave) - n;
	memmove((uchar*)&cmd->vmx->fp + off, p, n);
	return n;
}

static int
cmdgo(VmCmd *cmd, va_list va)
{
	int step;
	char *r;
	Vmx *vmx;
	
	vmx = cmd->vmx;
	if(vmx->state != VMXREADY)
		error("VM not ready");
	step = va_arg(va, int);
	r = va_arg(va, char *);
	if(r != nil) setregs(vmx, r, ';', "=");
	if(step) vmx->onentry |= STEP;
	vmx->state = VMXRUNNING;
	return 0;
}

static int
cmdstop(VmCmd *cmd, va_list)
{
	Vmx *vmx;
	
	vmx = cmd->vmx;
	if(vmx->state != VMXREADY && vmx->state != VMXRUNNING)
		error("VM not ready or running");
	vmx->state = VMXREADY;
	return 0;
}

static int
cmdstatus(VmCmd *cmd, va_list va)
{	
	kstrcpy(va_arg(va, char *), cmd->vmx->errstr, ERRMAX);
	return cmd->vmx->state;
}

static char *exitreasons[] = {
	[0] "exc", [1] "extirq", [2] "triplef", [3] "initsig", [4] "sipi", [5] "smiio", [6] "smiother", [7] "irqwin",
	[8] "nmiwin", [9] "taskswitch", [10] ".cpuid", [11] ".getsec", [12] ".hlt", [13] ".invd", [14] ".invlpg", [15] ".rdpmc",
	[16] ".rdtsc", [17] ".rsm", [18] ".vmcall", [19] ".vmclear", [20] ".vmlaunch", [21] ".vmptrld", [22] ".vmptrst", [23] ".vmread",
	[24] ".vmresume", [25] ".vmwrite", [26] ".vmxoff", [27] ".vmxon", [28] "movcr", [29] ".movdr", [30] "io", [31] ".rdmsr",
	[32] ".wrmsr", [33] "entrystate", [34] "entrymsr", [36] ".mwait", [37] "monitortrap", [39] ".monitor",
	[40] ".pause", [41] "mcheck", [43] "tpr", [44] "apicacc", [45] "eoi", [46] "gdtr_idtr", [47] "ldtr_tr",
	[48] "eptfault", [49] "eptinval", [50] ".invept", [51] ".rdtscp", [52] "preempt", [53] ".invvpid", [54] ".wbinvd", [55] ".xsetbv",
	[56] "apicwrite", [57] ".rdrand", [58] ".invpcid", [59] ".vmfunc", [60] ".encls", [61] ".rdseed", [62] "pmlfull", [63] ".xsaves",
	[64] ".xrstors", 
};

static char *except[] = {
	[0] "#de", [1] "#db", [3] "#bp", [4] "#of", [5] "#br", [6] "#ud", [7] "#nm",
	[8] "#df", [10] "#ts", [11] "#np", [12] "#ss", [13] "#gp", [14] "#pf",
	[16] "#mf", [17] "#ac", [18] "#mc", [19] "#xm", [20] "#ve",
};

static int
cmdwait(VmCmd *cp, va_list va)
{
	char *p, *p0, *e;
	u32int reason, intr;
	uvlong qual;
	u16int rno;
	Vmx *vmx;

	if(cp->scratched)
		error(Eintr);
	vmx = cp->vmx;
	p0 = p = va_arg(va, char *);
	e = va_arg(va, char *);
	if((vmx->got & GOTIRQACK) != 0){
		p = seprint(p, e, "*ack %d\n", vmx->irqack.info & 0xff);
		vmx->got &= ~GOTIRQACK;
		return p - p0;
	}
	if((vmx->got & GOTEXIT) == 0){
		cp->flags |= CMDFPOSTP;
		return -1;
	}
	vmx->got &= ~GOTEXIT;
	reason = vmcsread(VM_EXREASON);
	qual = vmcsread(VM_EXQUALIF);
	rno = reason;
	intr = vmcsread(VM_EXINTRINFO);
	if((reason & 1<<31) != 0)
		p = seprint(p, e, "!");
	if(rno == 0 && (intr & 1<<31) != 0){
		if((intr & 0xff) >= nelem(except) || except[intr & 0xff] == nil)
			p = seprint(p, e, "#%d ", intr & 0xff);
		else
			p = seprint(p, e, "%s ", except[intr & 0xff]);
	}else if(rno >= nelem(exitreasons) || exitreasons[rno] == nil)
		p = seprint(p, e, "?%d ", rno);
	else
		p = seprint(p, e, "%s ", exitreasons[rno]);
	p = seprint(p, e, "%#ullx pc %#ullx sp %#ullx ilen %#ullx iinfo %#ullx", qual, vmcsread(GUEST_RIP), vmcsread(GUEST_RSP), vmcsread(VM_EXINSTRLEN), vmcsread(VM_EXINSTRINFO));
	if((intr & 1<<11) != 0) p = seprint(p, e, " excode %#ullx", vmcsread(VM_EXINTRCODE));
	if(rno == 48 && (qual & 0x80) != 0) p = seprint(p, e, " va %#ullx", vmcsread(VM_GUESTVA));
	if(rno == 48 || rno == 49) p = seprint(p, e, " pa %#ullx", vmcsread(VM_GUESTPA));
	if(rno == 30) p = seprint(p, e, " ax %#ullx", (uvlong)vmx->ureg.ax);
	p = seprint(p, e, "\n");
	return p - p0;
}

static void
eventparse(char *p, VmIntr *vi)
{
	char *q, *r;
	int i;
	
	memset(vi, 0, sizeof(VmIntr));
	q = nil;
	kstrdup(&q, p);
	if(waserror()){
		free(q);
		memset(vi, 0, sizeof(VmIntr));
		nexterror();
	}
	vi->info = 1<<31;
	r = strchr(q, ',');
	if(r != nil) *r++ = 0;
	for(i = 0; i < nelem(except); i++)
		if(except[i] != nil && strcmp(except[i], q) == 0)
			break;
	if(*q == '#'){
		q++;
		vi->info |= 3 << 8;
	}
	if(i == nelem(except)){
		i = strtoul(q, &q, 10);
		if(*q != 0 || i > 255) error(Ebadctl);
	}
	vi->info |= i;
	if((vi->info & 0x7ff) == 3 || (vi->info & 0x7ff) == 4)
		vi->info += 3 << 8;
	if(r == nil) goto out;
	if(*r != ','){
		vi->code = strtoul(r, &r, 0);
		vi->info |= 1<<11;
	}else r++;
	if(*r == ',')
		vi->ilen = strtoul(r + 1, &r, 0);
	if(*r != 0) error(Ebadctl);
out:
	poperror();
	free(q);
}

static int
cmdexcept(VmCmd *cp, va_list va)
{
	Vmx *vmx;
	
	vmx = cp->vmx;
	if(cp->scratched) error(Eintr);
	if((vmx->onentry & POSTEX) != 0){
		cp->flags |= CMDFPOSTP;
		return 0;
	}
	eventparse(va_arg(va, char *), &vmx->exc);
	vmx->onentry |= POSTEX;
	return 0;
}

static int
cmdirq(VmCmd *cmd, va_list va)
{
	char *p;
	VmIntr vi;
	Vmx *vmx;
	
	vmx = cmd->vmx;
	p = va_arg(va, char *);
	if(p == nil)
		vmx->onentry &= ~POSTIRQ;
	else{
		eventparse(p, &vi);
		vmx->irq = vi;
		vmx->onentry |= POSTIRQ;
	}
	return 0;
}

static int
cmdextrap(VmCmd *, va_list va)
{
	char *p, *q;
	u32int v;
	
	p = va_arg(va, char *);
	v = strtoul(p, &q, 0);
	if(q == p || *q != 0) error(Ebadarg);
	vmcswrite(EXC_BITMAP, v);
	return 0;
}

static int
gotcmd(void *vmxp)
{
	int rc;
	Vmx *vmx;

	vmx = vmxp;
	ilock(&vmx->cmdlock);
	rc = vmx->firstcmd != nil;
	iunlock(&vmx->cmdlock);
	return rc;
}

static void
markcmddone(VmCmd *p, VmCmd ***pp)
{
	if((p->flags & (CMDFFAIL|CMDFPOSTP)) == CMDFPOSTP){
		**pp = p;
		*pp = &p->next;
	}else{
		p->flags = p->flags & ~CMDFPOSTP;
		cmdrelease(p, 0);
	}
}

static VmCmd **
markppcmddone(VmCmd **pp)
{
	VmCmd *p;
	
	p = *pp;
	if((p->flags & (CMDFFAIL|CMDFPOSTP)) == CMDFPOSTP)
		return &p->next;
	*pp = p->next;
	p->next = nil;
	p->flags = p->flags & ~CMDFPOSTP;
	cmdrelease(p, 0);
	return pp;
}


static void
runcmd(Vmx *vmx)
{
	VmCmd *p, **pp;
	
	for(pp = &vmx->postponed; p = *pp, p != nil; ){
		if(waserror()){
			kstrcpy(p->errstr, up->errstr, ERRMAX);
			p->flags |= CMDFFAIL;
			pp = markppcmddone(pp);
			continue;
		}
		p->flags &= ~CMDFPOSTP;
		p->retval = p->cmd(p, p->va);
		poperror();
		pp = markppcmddone(pp);
	}
	for(;;){
		ilock(&vmx->cmdlock);
		p = vmx->firstcmd;
		if(p == nil){
			iunlock(&vmx->cmdlock);
			break;
		}
		vmx->firstcmd = p->next;
		if(vmx->lastcmd == &p->next)
			vmx->lastcmd = &vmx->firstcmd;
		iunlock(&vmx->cmdlock);
		p->next = nil;
		if(waserror()){
			kstrcpy(p->errstr, up->errstr, ERRMAX);
			p->flags |= CMDFFAIL;
			markcmddone(p, &pp);
			continue;
		}
		if(p->scratched) error(Eintr);
		p->retval = p->cmd(p, p->va);
		poperror();
		markcmddone(p, &pp);
	}
}

static void
vmxproc(void *vmxp)
{
	int init, rc, x;
	u32int procbctls, defprocbctls;
	vlong v;
	Vmx *vmx;

	vmx = vmxp;
	procwired(up, vmx->machno);
	sched();
	init = 0;
	defprocbctls = 0;
	while(waserror()){
		kstrcpy(vmx->errstr, up->errstr, ERRMAX);
		vmx->state = VMXDEAD;
	}
	for(;;){
		if(!init){
			init = 1;
			vmxstart(vmx);
			vmx->state = VMXREADY;
			defprocbctls = vmcsread(PROCB_CTLS);
		}
		runcmd(vmx);
		if(vmx->state == VMXRUNNING){
			procbctls = defprocbctls;
			if((vmx->onentry & STEP) != 0)
				defprocbctls |= PROCB_MONTRAP;
			if((vmx->onentry & POSTEX) != 0){
				vmcswrite(VMENTRY_INTRINFO, vmx->exc.info);
				vmcswrite(VMENTRY_INTRCODE, vmx->exc.code);
				vmcswrite(VMENTRY_INTRILEN, vmx->exc.ilen);
				vmx->onentry &= ~POSTEX;
			}
			if((vmx->onentry & POSTIRQ) != 0 && (vmx->onentry & STEP) == 0){
				if((vmx->onentry & POSTEX) == 0 && (vmcsread(GUEST_RFLAGS) & 1<<9) != 0 && (vmcsread(GUEST_CANINTR) & 3) == 0){
					vmcswrite(VMENTRY_INTRINFO, vmx->irq.info);
					vmcswrite(VMENTRY_INTRCODE, vmx->irq.code);
					vmcswrite(VMENTRY_INTRILEN, vmx->irq.ilen);
					vmx->onentry &= ~POSTIRQ;
					vmx->got |= GOTIRQACK;
					vmx->irqack = vmx->irq;
				}else
					procbctls |= PROCB_IRQWIN;
			}
			if((vmx->onentry & FLUSHVPID) != 0){
				if(invvpid(INVLOCAL, vmx->vpid, 0) < 0)
					error("invvpid failed");
				vmx->onentry &= ~FLUSHVPID;
			}
			if((vmx->onentry & FLUSHEPT) != 0){
				if(invept(INVLOCAL, PADDR(vmx->pml4) | 3<<3, 0) < 0)
					error("invept failed");
				vmx->onentry &= ~FLUSHEPT;
			}
			vmcswrite(PROCB_CTLS, procbctls);
			vmx->got &= ~GOTEXIT;
			
			x = splhi();
			if(sizeof(uintptr) == 8){
				rdmsr(FSbase, &v);
				vmwrite(HOST_FSBASE, v);
			}
			if((vmx->dr[7] & ~0xd400) != 0)
				putdr01236(vmx->dr);
			fpsserestore(&vmx->fp);
			putcr2(vmx->cr2);
			rc = vmlaunch(&vmx->ureg, vmx->launched);
			vmx->cr2 = getcr2();
			fpssesave(&vmx->fp);
			splx(x);
			if(rc < 0)
				error("vmlaunch failed");
			vmx->launched = 1;
			processexit(vmx);
		}else{
			up->psstate = "Idle";
			sleep(&vmx->cmdwait, gotcmd, vmx);
			up->psstate = nil;
		}
	}
}

enum {
	/* Qdir */
	Qclone = 1,
};

enum {
	Qdir,
	Qctl,
	Qregs,
	Qstatus,
	Qmap,
	Qwait,
	Qfpregs,
};

static Dirtab vmxdir[] = {
	"ctl",		{ Qctl, 0, 0 },		0,		0660,
	"regs",		{ Qregs, 0, 0 },	0,		0660,
	"status",	{ Qstatus, 0, 0 },	0,		0440,
	"map",		{ Qmap, 0, 0 },		0,		0660,
	"wait",		{ Qwait, 0, 0 },	0,		0440,
	"fpregs",	{ Qfpregs, 0, 0 },	0,		0660,
};

enum {
	CMquit,
	CMgo,
	CMstop,
	CMstep,
	CMexc,
	CMirq,
	CMextrap,
};

static Cmdtab vmxctlmsg[] = {
	CMquit,		"quit",		1,
	CMgo,		"go",		0,
	CMstop,		"stop",		1,
	CMstep,		"step",		0,
	CMexc,		"exc",		2,
	CMirq,		"irq",		0,
	CMextrap,	"extrap",	2,
};

enum { AUXSIZE = 4096 };

static Vmx *
vmxlook(vlong n)
{
	if(n < 0) return nil;
	if(n >= nvmxtab) return nil;
	return vmxtab[n];
}
#define QIDPATH(q,e) ((q) + 1 << 8 | (e)) 
#define SLOT(q) ((vlong)((q).path >> 8) - 1)
#define FILE(q) ((int)(q).path & 0xff)
static Vmx *
vmxent(Qid q)
{
	Vmx *vmx;

	eqlock(&vmxtablock);
	if(waserror()){
		qunlock(&vmxtablock);
		nexterror();
	}
	vmx = vmxlook(SLOT(q));
	qunlock(&vmxtablock);
	poperror();
	return vmx;
}

static int
iscmddone(void *cp)
{
	return (((VmCmd*)cp)->flags & CMDFDONE) != 0;
}

static int
vmxcmd(Vmx *vmx, int (*f)(VmCmd *, va_list), ...)
{
	VmCmd cmd;

	if(vmx->state == VMXENDING)
	ending:
		error(Equit);
	memset(&cmd, 0, sizeof(VmCmd));
	cmd.vmx = vmx;
	cmd.errstr = up->errstr;
	cmd.cmd = f;
	va_start(cmd.va, f);
	 
	ilock(&vmx->cmdlock);
	if(vmx->state == VMXENDING){
		iunlock(&vmx->cmdlock);
		goto ending;
	}
	*vmx->lastcmd = &cmd;
	vmx->lastcmd = &cmd.next;
	iunlock(&vmx->cmdlock);
	
	while(waserror())
		cmd.scratched = 1;
	wakeup(&vmx->cmdwait);
	do
		sleep(&cmd, iscmddone, &cmd);
	while(!iscmddone(&cmd));
	poperror();
	lock(&cmd);
	unlock(&cmd);
	if((cmd.flags & CMDFFAIL) != 0)
		error(up->errstr);
	return cmd.retval;
}

static Vmx *
vmxnew(void)
{
	Vmx *vmx;
	Vmx **newtab;
	int i, mi, mv;
	
	vmx = mallocalign(sizeof(Vmx), 4096, 0, 0);
	if(waserror()){

		free(vmx);
		nexterror();
	}
	vmx->state = VMXINIT;
	vmx->lastcmd = &vmx->firstcmd;
	vmx->mem.next = &vmx->mem;
	vmx->mem.prev = &vmx->mem;
	vmx->index = -1;
	
	eqlock(&vmxtablock);
	if(waserror()){
		if(vmx->index >= 0)
			vmxtab[vmx->index] = 0;
		qunlock(&vmxtablock);
		nexterror();
	}
	for(i = 0; i < nvmxtab; i++)
		if(vmxtab[i] == nil){
			vmxtab[i] = vmx;
			vmx->index = i;
			break;
		}
	if(i == nvmxtab){
		newtab = realloc(vmxtab, (nvmxtab + 1) * sizeof(Vmx *));
		if(newtab == nil)
			error(Enomem);
		vmxtab = newtab;
		vmxtab[nvmxtab] = vmx;
		vmx->index = nvmxtab++;
	}
	kproc("kvmx", vmxproc, vmx);
	qunlock(&vmxtablock);
	poperror();
	poperror();
	mi = 0;
	mv = 0x7fffffff;
	for(i = 0; i < conf.nmach; i++)
		if(vmxmachp(i)->vms < mv){
			mi = i;
			mv = vmxmachp(i)->vms;
		}
	vmx->machno = mi;
	if(vmxcmd(vmx, cmdstatus, up->errstr) == VMXDEAD)
		error(up->errstr);
	return vmx;
}

void
vmxshutdown(void)
{
	int i;
	
	for(i = 0; i < nvmxtab; i++)
		if(vmxtab[i] != nil)
			vmxcmd(vmxtab[i], cmdquit);
}

static Chan *
vmxattach(char *spec)
{
	if(!gotvmx) error(Enodev);
	return devattach('X', spec);
}

static int
vmxgen(Chan *c, char *, Dirtab *, int, int s, Dir *dp)
{
	Dirtab *tab;
	int path;

	if(s == DEVDOTDOT){
		devdir(c, (Qid){Qdir, 0, QTDIR}, "#X", 0, eve, 0555, dp);
		return 1;
	}
	if(c->qid.path == Qdir){
		if(s-- == 0) goto clone;
		if(s >= nvmxtab)
			return -1;
		if(vmxlook(s) == nil)
			return 0;
		sprint(up->genbuf, "%d", s);
		devdir(c, (Qid){QIDPATH(s, Qdir), 0, QTDIR}, up->genbuf, 0, eve, DMDIR|0555, dp);
		return 1;
	}
	if(c->qid.path == Qclone){
	clone:
		strcpy(up->genbuf, "clone");
		devdir(c, (Qid){Qclone, 0, QTFILE}, up->genbuf, 0, eve, 0444, dp);
		return 1;
	}
	if(s >= nelem(vmxdir))
		return -1;
	tab = &vmxdir[s];
	path = QIDPATH(SLOT(c->qid), 0);
	devdir(c, (Qid){tab->qid.path|path, tab->qid.vers, tab->qid.type}, tab->name, tab->length, eve, tab->perm, dp);
	return 1;
}

static Walkqid*
vmxwalk(Chan *c, Chan *nc, char **name, int nname)
{
	Walkqid *rc;

	eqlock(&vmxtablock);
	if(waserror()){
		qunlock(&vmxtablock);
		nexterror();
	}
	rc = devwalk(c, nc, name, nname, nil, 0, vmxgen);
	qunlock(&vmxtablock);
	poperror();
	return rc;
}

static int
vmxstat(Chan *c, uchar *dp, int n)
{
	int rc;
	
	eqlock(&vmxtablock);
	if(waserror()){
		qunlock(&vmxtablock);
		nexterror();
	}
	rc = devstat(c, dp, n, nil, 0, vmxgen);
	qunlock(&vmxtablock);
	poperror();
	return rc;
}

static Chan*
vmxopen(Chan* c, int omode)
{
	Chan *ch;
	Vmx *vmx;

	if(c->qid.path == Qclone){
		if(!iseve()) error(Eperm);
		vmx = vmxnew();
		c->qid.path = QIDPATH(vmx->index, Qctl);
	}
	eqlock(&vmxtablock);
	if(waserror()){
		qunlock(&vmxtablock);
		nexterror();
	}
	vmx = vmxlook(SLOT(c->qid));
	if(SLOT(c->qid) >= 0 && vmx == nil) error(Enonexist);
	if(FILE(c->qid) != Qdir && !iseve()) error(Eperm);
	ch = devopen(c, omode, nil, 0, vmxgen);
	qunlock(&vmxtablock);
	poperror();
	ch->aux = smalloc(AUXSIZE);
	if(SLOT(ch->qid) >= 0 && FILE(ch->qid) == Qmap){
		if((omode & OTRUNC) != 0)
			vmxcmd(vmx, cmdclearmeminfo);
	}
	return ch;
}

static void
vmxclunk(Chan *ch)
{
	free(ch->aux);
	ch->aux = nil;
}

static void
vmxremove(Chan *ch)
{
	Vmx *vmx, *old;

	vmxclunk(ch);
	if(SLOT(ch->qid) == -1 || FILE(ch->qid) != Qctl)
		error(Eperm);
	vmx = vmxent(ch->qid);
	if(vmx == nil)
		error(Enonexist);
	qlock(&vmxtablock);
	old = moribund;
	moribund = vmx;
	qunlock(&vmxtablock);
	if(old != nil)
		vmxcmd(old, cmdquit);
}

static void
vmxclose(Chan *ch)
{
	if((ch->flag & CRCLOSE) != 0)
		vmxremove(ch);
	else
		vmxclunk(ch);
}


static long
vmxread(Chan* c, void* a, long n, vlong off)
{
	int rc;
	Vmx *vmx;

	if(SLOT(c->qid) == -1){
		switch((int)c->qid.path){
		case Qdir:
		dirread:
			eqlock(&vmxtablock);
			if(waserror()){
				qunlock(&vmxtablock);
				nexterror();
			}
			rc = devdirread(c, a, n, nil, 0, vmxgen);
			qunlock(&vmxtablock);
			poperror();
			return rc;
		default:
			error(Egreg);
		}
	}
	vmx = vmxent(c->qid);
	if(vmx == nil) error(Enonexist);
	switch(FILE(c->qid)){
	case Qdir:
		goto dirread;
	case Qctl:
		{
			char buf[20];
			
			sprint(buf, "%d", vmx->index);
			return readstr(off, a, n, buf);
		}
	case Qregs:
		if(off == 0)
			vmxcmd(vmx, cmdgetregs, c->aux, (char *) c->aux + AUXSIZE);
		return readstr(off, a, n, c->aux);
	case Qmap:
		if(off == 0)
			vmxcmd(vmx, cmdgetmeminfo, c->aux, (char *) c->aux + AUXSIZE);
		return readstr(off, a, n, c->aux);
	case Qstatus:
		{
			char buf[ERRMAX+128];
			char errbuf[ERRMAX];
			int status;
			
			status = vmx->state;
			if(status == VMXDEAD){
				vmxcmd(vmx, cmdstatus, errbuf);
				snprint(buf, sizeof(buf), "%s %#q\n", statenames[status], errbuf);
			}else if(status >= 0 && status < nelem(statenames))
				snprint(buf, sizeof(buf), "%s\n", statenames[status]);
			else
				snprint(buf, sizeof(buf), "%d\n", status);
			return readstr(off, a, n, buf);
		}
	case Qwait:
		{
			char buf[512];
			
			rc = vmxcmd(vmx, cmdwait, buf, buf + sizeof(buf));
			if(rc > n) rc = n;
			if(rc > 0) memmove(a, buf, rc);
			return rc;
		}
	case Qfpregs:
		{
			char buf[sizeof(FPsave)];
			
			vmxcmd(vmx, cmdgetfpregs, buf);
			if(n < 0 || off < 0 || off >= sizeof(buf)) n = 0;
			else if(off + n > sizeof(buf)) n = sizeof(buf) - off;
			if(n != 0) memmove(a, buf + off, n);
			return n;
		}
	default:
		error(Egreg);
		break;
	}
	return 0;
}

static long
vmxwrite(Chan* c, void* a, long n, vlong off)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	char *s;
	int rc;
	Vmx *vmx;

	if(SLOT(c->qid) == -1){
		switch((int)c->qid.path){
		case Qdir:
			error(Eperm);
		default:
			error(Egreg);
		}
	}
	vmx = vmxent(c->qid);
	if(vmx == nil) error(Enonexist);
	switch(FILE(c->qid)){
	case Qdir:
		error(Eperm);
	case Qctl:
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, vmxctlmsg, nelem(vmxctlmsg));
		switch(ct->index){
		case CMquit:
			vmxcmd(vmx, cmdquit);
			break;
		case CMgo:
		case CMstep:
			s = nil;
			if(cb->nf == 2) kstrdup(&s, cb->f[1]);
			else if(cb->nf != 1) error(Ebadarg);
			if(waserror()){
				free(s);
				nexterror();
			}
			vmxcmd(vmx, cmdgo, ct->index == CMstep, s);
			poperror();
			free(s);
			break;
		case CMstop:
			vmxcmd(vmx, cmdstop);
			break;
		case CMexc:
			s = nil;
			kstrdup(&s, cb->f[1]);
			if(waserror()){
				free(s);
				nexterror();
			}
			vmxcmd(vmx, cmdexcept, s);
			poperror();
			free(s);
			break;
		case CMirq:
			s = nil;
			if(cb->nf == 2)
				kstrdup(&s, cb->f[1]);
			if(waserror()){
				free(s);
				nexterror();
			}
			vmxcmd(vmx, cmdirq, s);
			poperror();
			free(s);
			break;
		case CMextrap:
			s = nil;
			kstrdup(&s, cb->f[1]);
			if(waserror()){
				free(s);
				nexterror();
			}
			vmxcmd(vmx, cmdextrap, s);
			poperror();
			free(s);
			break;

		default:
			error(Egreg);
		}
		poperror();
		free(cb);
		break;
	case Qmap:
	case Qregs:
		s = malloc(n+1);
		if(s == nil) error(Enomem);
		if(waserror()){
			free(s);
			nexterror();
		}
		memmove(s, a, n);
		s[n] = 0;
		rc = vmxcmd(vmx, FILE(c->qid) == Qregs ? cmdsetregs : cmdsetmeminfo, s);
		poperror();
		free(s);
		return rc;
	case Qfpregs:
		{
			char buf[sizeof(FPsave)];
			
			if(n > sizeof(FPsave)) n = sizeof(FPsave);
			memmove(buf, a, n);
			return vmxcmd(vmx, cmdsetfpregs, buf, n, off);
		}
	default:
		error(Egreg);
		break;
	}
	return n;
}

Dev vmxdevtab = {
	'X',
	"vmx",
	
	vmxreset,
	devinit,
	devshutdown,
	vmxattach,
	vmxwalk,
	vmxstat,
	vmxopen,
	devcreate,
	vmxclose,
	vmxread,
	devbread,
	vmxwrite,
	devbwrite,
	vmxremove,
	devwstat,
};
