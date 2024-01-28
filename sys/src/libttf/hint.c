#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ttf.h>
#include "impl.h"

typedef struct Hint Hint;

enum { debug = 0 };

#pragma varargck type "π" TTPoint

#define dprint(...) {if(debug) fprint(2, __VA_ARGS__);}

static TTGState defstate = {
	.fvx = 16384,
	.fvy = 0,
	.pvx = 16384,
	.pvy = 0,
	.dpvx = 16384,
	.dpvy = 0,
	.instctrl = 0,
	.scanctrl = 0,
	.rperiod = 64,
	.rphase = 0,
	.rthold = 32,
	.zp = 7,
	.cvci = 68,
	.loop = 1,
	.singlewval = 0,
	.singlewci = 0,
	.deltabase = 9,
	.deltashift = 3,
	.autoflip = 1,
	.mindist = 64,
};

struct Hint {
	TTFont *f;
	TTGlyph *g;
	u8int *shint, *ip, *ehint;
	u32int *stack;
	int sp, nstack;
	int level;
	char err[ERRMAX];
	jmp_buf jmp;
};

int
rounddiv(int a, int b)
{
	if(b < 0){ a = -a; b = -b; }
	if(a > 0)
		return (a + b/2) / b;
	else
		return (a - b/2) / b;
}

int
vrounddiv(vlong a, int b)
{
	if(b < 0){ a = -a; b = -b; }
	if(a > 0)
		return (a + b/2) / b;
	else
		return (a - b/2) / b;
}

static void
herror(Hint *h, char *fmt, ...)
{
	va_list va;
	
	va_start(va, fmt);
	vsnprint(h->err, sizeof(h->err), fmt, va);
	va_end(va);
	dprint("error: %s\n", h->err);
	longjmp(h->jmp, 1);
}

static void
push(Hint *h, u32int w)
{
	assert(h->sp < h->nstack);
	h->stack[h->sp++] = w;
}

static u32int
pop(Hint *h)
{
	assert(h->sp > 0);
	return h->stack[--h->sp];
}

static u8int
fetch8(Hint *h)
{
	if(h->ip == h->ehint)
		herror(h, "missing byte");
	return *h->ip++;
}

enum {
	RP0 = 0x10,
	RP1 = 0x20,
	RP2 = 0x30,
	ZP0 = 0,
	ZP1 = 0x100,
	ZP2 = 0x200,
	ORIG = 0x1000,
	NOTOUCH = 0x2000,
};

static TTPoint
getpoint(Hint *h, int n, int pi)
{
	if((n & RP2) != 0)
		pi = h->f->rp[(n >> 4 & 3) - 1];
	if((h->f->zp >> (n >> 8 & 3) & 1) != 0){
		if(h->g == nil)
			herror(h, "access to glyph zone from FPGM/CVT");
		if((uint)pi >= h->g->npt)
			herror(h, "glyph zone point index %d out of range", pi);
		dprint("G%s%d: %+π\n", n&ORIG?"O":"", pi, (n & ORIG) != 0 ? h->g->ptorg[pi] : h->g->pt[pi]);
		return (n & ORIG) != 0 ? h->g->ptorg[pi] : h->g->pt[pi];
	}else{
		if((uint)pi >= h->f->u->maxTwilightPoints)
			herror(h, "twilight zone point index %d out of range", pi);
		return (n & ORIG) != 0 ? h->f->twiorg[pi] : h->f->twilight[pi];
	}
}

static void
setpoint(Hint *h, int n, int pi, TTPoint p)
{
	if((n & RP2) != 0)
		pi = h->f->rp[(n >> 4 & 3) - 1];
	if((n & NOTOUCH) == 0){
		if(h->f->fvx != 0) p.flags |= 2;
		if(h->f->fvy != 0) p.flags |= 4;
	}
	if((h->f->zp >> (n >> 8 & 3) & 1) != 0){
		if(h->g == nil)
			herror(h, "access to glyph zone from FPGM/CVT");
		if((uint)pi >= h->g->npt)
			herror(h, "glyph zone point index %d out of range", pi);
		dprint("G%d: %+π -> %+π\n", pi, h->g->pt[pi], p);
		h->g->pt[pi] = p;
	}else{
		if((uint)pi >= h->f->u->maxTwilightPoints)
			herror(h, "twilight zone point index %d out of range", pi);
		dprint("T%d: %+π -> %+π\n", pi, h->f->twilight[pi], p);
		h->f->twilight[pi] = p;
	}
}

static TTPoint
getpointz(Hint *h, int z, int pi)
{
	if((z & 1) != 0){
		if(h->g == nil)
			herror(h, "access to glyph zone from FPGM/CVT");
		if((uint)pi >= h->g->npt)
			herror(h, "glyph zone point index %d out of range", pi);
		dprint("G%s%d: %+π\n", z&ORIG?"O":"", pi, (z & ORIG) != 0 ? h->g->ptorg[pi] : h->g->pt[pi]);
		return (z & ORIG) != 0 ? h->g->ptorg[pi] : h->g->pt[pi];
	}else{
		if((uint)pi >= h->f->u->maxTwilightPoints)
			herror(h, "twilight zone point index %d out of range", pi);
		return (z & ORIG) != 0 ? h->f->twiorg[pi] : h->f->twilight[pi];
	}
}

static void
setpointz(Hint *h, int z, int pi, TTPoint p)
{
	if((z & 1) != 0){
		if(h->g == nil)
			herror(h, "access to glyph zone from FPGM/CVT");
		if((uint)pi >= h->g->npt)
			herror(h, "glyph zone point index %d out of range", pi);
		dprint("G%d: %+π -> %+π\n", pi, h->g->pt[pi], p);
		h->g->pt[pi] = p;
	}else{
		if((uint)pi >= h->f->u->maxTwilightPoints)
			herror(h, "twilight zone point index %d out of range", pi);
		dprint("T%d: %+π -> %+π\n", pi, h->f->twilight[pi], p);
		h->f->twilight[pi] = p;
	}
}


static void
debugprint(Hint *h, int skip)
{
	Fmt f;
	char buf[256];

	static char *opcnames[256] = {
		[0x00] "SVTCA", "SVTCA", "SPVTCA", "SPVTCA", "SFVTCA", "SFVTCA", "SPVTL", "SPVTL",
		[0x08] "SFVTL", "SFVTL", "SPVFS", "SFVFS", "GPV", "GFV", "SFVTPV", "ISECT",
		[0x10] "SRP0", "SRP1", "SRP2", "SZP0", "SZP1", "SZP2", "SZPS", "SLOOP",
		[0x18] "RTG", "RTHG", "SMD", "ELSE", "JMPR", "SCVTCI", "SSWCI", "SSW",
		[0x20] "DUP", "POP", "CLEAR", "SWAP", "DEPTH", "CINDEX", "MINDEX", "ALIGNPTS",
		[0x28] nil, "UTP", "LOOPCALL", "CALL", "FDEF", "ENDF", "MDAP", "MDAP",
		[0x30] "IUP", "IUP", "SHP", "SHP", "SHC", "SHC", "SHZ", "SHZ",
		[0x38] "SHPIX", "IP", "MSIRP", "MSIRP", "ALIGNRP", "RTDG", "MIAP", "MIAP",
		[0x40] "NPUSHB", "NPUSHW", "WS", "RS", "WCVTP", "RCVT", "GC", "GC",
		[0x48] "SCFS", "MD", "MD", "MPPEM", "MPS", "FLIPON", "FLIPOFF", "DEBUG",
		[0x50] "LT", "LTEQ", "GT", "GTEQ", "EQ", "NEQ", "ODD", "EVEN",
		[0x58] "IF", "EIF", "AND", "OR", "NOT", "DELTAP1", "SDB", "SDS",
		[0x60] "ADD", "SUB", "DIV", "MUL", "ABS", "NEG", "FLOOR", "CEILING",
		[0x68] "ROUND", "ROUND", "ROUND", "ROUND", "NROUND", "NROUND", "NROUND", "NROUND",
		[0x70] "WCVTF", "DELTAP2", "DELTAP3", "DELTAC1", "DELTAC2", "DELTAC3", "SROUND", "S45ROUND",
		[0x78] "JROT", "JROF", "ROFF", nil, "RUTG", "RDTG", "SANGW", "AA",
		[0x80] "FLIPPT", "FLIPRGON", "FLIPRGOFF", [0x85] "SCANCTRL", "SDPVTL", "SDPVTL",
		[0x88] "GETINFO", "IDEF", "ROLL", "MAX", "MIN", "SCANTYPE", "INSTCTRL", nil,
		[0xB0] "PUSHB", "PUSHB", "PUSHB", "PUSHB", "PUSHB", "PUSHB", "PUSHB", "PUSHB", 
		[0xB8] "PUSHW", "PUSHW", "PUSHW", "PUSHW", "PUSHW", "PUSHW", "PUSHW", "PUSHW",
	};
	static u8int argb[256] = {
		[0x00] 1, 1, 1, 1, 1, 1, 1, 1,
		[0x08] 1, 1, 1, 1, 1, 1,
		[0x2e] 1, 1,
		[0x30] 1, 1, 1, 1, 1, 1, 1, 1,
		[0x38] 0, 0, 1, 1, 0, 0, 1, 1,
		[0x46] 1, 1, 0, 1, 1,
		[0x68] 2, 2, 2, 2, 2, 2, 2, 2,
	};
	u8int op;
	int i;

	fmtfdinit(&f, 2, buf, sizeof(buf));
	op = *h->ip;
	if(skip) fmtprint(&f, "** ");
	fmtprint(&f, "%d %d ", h->level, (int)(h->ip - h->shint));
	if(op >= 0xc0)
		fmtprint(&f, "%s[%d]", op >= 0xe0 ? "MIRP" : "MDRP", op & 0x1f);
	else if(opcnames[op] == nil)
		fmtprint(&f, "???");
	else
		fmtprint(&f, argb[op] != 0 ? "%s[%d]" : "%s[]", opcnames[op], op & (1<<argb[op]) - 1);
	if(!skip){
		fmtprint(&f, " :: ");
		for(i = 0; i < 8 && i < h->sp; i++)
			fmtprint(&f, "%d ", h->stack[h->sp - 1 - i]);
	}
	fmtprint(&f, "\n");
	fmtfdflush(&f);
}

static void
h_npushb(Hint *h)
{
	u8int n, b;
	
	n = fetch8(h);
	while(n-- > 0){
		b = fetch8(h);
		push(h, b);
	}
}

static void
h_npushw(Hint *h)
{
	u8int n;
	u32int x;
	
	n = fetch8(h);
	while(n-- > 0){
		x = fetch8(h) << 8;
		x |= fetch8(h);
		push(h, (short)x);
	}
}

static void
h_pushb(Hint *h)
{
	int n;
	u8int b;
	
	n = (h->ip[-1] & 7) + 1;
	while(n-- > 0){
		b = fetch8(h);
		push(h, b);
	}
}

static void
h_pushw(Hint *h)
{
	int n;
	u16int w;
	
	n = (h->ip[-1] & 7) + 1;
	while(n-- > 0){
		w = fetch8(h) << 8;
		w |= fetch8(h);
		push(h, (short)w);
	}
}

static void
skip(Hint *h, int mode)
{
	int level;

	level = 0;
	for(;;){
		if(h->ip >= h->ehint)
			herror(h, "reached end of stream during skip()");
		if(debug) debugprint(h, 1);
		switch(mode){
		case 0:
			if(*h->ip == 0x2d)
				return;
			break;
		case 1:
			if(level == 0 && (*h->ip == 0x1b || *h->ip == 0x59))
				return;
		}
		switch(*h->ip++){
		case 0x40:
		case 0x41:
			if(h->ip < h->ehint)
				h->ip += *h->ip + 1;
			break;
		case 0x58: level++; break;
		case 0x59: level--; break;
		case 0xb0: case 0xb1: case 0xb2: case 0xb3:
		case 0xb4: case 0xb5: case 0xb6: case 0xb7:
			h->ip += (h->ip[-1] & 7) + 1;
			break;
		case 0xb8: case 0xb9: case 0xba: case 0xbb:
		case 0xbc: case 0xbd: case 0xbe: case 0xbf:
			h->ip += 2 * ((h->ip[-1] & 7) + 1);
			break;
		}
	}
}

static void
h_fdef(Hint *h)
{
	int i;
	u8int *sp;
	TTFont *f;
	
	f = h->f;
	i = pop(h);
	if((uint)i >= h->f->u->maxFunctionDefs)
		herror(h, "function identifier out of range");
	sp = h->ip;
	skip(h, 0);
	f->func[i].npgm = h->ip - sp;
	f->func[i].pgm = mallocz(f->func[i].npgm, 1);
	if(f->func[i].pgm == nil)
		herror(h, "malloc: %r");
	memcpy(f->func[i].pgm, sp, f->func[i].npgm);
	h->ip++;
}

static void run(Hint *);

static void
h_call(Hint *h)
{
	int i;
	u8int *lip, *lshint, *lehint;
	
	i = pop(h);
	if((uint)i >= h->f->u->maxFunctionDefs || h->f->func[i].npgm == 0)
		herror(h, "undefined funcion %d", i);
	lip = h->ip;
	lshint = h->shint;
	lehint = h->ehint;
	h->ip = h->shint = h->f->func[i].pgm;
	h->ehint = h->ip + h->f->func[i].npgm;
	h->level++;
	run(h);
	h->level--;
	h->ip = lip;
	h->shint = lshint;
	h->ehint = lehint;
}

static void
h_loopcall(Hint *h)
{
	int i, n;
	u8int *lip, *lshint, *lehint;
	
	i = pop(h);
	n = pop(h);
	if((uint)i >= h->f->u->maxFunctionDefs || h->f->func[i].npgm == 0)
		herror(h, "undefined funcion %d", i);
	for(; n > 0; n--){
		lip = h->ip;
		lshint = h->shint;
		lehint = h->ehint;
		h->ip = h->shint = h->f->func[i].pgm;
		h->ehint = h->ip + h->f->func[i].npgm;
		h->level++;
		run(h);
		h->level--;
		h->ip = lip;
		h->shint = lshint;
		h->ehint = lehint;
	}
}

static void
h_dup(Hint *h)
{
	u32int x;
	
	x = pop(h);
	push(h, x);
	push(h, x);
}

static void
h_swap(Hint *h)
{
	u32int x, y;
	
	x = pop(h);
	y = pop(h);
	push(h, x);
	push(h, y);
}

static void
h_cindex(Hint *h)
{
	int n;
	
	n = pop(h);
	if(n <= 0 || n > h->sp)
		herror(h, "CINDEX[%d] out of range", n);
	push(h, h->stack[h->sp - n]);
}

static void
h_mindex(Hint *h)
{
	int n, x;
	
	n = pop(h);
	if(n <= 0 || n > h->sp)
		herror(h, "MINDEX[%d] out of range", n);
	x = h->stack[h->sp - n];
	memmove(&h->stack[h->sp - n], &h->stack[h->sp - n + 1], (n - 1) * sizeof(u32int));
	h->stack[h->sp - 1] = x;
}

static void
h_svtca(Hint *h)
{
	int a;
	
	a = h->ip[-1];
	if(a < 2 || a >= 4){
		h->f->fvx = 16384 * (a & 1);
		h->f->fvy = 16384 * (~a & 1);
	}
	if(a < 4){
		h->f->dpvx = h->f->pvx = 16384 * (a & 1);
		h->f->dpvy = h->f->pvy = 16384 * (~a & 1);
	}
}

static void
h_instctrl(Hint *h)
{
	int s, v;
	
	s = pop(h);
	v = pop(h);
	if(v != 0)
		h->f->instctrl |= 1<<s;
	else
		h->f->instctrl &= ~(1<<s);
}

static void
h_mppem(Hint *h)
{
	push(h, h->f->ppem);
}

static int
ttround(Hint *h, int x)
{
	int y;
	
	if(h->f->rperiod == 0) return x;
	if(x >= 0){
		y = x - h->f->rphase + h->f->rthold;
		y -= y % h->f->rperiod;
		y += h->f->rphase;
		if(y < 0) y = h->f->rphase;
	}else{
		y = x + h->f->rphase - h->f->rthold;
		y -= y % h->f->rperiod;
		y -= h->f->rphase;
		if(y > 0) y = -h->f->rphase;
	}
	return y;
}

static void
h_binop(Hint *h)
{
	int a, b, r;
	
	b = pop(h);
	a = pop(h);
	switch(h->ip[-1]){
	case 0x50: r = a < b; break;
	case 0x51: r = a <= b; break;
	case 0x52: r = a > b; break;
	case 0x53: r = a >= b; break;
	case 0x54: r = a == b; break;
	case 0x55: r = a != b; break;
	case 0x5a: r = a && b; break;
	case 0x5b: r = a || b; break;
	case 0x60: r = a + b; break;
	case 0x61: r = a - b; break;
	case 0x62: if(b == 0) herror(h, "division by zero"); r = (vlong)(int)a * 64 / (int)b; break;
	case 0x63: r = (vlong)(int)a * (vlong)(int)b >> 6; break;
	case 0x8b: r = a < b ? b : a; break;
	case 0x8c: r = a < b ? a : b; break;
	default: abort();
	}
	push(h, r);
}

static void
h_unop(Hint *h)
{
	u32int a, r;
	
	a = pop(h);
	switch(h->ip[-1]){
	case 0x56: r = (ttround(h, a) / 64 & 1) != 0; break;
	case 0x57: r = (ttround(h, a) / 64 & 1) == 0; break;
	case 0x5c: r = !a; break;
	case 0x64: r = (int)a < 0 ? -a : a; break;
	case 0x65: r = -a; break;
	case 0x66: r = a & -64; break;
	case 0x67: r = -(-a & -64); break;
	case 0x68: case 0x69: case 0x6a: case 0x6b: r = ttround(h, a); break;
	default: abort();
	}
	push(h, r);
}

static void
h_rs(Hint *h)
{
	int n;
	
	n = pop(h);
	if((uint)n >= h->f->u->maxStorage)
		herror(h, "RS[%d] out of bounds");
	push(h, h->f->storage[n]);
}

static void
h_ws(Hint *h)
{
	u32int v;
	int n;
	
	v = pop(h);
	n = pop(h);
	if((uint)n >= h->f->u->maxStorage)
		herror(h, "WS[%d] out of bounds");
	h->f->storage[n] = v;
}

static void
h_if(Hint *h)
{
	u32int x;
	
	x = pop(h);
	if(!x){
		skip(h, 1);
		h->ip++;
	}
}

static void
h_else(Hint *h)
{
	skip(h, 1);
	h->ip++;
}

static void
h_nop(Hint *)
{
}

static void
h_getinfo(Hint *h)
{
	int s;
	u32int r;
	
	s = pop(h);
	r = 0;
	if((s & 1) != 0) r |= 3;
	push(h, r);
}

static void
h_scanctrl(Hint *h)
{
	h->f->scanctrl = pop(h);
}

static void
h_scantype(Hint *h)
{
	h->f->scantype = pop(h);
}

static void
h_roundst(Hint *h)
{
	h->f->rperiod = 64;
	h->f->rphase = 0;
	h->f->rthold = 32;
	switch(h->ip[-1]){
	case 0x19: /* RTHG */
		h->f->rphase = 32;
		break;
	case 0x3D: /* RTDG */
		h->f->rperiod = 32;
		h->f->rthold = 16;
		break;
	case 0x7C: /* RUTG */
		h->f->rthold = 63;
		break;
	case 0x7D: /* RDTG */
		h->f->rthold = 0;
		break;
	case 0x7A: /* ROFF */
		h->f->rperiod = 0;
		break;
	}
}

static void
h_sround(Hint *h)
{
	u8int n;
	
	n = pop(h);
	if((n >> 6 & 3) == 3)
		herror(h, "(S)ROUND: period set to reserved value 3");
	if(h->ip[-1] == 0x77)
		h->f->rperiod = 181 >> (2 - (n >> 6 & 3));
	else
		h->f->rperiod = 32 << (n >> 6 & 3);
	h->f->rphase = h->f->rperiod * (n >> 4 & 3) / 4;
	if((n & 15) == 0)
		h->f->rthold = h->f->rperiod - 1;
	else
		h->f->rthold = h->f->rperiod * ((int)(n & 15) - 4) / 8;
}

static void
h_srp(Hint *h)
{
	h->f->rp[h->ip[-1] & 3] = pop(h);
}

static void
h_szp(Hint *h)
{
	int n, t;
	
	n = pop(h);
	if(n>>1 != 0) herror(h, "SZP invalid argument %d", n);
	t = h->ip[-1] - 0x13;
	if(t == 3) h->f->zp = 7 * n;
	else h->f->zp = h->f->zp & ~(1<<t) | n<<t;
}

static int
project(Hint *h, TTPoint *p, TTPoint *q)
{
	if(q == nil)
		return rounddiv(h->f->pvx * p->x + h->f->pvy * p->y, 16384);
	return rounddiv(h->f->pvx * (p->x - q->x) + h->f->pvy * (p->y - q->y), 16384);
}

static int
dualproject(Hint *h, TTPoint *p, TTPoint *q)
{
	if(q == nil)
		return rounddiv(h->f->dpvx * p->x + h->f->dpvy * p->y, 16384);
	return rounddiv(h->f->dpvx * (p->x - q->x) + h->f->dpvy * (p->y - q->y), 16384);
}

static TTPoint
forceproject(Hint *h, TTPoint p, int d)
{
	TTFont *f;
	TTPoint n;
	int den;
	vlong k;

	f = h->f;
	den = f->pvx * f->fvx + f->pvy * f->fvy;
	if(den == 0) herror(h, "FV and PV orthogonal");
	k = f->fvx * p.y - f->fvy * p.x;
	n.x = vrounddiv(16384LL * d * f->fvx - k * f->pvy, den);
	n.y = vrounddiv(16384LL * d * f->fvy + k * f->pvx, den);
	n.flags = p.flags;
	return n;
}

static void
h_miap(Hint *h)
{
	int a, pi, di, d, d0, d1;
	TTPoint p, n;
	
	a = h->ip[-1] & 1;
	di = pop(h);
	pi = pop(h);
	if((uint)di >= h->f->ncvt) herror(h, "MIAP out of range");
	p = getpoint(h, ZP0, pi);
	d0 = h->f->cvt[di];
	dprint("cvt %d\n", d0);
	d1 = project(h, &p, nil);
	dprint("old %d\n", d1);
	d = d0;
	if((h->f->zp & 1) != 0){
		if(a && abs(d1 - d) > h->f->cvci)
			d = d1;
	}else{
		/* fuck you microsoft */
		h->f->twiorg[pi].x = rounddiv(d0 * h->f->pvx, 16384);
		h->f->twiorg[pi].y = rounddiv(d0 * h->f->pvy, 16384);
	}
	if(a) d = ttround(h, d);
	n = forceproject(h, p, d);
	setpoint(h, 0x80, pi, n);
	h->f->rp[0] = h->f->rp[1] = pi;
}

static void
h_mdap(Hint *h)
{
	int pi;
	TTPoint p;
	
	pi = pop(h);
	p = getpoint(h, ZP0, pi);
	if((h->ip[-1] & 1) != 0)
		p = forceproject(h, p, ttround(h, project(h, &p, nil)));
	setpoint(h, ZP0, pi, p);
	h->f->rp[0] = h->f->rp[1] = pi;
}

static void
h_ip(Hint *h)
{
	int i;
	int pi;
	TTPoint p1, op1, p2, op2, p, op, n;
	int dp1, dp2, do12, d;

	p1 = getpoint(h, RP1 | ZP0, 0);
	op1 = getpoint(h, RP1 | ZP0 | ORIG, 0);
	p2 = getpoint(h, RP2 | ZP1, 0);
	op2 = getpoint(h, RP2 | ZP1 | ORIG, 0);
	dp1 = project(h, &p1, nil);
	dp2 = project(h, &p2, nil);
	do12 = dualproject(h, &op1, &op2);
	if(do12 == 0)
		herror(h, "invalid IP[] call");
	for(i = 0; i < h->f->loop; i++){
		pi = pop(h);
		p = getpoint(h, ZP2, pi);
		op = getpoint(h, ZP2 | ORIG, pi);
		d = ttfvrounddiv((vlong)dp1 * dualproject(h, &op, &op2) - (vlong)dp2 * dualproject(h, &op, &op1), do12);
		n = forceproject(h, p, d);
		setpoint(h, 0x82, pi, n);
		dprint("(%d,%d) -> (%d,%d)\n", p.x, p.y, n.x, n.y);
	}
	h->f->loop = 1;
}

static void
h_gc0(Hint *h)
{
	int pi;
	TTPoint p;
	
	pi = pop(h);
	p = getpoint(h, ZP2, pi);
	push(h, project(h, &p, nil));
}

static void
h_gc1(Hint *h)
{
	int pi;
	TTPoint p;
	
	pi = pop(h);
	p = getpoint(h, ZP2|ORIG, pi);
	push(h, dualproject(h, &p, nil));
}

static void
h_wcvtp(Hint *h)
{
	u32int v, l;
	
	v = pop(h);
	l = pop(h);
	if(l >= h->f->ncvt) herror(h, "WCVTP out of range");
	h->f->cvt[l] = v;
}

static void
h_wcvtf(Hint *h)
{
	u32int v, l;
	
	v = pop(h);
	l = pop(h);
	if(l >= h->f->ncvt) herror(h, "WCVTF out of range");
	h->f->cvt[l] = rounddiv(v * h->f->ppem * 64, h->f->u->emsize);
}

static void
h_rcvt(Hint *h)
{
	u32int l;
	
	l = pop(h);
	if(l >= h->f->ncvt) herror(h, "RCVT out of range");
	push(h, h->f->cvt[l]);
}

static void
h_round(Hint *h)
{
	push(h, ttround(h, pop(h)));
}

static void
h_roll(Hint *h)
{
	u32int a, b, c;
	
	a = pop(h);
	b = pop(h);
	c = pop(h);
	push(h, b);
	push(h, a);
	push(h, c);
}

static void
h_pop(Hint *h)
{
	pop(h);
}

static void
h_clear(Hint *h)
{
	h->sp = 0;
}

static void
h_depth(Hint *h)
{
	push(h, h->sp);
}

static void
h_scvtci(Hint *h)
{
	h->f->cvci = pop(h);
}

static void
h_mirp(Hint *h)
{
	int a;
	u32int cvti, pi;
	TTPoint n, p, p0, op, op0;
	int d0, d;
	
	a = h->ip[-1] & 31;
	cvti = pop(h);
	pi = pop(h);
	if(cvti >= h->f->ncvt)
		herror(h, "MIRP out of bounds");
	d = h->f->cvt[cvti];
	dprint("cvt %d\n", d);
	if(abs(d - h->f->singlewval) < h->f->singlewci)
		d = d < 0 ? -h->f->singlewci : h->f->singlewci;
	dprint("single %d\n", d);
	p = getpoint(h, ZP1, pi);
	p0 = getpoint(h, ZP0 | RP0, 0);
	op = getpoint(h, ZP1 | ORIG, pi);
	op0 = getpoint(h, ZP0 | RP0 | ORIG, 0);
	d0 = dualproject(h, &op, &op0);
	if(h->f->autoflip && (d0 ^ d) < 0)
		d = -d;
	if((a & 4) != 0){
		if((h->f->zp + 1 & 3) <= 1 && abs(d - d0) > h->f->cvci)
			d = d0;
		dprint("cutin %d (%d)\n", d, h->f->cvci);
		d = ttround(h, d);
	}
	dprint("round %d\n", d);
	if((a & 8) != 0)
		if(d0 >= 0){
			if(d < h->f->mindist)
				d = h->f->mindist;
		}else{
			if(d > -h->f->mindist)
				d = -h->f->mindist;
		}
	dprint("mindist %d (%d)\n", d, h->f->mindist);
	d += project(h, &p0, nil);
	dprint("total %d\n", d);
	n = forceproject(h, p, d);
	setpoint(h, ZP1, pi, n);
	h->f->rp[1] = h->f->rp[0];
	h->f->rp[2] = pi;
	if((a & 16) != 0)
		h->f->rp[0] = pi;
}

static void
h_msirp(Hint *h)
{
	int a;
	u32int pi;
	TTPoint n, p, p0;
	int d;
	
	a = h->ip[-1] & 31;
	d = pop(h);
	pi = pop(h);
	if(abs(d - h->f->singlewval) < h->f->singlewci)
		d = d < 0 ? -h->f->singlewci : h->f->singlewci;
	p = getpoint(h, ZP1, pi);
	p0 = getpoint(h, ZP0 | RP0, 0);
	d += project(h, &p0, nil);
	n = forceproject(h, p, d);
	setpoint(h, ZP1, pi, n);
	h->f->rp[1] = h->f->rp[0];
	h->f->rp[2] = pi;
	if((a & 1) != 0)
		h->f->rp[0] = pi;
}

static void
h_deltac(Hint *h)
{
	int n, b, c, arg;
	
	n = pop(h);
	b = (h->ip[-1] - 0x73) * 16 + h->f->deltabase;
	while(n--){
		c = pop(h);
		arg = pop(h);
		if(h->f->ppem != b + (arg >> 4)) continue;
		arg &= 0xf;
		arg = arg + (arg >> 3) - 8 << h->f->deltashift;
		if((uint)c >= h->f->ncvt) herror(h, "DELTAC argument out of range");
		h->f->cvt[c] += arg;
	}
}

static void
h_deltap(Hint *h)
{
	int cnt, b, pi, arg;
	TTPoint p, n;
	
	cnt = pop(h);
	b = (h->ip[-1] == 0x5d ? 0 : h->ip[-1] - 0x70) * 16 + h->f->deltabase;
	while(cnt--){
		pi = pop(h);
		arg = pop(h);
		if(h->f->ppem != b + (arg >> 4)) continue;
		arg &= 0xf;
		arg = arg + (arg >> 3) - 8 << h->f->deltashift;
		p = getpoint(h, ZP0, pi);
		n = forceproject(h, p, project(h, &p, nil) + arg);
		setpoint(h, ZP0, pi, n);
	}
}

static void
h_jmpr(Hint *h)
{
	h->ip += (int)pop(h) - 1;
	if(h->ip < h->shint || h->ip > h->ehint)
		herror(h, "JMPR out of bounds");
}

static void
h_jrcond(Hint *h)
{
	u32int e;
	int n;
	
	e = pop(h);
	n = pop(h) - 1;
	if((e == 0) == (h->ip[-1] & 1)){
		h->ip += n;
		if(h->ip < h->shint || h->ip > h->ehint)
			herror(h, "JROT/JROF out of bounds");
	}
}

static void
h_smd(Hint *h)
{
	h->f->mindist = pop(h);
}

static void
h_alignrp(Hint *h)
{
	int i, pi;
	TTPoint p, q, n;
	int dq;
	
	q = getpoint(h, ZP0 | RP0, 0);
	dq = project(h, &q, nil);
	for(i = 0; i < h->f->loop; i++){
		pi = pop(h);
		p = getpoint(h, ZP1, pi);
		n = forceproject(h, p, dq);
		setpoint(h, ZP1, pi, n);
	}
	h->f->loop = 1;
}

static TTPoint
dirvec(TTPoint a, TTPoint b)
{
	TTPoint r;
	double d;
	
	r.x = a.x - b.x;
	r.y = a.y - b.y;
	if(r.x == 0 && r.y == 0) r.x = 1<<14;
	else{
		d = hypot(r.x, r.y);
		r.x = r.x / d * 16384;
		r.y = r.y / d * 16384;
	}
	return r;
}

static void
h_sxvtl(Hint *h)
{
	int pi1, pi2;
	TTPoint p1, p2;
	TTPoint p;
	int z;
	
	pi2 = pop(h);
	pi1 = pop(h);
	p1 = getpoint(h, ZP1, pi1);
	p2 = getpoint(h, ZP2, pi2);
	p = dirvec(p1, p2);
	if((h->ip[-1] & 1) != 0){
		z = p.x;
		p.x = -p.y;
		p.y = z;
	}
	if(h->ip[-1] >= 8){
		h->f->fvx = p.x;
		h->f->fvy = p.y;
	}else{
		h->f->dpvx = h->f->pvx = p.x;
		h->f->dpvy = h->f->pvy = p.y;
	}
}

static void
h_sfvfs(Hint *h)
{
	h->f->fvy = pop(h);
	h->f->fvx = pop(h);
}

static void
h_spvfs(Hint *h)
{
	h->f->dpvy = h->f->pvy = pop(h);
	h->f->dpvx = h->f->pvx = pop(h);
}

static void
h_gfv(Hint *h)
{
	push(h, h->f->fvx);
	push(h, h->f->fvy);
}

static void
h_gpv(Hint *h)
{
	push(h, h->f->pvx);
	push(h, h->f->pvy);
}

static void
h_mdrp(Hint *h)
{
	int pi;
	TTPoint p, p0, op, op0, n;
	int d, d0;
	
	pi = pop(h);
	p = getpoint(h, ZP1, pi);
	p0 = getpoint(h, ZP0 | RP0, 0);
	op = getpoint(h, ZP1 | ORIG, pi);
	op0 = getpoint(h, ZP0 | RP0 | ORIG, 0);
	d = d0 = dualproject(h, &op, &op0);
	if(abs(d - h->f->singlewval) < h->f->singlewci)
		d = d >= 0 ? -h->f->singlewci : h->f->singlewci;
	if((h->ip[-1] & 4) != 0)
		d = ttround(h, d);
	if((h->ip[-1] & 8) != 0)
		if(d0 >= 0){
			if(d < h->f->mindist)
				d = h->f->mindist;
		}else{
			if(d > -h->f->mindist)
				d = -h->f->mindist;
		}
	n = forceproject(h, p, d + project(h, &p0, nil));
	setpoint(h, ZP1, pi, n);
	h->f->rp[1] = h->f->rp[0];
	h->f->rp[2] = pi;
	if((h->ip[-1] & 16) != 0)
		h->f->rp[0] = pi;
}

static void
h_sdpvtl(Hint *h)
{
	int pi1, pi2;
	TTPoint p1, p2;
	TTPoint op1, op2;
	TTPoint p;
	
	pi2 = pop(h);
	pi1 = pop(h);
	p1 = getpoint(h, ZP1, pi1);
	p2 = getpoint(h, ZP2, pi2);
	op1 = getpoint(h, ZP1 | ORIG, pi1);
	op2 = getpoint(h, ZP2 | ORIG, pi2);
	p = dirvec(p1, p2);
	if((h->ip[-1] & 1) != 0){
		h->f->pvx = -p.y;
		h->f->pvy = p.x;
	}else{
		h->f->pvx = p.x;
		h->f->pvy = p.y;
	}
	p = dirvec(op1, op2);
	if((h->ip[-1] & 1) != 0){
		h->f->dpvx = -p.y;
		h->f->dpvy = p.x;
	}else{
		h->f->dpvx = p.x;
		h->f->dpvy = p.y;
	}
}

static void
h_sfvtpv(Hint *h)
{
	h->f->fvx = h->f->pvx;
	h->f->fvy = h->f->pvy;
}

static void
h_sdb(Hint *h)
{
	h->f->deltabase = pop(h);
}

static void
h_sds(Hint *h)
{
	h->f->deltashift = pop(h);
}

static void
h_ssw(Hint *h)
{
	h->f->singlewval = pop(h);
}

static void
h_sswci(Hint *h)
{
	h->f->singlewci = pop(h);
}

static void
h_fliponoff(Hint *h)
{
	h->f->autoflip = h->ip[-1] & 1;
}

static void
h_md0(Hint *h)
{
	TTPoint p0, p1;
	
	p1 = getpoint(h, ZP1, pop(h));
	p0 = getpoint(h, ZP0, pop(h));
	push(h, project(h, &p0, &p1));
}

static void
h_md1(Hint *h)
{
	TTPoint p0, p1;
	
	p1 = getpoint(h, ZP1 | ORIG, pop(h));
	p0 = getpoint(h, ZP0 | ORIG, pop(h));
	push(h, dualproject(h, &p0, &p1));
}

static void
h_shpix(Hint *h)
{
	int i, d, pi, dx, dy;
	TTPoint p;
	
	d = pop(h);
	dx = vrounddiv((vlong)h->f->fvx * d, 16384);
	dy = vrounddiv((vlong)h->f->fvy * d, 16384);
	for(i = 0; i < h->f->loop; i++){
		pi = pop(h);
		p = getpoint(h, ZP2, pi);
		p.x += dx;
		p.y += dy;
		setpoint(h, ZP2, pi, p);
	}
	h->f->loop = 1;
}

static void
iup1(Hint *h, int ip, int iq, int i, int e)
{
	TTGlyph *g;
	int z;
	
	g = h->g;
	if(g->ptorg[ip].x == g->ptorg[iq].x)
		for(; i <= e; i++)
			g->pt[i].x = g->ptorg[i].x + g->pt[iq].x - g->ptorg[iq].x;
	else
		for(; i <= e; i++){
			z = (g->ptorg[i].x - g->ptorg[iq].x) * 64 / (g->ptorg[ip].x - g->ptorg[iq].x);
			if(z < 0) z = 0;
			else if(z > 64) z = 64;
			g->pt[i].x = g->ptorg[i].x + (((g->pt[ip].x - g->ptorg[ip].x) * z + (g->pt[iq].x - g->ptorg[iq].x) * (64 - z)) /  64);
		}
}

static void
iup0(Hint *h, int ip, int iq, int i, int e)
{
	TTGlyph *g;
	int z;
	
	g = h->g;
	if(g->ptorg[ip].y == g->ptorg[iq].y)
		for(; i <= e; i++)
			g->pt[i].y = g->ptorg[i].y + g->pt[iq].y - g->ptorg[iq].y;
	else
		for(; i <= e; i++){
			z = (g->ptorg[i].y - g->ptorg[iq].y) * 64 / (g->ptorg[ip].y - g->ptorg[iq].y);
			if(z < 0) z = 0;
			else if(z > 64) z = 64;
			g->pt[i].y = g->ptorg[i].y + (((g->pt[ip].y - g->ptorg[ip].y) * z + (g->pt[iq].y - g->ptorg[iq].y) * (64 - z)) / 64);
		}
}

static void
h_iup(Hint *h)
{
	int i, j, t0, t1;
	TTPoint *p;
	void (*iupp)(Hint *, int, int, int, int);

	iupp = (h->ip[-1] & 1) != 0 ? iup1 : iup0;
	for(i = 0; i < h->g->ncon; i++){
		t0 = t1 = -1;
		for(j = h->g->confst[i]; j < h->g->confst[i+1]; j++){
			p = &h->g->pt[j];
			if((p->flags & TOUCHY>>(h->ip[-1]&1)) != 0){
				if(t0 < 0)
					t0 = j;
				if(t1 >= 0)
					iupp(h, t1, j, t1 + 1, j - 1);
				t1 = j;
			}
		}
		if(t1 != t0){
			iupp(h, t1, t0, h->g->confst[i], t0 - 1);
			iupp(h, t1, t0, t1 + 1, h->g->confst[i+1]-1);
		}else if(t0 >= 0)
			iupp(h, t0, t0, h->g->confst[i], h->g->confst[i+1]-1);
	}
	
	for(i = 0; i < h->g->npt; i++)
		dprint("%d: %+π\n", i, h->g->pt[i]);
}

static void
h_sloop(Hint *h)
{
	int n;
	
	n = pop(h);
	if(n <= 0)
		herror(h, "SLOOP invalid argument %d", n);
	h->f->loop = n;
}

static void
h_scfs(Hint *h)
{
	int d, pi;
	TTPoint p, n;
	
	d = pop(h);
	pi = pop(h);
	p = getpoint(h, ZP2, pi);
	n = forceproject(h, p, d);
	setpoint(h, ZP2, pi, n);
}

static void
h_fliprg(Hint *h)
{
	int i, e;
	
	e = pop(h);
	i = pop(h);
	if(h->g == nil)
		herror(h, "FLIPRG without glyph");
	for(; i <= e; i++)
		if((int)i < h->g->npt)
			h->g->pt[i].flags = h->g->pt[i].flags & ~1 | h->ip[-1] & 1;
}

static void
h_isect(Hint *h)
{
	int a0i, a1i, b0i, b1i, pi;
	TTPoint a0, a1, b0, b1, p;
	int n0x, n0y;
	vlong n0c;
	int n1x, n1y;
	vlong n1c;
	int Δ;
	
	a0i = pop(h);
	a1i = pop(h);
	b0i = pop(h);
	b1i = pop(h);
	pi = pop(h);
	a0 = getpoint(h, ZP0, a0i);
	a1 = getpoint(h, ZP0, a1i);
	b0 = getpoint(h, ZP1, b0i);
	b1 = getpoint(h, ZP1, b1i);
	p = getpoint(h, ZP2, pi);
	n0x = a1.y - a0.y;
	n0y = a0.x - a1.x;
	n0c = (vlong)n0x * a0.x + (vlong)n0y * a0.y;
	n1x = b1.y - b0.y;
	n1y = b0.x - b1.x;
	n1c = (vlong)n1x * b0.x + (vlong)n1y * b0.y;
	Δ = (vlong)n1x * n0y - (vlong)n0x * n1y;
	if(Δ == 0){
		p.x = ((a0.x + a1.x) / 2 + (b0.x + b1.x) / 2) / 2;
		p.y = ((a0.y + a1.y) / 2 + (b0.y + b1.y) / 2) / 2;
	}else{
		p.x = vrounddiv(n0y * n1c - n1y * n0c, Δ);
		p.y = vrounddiv(n1x * n0c - n0x * n1c, Δ);
	}
	p.flags |= TOUCH;
	setpoint(h, ZP2, pi, p);
}

static void
h_shp(Hint *h)
{
	int i;
	TTPoint rp, orp;
	int pi;
	TTPoint p, n;
	int d, dp;

	if((h->ip[-1] & 1) != 0){
		rp = getpoint(h, RP1|ZP0, 0);
		orp = getpoint(h, RP1|ZP0|ORIG, 0);
	}else{
		rp = getpoint(h, RP2|ZP1, 0);
		orp = getpoint(h, RP2|ZP1|ORIG, 0);
	}
	
	d = project(h, &rp, &orp);
	for(i = 0; i < h->f->loop; i++){
		pi = pop(h);
		p = getpoint(h, ZP2, pi);
		dp = project(h, &p, nil);
		n = forceproject(h, p, dp + d);
		setpoint(h, ZP2, pi, n);
	}
	h->f->loop = 1;
}

static void
h_shc(Hint *h)
{
	int i, c;
	int rpi;
	TTPoint rp, orp;
	TTPoint p, n;
	int d, dp;

	if((h->ip[-1] & 1) != 0){
		rpi = h->f->rp[1];
		if(((h->f->zp ^ h->f->zp >> 2) & 1) != 0)
			rpi = -1;
		rp = getpoint(h, RP1|ZP0, 0);
		orp = getpoint(h, RP1|ZP0|ORIG, 0);
	}else{
		rpi = h->f->rp[2];
		if(((h->f->zp ^ h->f->zp >> 1) & 1) != 0)
			rpi = -1;
		rp = getpoint(h, RP2|ZP1, 0);
		orp = getpoint(h, RP2|ZP1|ORIG, 0);
	}
	c = pop(h);
	if(h->g == nil)
		herror(h, "SHC[] outside of glyf program");
	if((uint)c >= h->g->ncon)
		herror(h, "contour %d out of range", c);
	d = project(h, &rp, &orp);
	for(i = h->g->confst[c]; i < h->g->confst[c+1]; i++){
		if(i == rpi) continue;
		p = getpoint(h, ZP2, i);
		dp = project(h, &p, nil);
		n = forceproject(h, p, dp + d);
		setpoint(h, ZP2, i, n);
	}
}

static void
h_shz(Hint *h)
{
	int i, e, np;
	TTPoint rp, orp;
	TTPoint p, n;
	int d, dp;

	if((h->ip[-1] & 1) != 0){
		rp = getpoint(h, RP1|ZP0, 0);
		orp = getpoint(h, RP1|ZP0|ORIG, 0);
	}else{
		rp = getpoint(h, RP2|ZP1, 0);
		orp = getpoint(h, RP2|ZP1|ORIG, 0);
	}
	e = pop(h);
	if((uint)e > 1)
		herror(h, "SHZ[] with invalid zone %d", e);
	d = project(h, &rp, &orp);
	np = e ? h->g->npt : h->f->u->maxTwilightPoints;
	for(i = 0; i < np; i++){
		p = getpointz(h, e, i);
		dp = project(h, &p, nil);
		n = forceproject(h, p, dp + d);
		setpointz(h, e, i, n);
	}
}

static void (*itable[256])(Hint *) = {
	[0x00] h_svtca, h_svtca, h_svtca, h_svtca, h_svtca, h_svtca,
	[0x06] h_sxvtl, h_sxvtl, h_sxvtl, h_sxvtl,
	[0x0a] h_spvfs,
	[0x0b] h_sfvfs,
	[0x0c] h_gpv,
	[0x0d] h_gfv,
	[0x0e] h_sfvtpv,
	[0x0f] h_isect,
	[0x10] h_srp, h_srp, h_srp,
	[0x13] h_szp, h_szp, h_szp, h_szp,
	[0x17] h_sloop,
	[0x18] h_roundst, h_roundst,
	[0x1a] h_smd,
	[0x1b] h_else,
	[0x1c] h_jmpr,
	[0x1d] h_scvtci,
	[0x1e] h_sswci,
	[0x1f] h_ssw,
	[0x20] h_dup,
	[0x21] h_pop,
	[0x22] h_clear,
	[0x23] h_swap,
	[0x24] h_depth,
	[0x25] h_cindex,
	[0x26] h_mindex,
	[0x2a] h_loopcall,
	[0x2b] h_call,
	[0x2c] h_fdef,
	[0x2e] h_mdap, h_mdap,
	[0x30] h_iup, h_iup,
	[0x32] h_shp, h_shp,
	[0x34] h_shc, h_shc,
	[0x36] h_shz, h_shz,
	[0x38] h_shpix,
	[0x39] h_ip,
	[0x3a] h_msirp, h_msirp,
	[0x3c] h_alignrp,
	[0x3d] h_roundst,
	[0x3e] h_miap, h_miap,
	[0x40] h_npushb,
	[0x41] h_npushw,
	[0x42] h_ws,
	[0x43] h_rs,
	[0x44] h_wcvtp,
	[0x45] h_rcvt,
	[0x46] h_gc0, h_gc1,
	[0x48] h_scfs,
	[0x49] h_md0, h_md1,
	[0x4b] h_mppem,
	[0x4d] h_fliponoff, h_fliponoff,
	[0x4f] h_nop,
	[0x50] h_binop, h_binop, h_binop, h_binop, h_binop, h_binop,
	[0x56] h_unop, h_unop,
	[0x58] h_if,
	[0x59] h_nop, /* endif */
	[0x5a] h_binop, h_binop,
	[0x5c] h_unop,
	[0x5d] h_deltap,
	[0x5e] h_sdb,
	[0x5f] h_sds,
	[0x60] h_binop, h_binop, h_binop, h_binop, h_unop, h_unop, h_unop, h_unop,
	[0x68] h_unop, h_unop, h_unop, h_unop, h_nop, h_nop, h_nop, h_nop,
	[0x70] h_wcvtf,
	[0x71] h_deltap, h_deltap,
	[0x73] h_deltac, h_deltac, h_deltac,
	[0x76] h_sround, h_sround,
	[0x78] h_jrcond, h_jrcond,
	[0x7a] h_roundst,
	[0x7c] h_roundst, h_roundst,
	[0x7e] h_pop,
	[0x7f] h_pop,
	[0x81] h_fliprg, h_fliprg,
	[0x85] h_scanctrl,
	[0x86] h_sdpvtl, h_sdpvtl,
	[0x88] h_getinfo,
	[0x8a] h_roll,
	[0x8b] h_binop, h_binop,
	[0x8d] h_scantype,
	[0x8e] h_instctrl,
	[0xb0] h_pushb, h_pushb, h_pushb, h_pushb,
	       h_pushb, h_pushb, h_pushb, h_pushb,
	[0xb8] h_pushw, h_pushw, h_pushw, h_pushw,
	       h_pushw, h_pushw, h_pushw, h_pushw,
	[0xc0] h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp,
	       h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp,
	       h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp,
	       h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp, h_mdrp,
	[0xe0] h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp,
	       h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp,
	       h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp,
	       h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp, h_mirp,
};

static int
pointfmt(Fmt *f)
{
	TTPoint p;
	
	p = va_arg(f->args, TTPoint);
	if((f->flags & FmtSign) != 0)
		return fmtprint(f, "(%.2f,%.2f,%d)", (float)p.x/64, (float)p.y/64, p.flags);
	else
		return fmtprint(f, "(%d,%d,%d)", p.x, p.y, p.flags);
}

static void
run(Hint *h)
{
	while(h->ip < h->ehint){
		if(debug) debugprint(h, 0);
		if(itable[*h->ip] == nil)
			sysfatal("unknown hint instruction %#.2x", *h->ip);
		else
			itable[*h->ip++](h);
	}
}

static int
runpg(TTFont *f, TTGlyph *g, uchar *buf, int n)
{
	Hint h;
	static int didfmt;

	if(debug && !didfmt){
		fmtinstall(L'π', pointfmt);
		didfmt = 1;
	}
	memset(&h, 0, sizeof(Hint));
	if(setjmp(h.jmp) != 0){
		errstr(h.err, sizeof(h.err));
		return -1;
	}
	h.g = g;
	h.f = f;
	h.stack = f->hintstack;
	h.nstack = f->u->maxStackElements;
	h.ip = h.shint = buf;
	h.ehint = buf + n;
	run(&h);
	return 0;
}

int
ttfhint(TTGlyph *g)
{
	int rc, i;

	if((g->font->defstate.instctrl & 1<<1) != 0)
		return 0;
	dprint("HINT:\n");
	if((g->font->defstate.instctrl & 1<<2) != 0)
		g->font->TTGState = defstate;
	else
		g->font->TTGState = g->font->defstate;
	rc = runpg(g->font, g, g->hint, g->nhint);
	if(debug && rc >= 0){
		for(i = 0; i < g->npt; i++)
			dprint("%d: %+π\n", i, g->pt[i]);
	}
	return rc;
}

int
ttfrunfpgm(TTFont *f)
{
	int len, rc;
	u8int *buf;

	f->TTGState = defstate;
	f->defstate = defstate;
	len = ttfgototable(f->u, "fpgm");
	if(len <= 0)
		return 0;
	buf = mallocz(len, 1);
	if(buf == nil)
		return -1;
	Bread(f->u->bin, buf, len);
	dprint("FPGM:\n");
	rc = runpg(f, nil, buf, len);
	free(buf);
	return rc;
}

int
ttfruncvt(TTFont *f)
{
	int len, rc;
	u8int *buf;

	f->TTGState = defstate;
	f->defstate = defstate;
	len = ttfgototable(f->u, "prep");
	if(len <= 0)
		return 0;
	buf = mallocz(len, 1);
	if(buf == nil)
		return -1;
	Bread(f->u->bin, buf, len);
	dprint("CVT:\n");
	rc = runpg(f, nil, buf, len);
	free(buf);
	if(rc >= 0){
		f->zp = 7;
		f->rp[0] = 0;
		f->rp[1] = 0;
		f->rp[2] = 0;
		f->loop = 1;
		f->rperiod = 64;
		f->rphase = 0;
		f->rthold = 32;
		f->fvx = 16384;
		f->fvy = 0;
		f->pvx = 16384;
		f->pvy = 0;
		f->dpvx = 16384;
		f->dpvy = 0;
		f->defstate = f->TTGState;
	}
	return rc;
}
