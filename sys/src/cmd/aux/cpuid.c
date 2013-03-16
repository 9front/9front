#include <u.h>
#include <libc.h>
#include <bio.h>

#define CUT(x, a, b) (((x)&((1<<((b)+1))-1))>>(a))

typedef struct Res {
	ulong ax, bx, cx, dx;
} Res;

Biobuf *out;

uchar _cpuid[] = {
	0x8B, 0x44, 0x24, 0x08,    /* MOV 8(SP), AX */
	0x31, 0xDB,                /* XOR BX, BX */
	0x8B, 0x4C, 0x24, 0x0C,    /* MOV 12(SP), CX */ 
	0x31, 0xD2,                /* XOR DX, DX */
	0x0F, 0xA2,                /* CPUID */
	0x8B, 0x7C, 0x24, 0x04,    /* MOV 4(SP), DI */
	0x89, 0x07,                /* MOV AX, (DI) */
	0x89, 0x5F, 0x04,          /* MOV BX, 4(DI) */
	0x89, 0x4F, 0x08,          /* MOV CX, 8(DI) */
	0x89, 0x57, 0x0C,          /* MOV DX, 12(DI) */
	0xC3                       /* RET */
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
	static char *bitsdx[32] = {
		"fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce", "cx8", "apic",
		nil, "sep", "mtrr", "pge", "mca", "cmov", "pat", "pse36", "pn", "clflush",
		nil, "dts", "acpi", "mmx", "fxsr", "sse", "sse2", "ss", "ht", "tm", "ia64", "pbe",
	};
	static char *bitscx[32] = {
		"pni", "pclmulqdq", "dtes64", "monitor", "ds_cpl", "vmx", "smx", "est", "tm2", "ssse3",
		"cid", nil, "fma", "cx16", "xtpr", "pdcm", nil, "pcid", "dca", "sse4_1", "sse4_2", "x2apic",
		"movbe", "popcnt", "tscdeadline", "aes", "xsave", "osxsave", "avx", 
		"f16c", "rdrnd", "hypervisor"
	};

	r = cpuid(1, 0);
	Bprint(out, "procmodel %.8ulx / %.8ulx\n", r.ax, r.bx);
	printbits("features", r.dx, bitsdx);
	printbits("features", r.cx, bitscx);
}

void
extfunc1(ulong ax)
{
	Res r;
	static char *bitsdx[32] = {
		"fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce", "cx8", "apic",
		nil, "syscall", "mtrr", "pge", "mca", "cmov", "pat", "pse36", nil, "mp",
		"nx", nil, "mmx+", "mmx", "fxsr", "ffxsr", "pg1g", "tscp", nil, "lm", "3dnow!+", "3dnow!"
	};
	static char *bitscx[32] = {
		"ahf64", "cmp", "svm", "eas", "cr8d", "lzcnt", "sse4a", "msse", "3dnow!p", "osvw", "ibs",
		"xop", "skinit", "wdt", nil, "lwp", "fma4", "tce", nil, "nodeid", nil, "tbm", "topx",
		"pcx_core", "pcx_nb",
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
	[0] func0,
	[1] func1,
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
	w = *(ulong *)0x1000;
	notify(nil);
	if(w != 0xeb010000)
		sysfatal(Egreg);
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
	r.ax -= 0x80000000;
	for(i = 0; i <= r.ax; i++)
		if(i >= nelem(extfuncs) || extfuncs[i] == nil || rflag){
			if(rflag || aflag)
				stdfunc(0x80000000 | i);
		}else
			extfuncs[i](0x80000000 | i);
	Bterm(out);
}
