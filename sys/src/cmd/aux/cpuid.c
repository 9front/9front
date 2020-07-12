#include <u.h>
#include <libc.h>
#include <bio.h>

#define CUT(x, a, b) (((x)&((1<<((b)+1))-1))>>(a))

typedef struct Res {
	ulong ax, bx, cx, dx;
} Res;

Biobuf *out;

uchar _cpuid[] = {
	0x5E,			/* POP SI (PC) */
	0x5D,			/* POP BP (Res&) */
	0x58,			/* POP AX */
	0x59,			/* POP CX */

	0x51,			/* PUSH CX */
	0x50,			/* PUSH AX */
	0x55,			/* PUSH BP */
	0x56,			/* PUSH SI */

	0x31, 0xDB,		/* XOR BX, BX */
	0x31, 0xD2,		/* XOR DX, DX */

	0x0F, 0xA2,		/* CPUID */

	0x89, 0x45, 0x00,	/* MOV AX, 0(BP) */
	0x89, 0x5d, 0x04,	/* MOV BX, 4(BP) */
	0x89, 0x4d, 0x08,	/* MOV CX, 8(BP) */
	0x89, 0x55, 0x0C,	/* MOV DX, 12(BP) */
	0xC3,			/* RET */
};

Res (*cpuid)(ulong ax, ulong cx) = (Res(*)(ulong, ulong)) _cpuid;

void
func0(ulong)
{
	Res r;
	char buf[13];

	r = cpuid(0, 0);
	((ulong *) buf)[0] = r.bx;
	((ulong *) buf)[1] = r.dx;
	((ulong *) buf)[2] = r.cx;
	buf[13] = 0;
	Bprint(out, "vendor %s\n", buf);
}

void
printbits(char *id, ulong x, char **s)
{
	int i, j;

	for(i = 0, j = 0; i < 32; i++)
		if((x & (1<<i)) != 0 && s[i] != nil){
			if(j++ % 16 == 0){
				if(j != 1)
					Bprint(out, "\n");
				Bprint(out, "%s ", id);
			}
			Bprint(out, "%s ", s[i]);
		}
	if(j % 16 != 0)
		Bprint(out, "\n");
}

void
func1(ulong)
{
	Res r;
	int family, model;
	static char *bitsdx[32] = {
		[0]		"fpu",  "vme",   "de",   "pse",
		[4]		"tsc",  "msr",   "pae",  "mce",
		[8]		"cx8",  "apic",  nil,    "sep",
		[12]	"mtrr", "pge",   "mca",  "cmov",
		[16]	"pat",  "pse36", "pn",   "clflush",
		[20]	nil,    "dts",   "acpi", "mmx",
		[24]	"fxsr", "sse",   "sse2", "ss",
		[28]	"ht",   "tm",    "ia64", "pbe",
	};
	static char *bitscx[32] = {
		[0]		"pni",         "pclmulqdq", "dtes64", "monitor",
		[4]		"ds_cpl",      "vmx",       "smx",    "est",
		[8]		"tm2",         "ssse3",     "cid",    nil,
		[12]	"fma",         "cx16",      "xtpr",   "pdcm",
		[16]	nil,           "pcid",      "dca",    "sse4_1",
		[20]	"sse4_2",      "x2apic",    "movbe",  "popcnt",
		[24]	"tscdeadline", "aes",       "xsave",  "osxsave",
		[28]	"avx",         "f16c",      "rdrnd",  "hypervisor",
	};

	r = cpuid(1, 0);
	Bprint(out, "procmodel %.8ulx / %.8ulx\n", r.ax, r.bx);
	family = r.ax >> 8 & 0xf;
	model = r.ax >> 8 & 0xf;
	if(family == 15)
		family += r.ax >> 20 & 0xff;
	if(family == 6 || family == 15)
		model += r.ax >> 12 & 0xf0;
	Bprint(out, "typefammod %.1x %.2x %.3x %.1x\n", (int)(r.ax >> 12 & 3), family, model, (int)(r.ax & 0xf));
	printbits("features", r.dx, bitsdx);
	printbits("features", r.cx, bitscx);
}

void
func13(ulong)
{
	Res r;
	static char *bitsax[32] = {
		[0]	"xsaveopt",
	};

	r = cpuid(13, 1);
	printbits("features", r.ax, bitsax);
}

void
extfunc1(ulong ax)
{
	Res r;
	static char *bitsdx[32] = {
		[0]		"fpu",  "vme",   "de",      "pse",
		[4]		"tsc",  "msr",   "pae",     "mce",
		[8]		"cx8",  "apic",  nil,       "syscall",
		[12]	"mtrr", "pge",   "mca",     "cmov",
		[16]	"pat",  "pse36", nil,       "mp",
		[20]	"nx",   nil,     "mmx+",    "mmx",
		[24]	"fxsr", "ffxsr", "pg1g",    "tscp",
		[28]	nil,    "lm",    "3dnow!+", "3dnow!",
	};
	static char *bitscx[32] = {
		[0]		"ahf64",   "cmp",   "svm",   "eas",
		[4]		"cr8d",    "lzcnt", "sse4a", "msse",
		[8]		"3dnow!p", "osvw",  "ibs",   "xop",
		[12]	"skinit",  "wdt",   nil,     "lwp",
		[16]	"fma4",    "tce",   nil,     "nodeid",
		[20]	nil,       "tbm",   "topx",  "pcx_core",
		[24]	"pcx_nb",  nil,     nil,     nil,
		[28]	nil,       nil,     nil,     nil,
	};

	r = cpuid(ax, 0);
	Bprint(out, "extmodel %.8ulx / %.8ulx\n", r.ax, r.bx);
	printbits("extfeatures", r.dx, bitsdx);
	printbits("extfeatures", r.cx, bitscx);
}

void
extfunc2(ulong ax)
{
	char buf[49];
	int i;
	Res r;
	char *p;

	if(ax != 0x80000004)
		return;
	buf[48] = 0;
	for(i = 0; i < 3; i++){
		r = cpuid(0x80000002 + i, 0);
		((ulong *) buf)[4 * i + 0] = r.ax;
		((ulong *) buf)[4 * i + 1] = r.bx;
		((ulong *) buf)[4 * i + 2] = r.cx;
		((ulong *) buf)[4 * i + 3] = r.dx;
	}
	p = buf;
	while(*p == ' ')
		p++;
	Bprint(out, "procname %s\n", p);
}

void
extfunc8(ulong ax)
{
	Res r;

	r = cpuid(ax, 0);
	Bprint(out, "physbits %uld\n", CUT(r.ax, 0, 7));
	Bprint(out, "virtbits %uld\n", CUT(r.ax, 8, 15));
	if(CUT(r.ax, 16, 23) != 0)
		Bprint(out, "guestbits %uld\n", CUT(r.ax, 16, 23));
}

void (*funcs[])(ulong) = {
	[0] 	func0,
	[1] 	func1,
	[13]	func13,
};

void (*extfuncs[])(ulong) = {
	[1] extfunc1,
	[2] extfunc2,
	[3] extfunc2,
	[4] extfunc2,
	[8] extfunc8,
};

void
stdfunc(ulong ax)
{
	Res r;

	r = cpuid(ax, 0);
	Bprint(out, "%.8ulx %.8ulx %.8ulx %.8ulx %.8ulx\n", ax, r.ax, r.bx, r.cx, r.dx);
}

char Egreg[] = "this information is classified";

void
notehand(void *, char *s)
{
	if(strncmp(s, "sys:", 4) == 0)
		sysfatal(Egreg);
	noted(NDFLT);
}

void
main(int argc, char **argv)
{
	Res r;
	int i, rflag, aflag;
	ulong w;
	static Biobuf buf;

	rflag = aflag = 0;
	ARGBEGIN {
	case 'r': rflag++; break;
	case 'a': aflag++; break;
	} ARGEND;
	notify(notehand);
	/* first long in a.out header */
	w = *(ulong *)(((uintptr)main)&~0xfff);
	notify(nil);
	switch(w){
	default:
		sysfatal(Egreg);
	case 0x978a0000:	/* amd64 */
		/* patch out POP BP -> POP AX */
		_cpuid[1] = 0x58;
	case 0xeb010000:	/* 386 */
		break;
	}
	segflush(_cpuid, sizeof(_cpuid));
	Binit(&buf, 1, OWRITE);
	out = &buf;
	r = cpuid(0, 0);
	for(i = 0; i <= r.ax; i++)
		if(i >= nelem(funcs) || funcs[i] == nil || rflag){
			if(rflag || aflag)
				stdfunc(i);
		}else
			funcs[i](i);
	r = cpuid(0x80000000, 0);
	if(r.ax < 0x80000000)
		exits(nil);
	r.ax -= 0x80000000;
	for(i = 0; i <= r.ax; i++)
		if(i >= nelem(extfuncs) || extfuncs[i] == nil || rflag){
			if(rflag || aflag)
				stdfunc(0x80000000 | i);
		}else
			extfuncs[i](0x80000000 | i);
	exits(nil);
}
