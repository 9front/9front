#include <u.h>
#include <libc.h>
#include <bio.h>

Biobuf *out;

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
	VMX_VPIDEPT_MSR = 0x48C,
	VMX_TRUE_PINB_CTLS_MSR = 0x48D,
	VMX_TRUE_PROCB_CTLS_MSR = 0x48E,
	VMX_TRUE_EXIT_CTLS_MSR = 0x48F,
	VMX_TRUE_ENTRY_CTLS_MSR = 0x490,
	VMX_VMFUNC_MSR = 0x491,

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
	PROCB_EXITMONITOR = 1<<29,
	PROCB_EXITPAUSE = 1<<30,
	PROCB_USECTLS2 = 1<<31,
	
	PROCB_EPT = 1<<1,
	PROCB_EXITGDT = 1<<2,
	PROCB_VPID = 1<<5,
	PROCB_UNRESTR = 1<<7,
};

int
cpuidcheck(void)
{
	int pfd[2];
	Biobuf *bp;
	char *l, *f[64];
	int i, rc;
	
	pipe(pfd);
	switch(rfork(RFPROC|RFFDG)){
	case 0:
		close(pfd[1]);
		close(0);
		open("/dev/null", OREAD);
		dup(pfd[0], 1);
		execl("/bin/aux/cpuid", "cpuid", nil);
		sysfatal("execl: %r");
		break;
	case -1: sysfatal("rfork: %r");
	}
	close(pfd[0]);
	bp = Bfdopen(pfd[1], OREAD);
	if(bp == nil) sysfatal("Bfdopen: %r");
	for(; l = Brdstr(bp, '\n', 10), l != nil; free(l)){
		rc = tokenize(l, f, nelem(f));
		if(rc < 1) continue;
		if(strcmp(f[0], "features") != 0) continue;
		for(i = 1; i < rc; i++)
			if(strcmp(f[i], "vmx") == 0){
				Bterm(bp);
				close(pfd[1]);
				waitpid();
				return 0;
			}
	}
	Bterm(bp);
	close(pfd[1]);
	waitpid();
	return -1;
}

static int msrfd;

u64int
rdmsr(u32int addr)
{
	u64int rv;

	if(pread(msrfd, &rv, 8, addr) < 0) sysfatal("pread: %r");
	return rv;
}

void
wrmsr(u32int addr, u64int val)
{
	if(pwrite(msrfd, &val, 8, addr) < 0) sysfatal("pwrite: %r");
}

static char *pinbits[64] = {
	[0] "extirq", [3] "nmiexit", [5] "virtnmi", [6] "preempt", [7] "procpostirq"
};

static char *procbits[64] = {
	[2] "irqwin", [3] "tscoffset", [7] "hltexit", [9] "invlpgexit", [10] "mwaitexit",
	[11] "rdpmcexit", [12] "rdtscexit", [15] "cr3ldexit", [16] "cr3stexit", [19] "cr8ldexit",
	[20] "cr8stexit", [21] "tprshadow", [22] "nmiwin", [23] "movdrexit", [24] "ioexit",
	[25] "iobitmap", [27] "mtf", [28] "msrbitmap", [29] "monitorexit", [30] "pauseexit",
};

static char *proc2bits[64] = {
	[0] "virtapic", [1] "ept", [2] "gdtexit", [3] "rdtscp", [4] "virtx2apic", [5] "vpid",
	[6] "wbinvdexit", [7] "unrestr", [8] "apicregs", [9] "virtirq", [10] "pauseloopexit",
	[11] "rdrandexit", [12] "invpcid", [13] "vmfunc", [14] "vmcsshadow", [15] "enclsexit",
	[16] "rdseedexit", [17] "pml", [18] "#ve", [19] "conceal", [20] "xsave", [22] "eptxmode",
	[25] "tscscale",
};

static char *exitbits[64] = {
	[2] "savedebug", [9] "host64", [12] "saveperfglobal", [15] "ackextirq",
	[16] "savepat", [17] "loadpat", [20] "saveefer", [21] "loadefer", [22] "savepreempt",
	[23] "savebndcfgs", [24] "concealexits",
};

static char *entrybits[64] = {
	[2] "loaddebug", [9] "guest64", [10] "entrysmm", [11] "dualmonitor", [13] "loadperfglobal",
	[14] "loadpat", [15] "loadefer", [16] "loadbndcfgs", [17] "concealentries",
};

static char *miscbits[64] = {
	[5] "longmodeswitch", [6] "hlt", [7] "shutdown", [8] "ipi", [14] "pt", [15] "rdmsrsmm",
	[28] "smmblock", [29] "vmwriteany", [30] "zerolenswirq",
};

static char *cr0bits[64] = {
	[0] "pe", [1] "mp", [2] "em", [3] "ts", [4] "et", [5] "ne", [6] "wp", [18] "am",
	[29] "nw", [30] "cd", [31] "pg",
};

static char *cr4bits[64] = {
	[0] "vme", [1] "pvi", [2] "tsd", [3] "de", [4] "pse", [5] "pae", [6] "mce", [7] "pge",
	[8] "pce", [9] "osxfsr", [10] "osxmmxcpt", [11] "umip", [13] "vmxe", [14] "smxe",
	[16] "fsgsbase", [17] "pcide", [18] "osxsave", [20] "smep", [21] "smap", [22] "pke"
};

static char *eptbits[64] = {
	[0] "xonly", [6] "pwl4", [8] "ucmem", [14] "wbmem", [16] "2MBpage", [17] "1GBpage",
	[20] "invept", [21] "dirtybits", [22] "violexitinfo", [25] "invept.single", [26] "invept.all",
};

static char *vpidbits[64] = {
	[32] "invvpid", [40] "invvpid.addr", [41] "invvpid.single", [42] "invvpid.all", [43] "invvpid.noglob",
};

void
printbits(char *id, uvlong allowed, uvlong forced, char **s)
{
	int i, l;

	l = -1;
	for(i = 0; i < 64; i++){
		if(s[i] == nil) continue;
		if(((allowed|forced) & 1ULL<<i) == 0) continue;
		if((uint)l > 80){
			if(l >= 0)
				Bprint(out, "\n");
			l = Bprint(out, "%s ", id);
		}
		if((forced & 1ULL<<i) != 0) l += Bprint(out, "!");
		l += Bprint(out, "%s ", s[i]);
	}
	if(l >= 0)
		Bprint(out, "\n");
}

void
printbits32(char *id, uvlong w, char **s)
{
	printbits(id, (u32int)(w >> 32), (u32int)w, s);
}

u64int
rawprint(char *s, int addr)
{
	u64int msr;
	
	msr = rdmsr(addr);
	Bprint(out, "%s %#.16ullx\n", s, msr);
	return msr;
}

void
main(int argc, char **argv)
{
	u64int msr, msr2;
	int ext;
	static int no, raw, verbose;
	
	ARGBEGIN {
	case 'r': raw++; verbose++; break;
	case 'v': verbose++; break;
	default: goto usage;
	} ARGEND;
	if(argc != 0){
	usage:
		fprint(2, "usage: %s [-rv]\n", argv0);
		exits("usage");
	}
	
	out = Bfdopen(1, OWRITE);
	if(out == nil) sysfatal("Bfdopen: %r");
	msrfd = open("#P/msr", OREAD);
	if(msrfd < 0) sysfatal("open: %r");
	if(cpuidcheck() < 0) sysfatal("CPU does not support VMX");
	if(!verbose){
		msr = rdmsr(0x3a);
		if((msr & 5) == 0) wrmsr(0x3a, msr | 5);
		if((rdmsr(0x3a) & 5) != 5){
			print("VMX disabled by BIOS\n");
			no++;
		}
		msr = rdmsr(VMX_PROCB_CTLS_MSR);
		if((vlong)msr >= 0){
			print("no secondary controls\n");
			no++;
		}else{
			msr = rdmsr(VMX_PROCB_CTLS2_MSR);
			if((msr >> 32 & PROCB_EPT) == 0){
				print("no EPT support\n");
				no++;
			}
			if((msr >> 32 & PROCB_VPID) == 0){
				print("no VPID support\n");
				no++;
			}
		}
		if(no == 0)
			print("VMX is supported\n");
		else
			print("Some needed features are missing\n");
	}else if(!raw){
		msr = rdmsr(VMX_BASIC_MSR);
		Bprint(out, "vmcsrev %#ux\n", (u32int)msr & 0x7fffffff);
		Bprint(out, "vmxonsz %d\n", (u32int)(msr >> 32) & 0x1fff);
		Bprint(out, "vmcsmem %d\n", (u32int)(msr >> 50) & 15);
		ext = (u32int)(msr >> 55) & 1;
		Bprint(out, "extcontrols %d\n", ext);
		
		msr = rdmsr(ext ? VMX_TRUE_PINB_CTLS_MSR : VMX_PINB_CTLS_MSR);
		printbits32("pin", msr, pinbits);
		msr = rdmsr(ext ? VMX_TRUE_PROCB_CTLS_MSR : VMX_PROCB_CTLS_MSR);
		printbits32("proc", msr, procbits);
		if((msr & 1ULL<<63) != 0){
			msr = rdmsr(VMX_PROCB_CTLS2_MSR);
			printbits32("proc2", msr, proc2bits);
		}
		msr = rdmsr(ext ? VMX_TRUE_ENTRY_CTLS_MSR : VMX_VMENTRY_CTLS_MSR);
		printbits32("entry", msr, entrybits);
		msr = rdmsr(ext ? VMX_TRUE_EXIT_CTLS_MSR : VMX_VMEXIT_CTLS_MSR);
		printbits32("exit", msr, exitbits);
		msr = rdmsr(VMX_MISC_MSR);
		Bprint(out, "misc preemptdiv:%d cr3targ:%d maxmsr:%d mseg:%#ux\n", (int)msr & 0x1f, (int)msr >> 16 & 0x1ff, (int)msr >> 25 & 7, (int)(msr >> 32));
		printbits("misc", msr, 0, miscbits);
		msr = rdmsr(VMX_CR0_FIXED0);
		msr2 = rdmsr(VMX_CR0_FIXED1);
		printbits("cr0fixed", msr & msr2, ~msr & ~msr2, cr0bits);
		msr = rdmsr(VMX_CR4_FIXED0);
		msr2 = rdmsr(VMX_CR4_FIXED1);
		printbits("cr4fixed", msr & msr2, ~msr & ~msr2, cr4bits);
		Bprint(out, "vmcsenum %#ullx\n", rdmsr(VMX_VMCS_ENUM));
		if(no == 0){
			msr = rdmsr(VMX_VPIDEPT_MSR);
			printbits("ept", msr, 0, eptbits);
			printbits("vpid", msr, 0, vpidbits);
		}
	}else{
		msr = rawprint("basic", VMX_BASIC_MSR);
		ext = (u32int)(msr >> 55) & 1;
		rawprint("pin", ext ? VMX_TRUE_PINB_CTLS_MSR : VMX_PINB_CTLS_MSR);
		msr = rawprint("proc", ext ? VMX_TRUE_PROCB_CTLS_MSR : VMX_PROCB_CTLS_MSR);
		if((msr & 1ULL<<63) != 0)
			rawprint("proc2", VMX_PROCB_CTLS2_MSR);
		rawprint("entry", ext ? VMX_TRUE_ENTRY_CTLS_MSR : VMX_VMENTRY_CTLS_MSR);
		rawprint("exit", ext ? VMX_TRUE_EXIT_CTLS_MSR : VMX_VMEXIT_CTLS_MSR);
		rawprint("misc", VMX_MISC_MSR);
		rawprint("cr0fixed0", VMX_CR0_FIXED0);
		rawprint("cr0fixed1", VMX_CR0_FIXED1);
		rawprint("cr4fixed0", VMX_CR4_FIXED0);
		rawprint("cr4fixed1", VMX_CR4_FIXED1);
		rawprint("vmcsenum", VMX_VMCS_ENUM);
		if(no == 0)
			rawprint("vpidept", VMX_VPIDEPT_MSR);
	}

	exits(nil);
}
