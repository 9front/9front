/*
 * this doesn't attempt to implement ARM floating-point properties
 * that aren't visible in the Inferno environment.
 * all arithmetic is done in double precision.
 * the FP trap status isn't updated.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

#include	"ureg.h"

#include	"arm.h"
#include	"fpi.h"

/* undef this if correct kernel r13 isn't in Ureg;
 * check calculation in fpiarm below
 */
#define	REG(ur, x) (*(long*)(((char*)(ur))+roff[(x)]))
#define	FR(ufp, x) (*(Internal*)(ufp)->regs[(x)&(Nfpctlregs - 1)])

typedef struct FP2 FP2;
typedef struct FP1 FP1;

struct FP2 {
	char*	name;
	void	(*f)(Internal, Internal, Internal*);
};

struct FP1 {
	char*	name;
	void	(*f)(Internal*, Internal*);
};

enum {
	N = 1<<31,
	Z = 1<<30,
	C = 1<<29,
	V = 1<<28,
	REGPC = 15,
};

enum {
	fpemudebug = 0,
};

#undef OFR
#define	OFR(X)	((ulong)&((Ureg*)0)->X)

static	int	roff[] = {
	OFR(r0), OFR(r1), OFR(r2), OFR(r3),
	OFR(r4), OFR(r5), OFR(r6), OFR(r7),
	OFR(r8), OFR(r9), OFR(r10), OFR(r11),
	OFR(r12), OFR(r13), OFR(r14), OFR(pc),
};

static Internal fpconst[8] = {		/* indexed by op&7 (ARM 7500 FPA) */
	/* s, e, l, h */
	{0, 0x1, 0x00000000, 0x00000000}, /* 0.0 */
	{0, 0x3FF, 0x00000000, 0x08000000},	/* 1.0 */
	{0, 0x400, 0x00000000, 0x08000000},	/* 2.0 */
	{0, 0x400, 0x00000000, 0x0C000000},	/* 3.0 */
	{0, 0x401, 0x00000000, 0x08000000},	/* 4.0 */
	{0, 0x401, 0x00000000, 0x0A000000},	/* 5.0 */
	{0, 0x3FE, 0x00000000, 0x08000000},	/* 0.5 */
	{0, 0x402, 0x00000000, 0x0A000000},	/* 10.0 */
};

/*
 * arm binary operations
 */

static void
fadd(Internal m, Internal n, Internal *d)
{
	(m.s == n.s? fpiadd: fpisub)(&m, &n, d);
}

static void
fsub(Internal m, Internal n, Internal *d)
{
	m.s ^= 1;
	(m.s == n.s? fpiadd: fpisub)(&m, &n, d);
}

static void
fsubr(Internal m, Internal n, Internal *d)
{
	n.s ^= 1;
	(n.s == m.s? fpiadd: fpisub)(&n, &m, d);
}

static void
fmul(Internal m, Internal n, Internal *d)
{
	fpimul(&m, &n, d);
}

static void
fdiv(Internal m, Internal n, Internal *d)
{
	fpidiv(&m, &n, d);
}

static void
fdivr(Internal m, Internal n, Internal *d)
{
	fpidiv(&n, &m, d);
}

/*
 * arm unary operations
 */

static void
fmov(Internal *m, Internal *d)
{
	*d = *m;
}

static void
fmovn(Internal *m, Internal *d)
{
	*d = *m;
	d->s ^= 1;
}

static void
fabsf(Internal *m, Internal *d)
{
	*d = *m;
	d->s = 0;
}

static void
frnd(Internal *m, Internal *d)
{
	short e;

	(m->s? fsub: fadd)(fpconst[6], *m, d);
	if(IsWeird(d))
		return;
	fpiround(d);
	e = (d->e - ExpBias) + 1;
	if(e <= 0)
		SetZero(d);
	else if(e > FractBits){
		if(e < 2*FractBits)
			d->l &= ~((1<<(2*FractBits - e))-1);
	}else{
		d->l = 0;
		if(e < FractBits)
			d->h &= ~((1<<(FractBits-e))-1);
	}
}

/*
 * ARM 7500 FPA opcodes
 */

static	FP1	optab1[16] = {	/* Fd := OP Fm */
[0]	{"MOVF",	fmov},
[1]	{"NEGF",	fmovn},
[2]	{"ABSF",	fabsf},
[3]	{"RNDF",	frnd},
[4]	{"SQTF",	/*fsqt*/0},
/* LOG, LGN, EXP, SIN, COS, TAN, ASN, ACS, ATN all `deprecated' */
/* URD and NRM aren't implemented */
};

static	FP2	optab2[16] = {	/* Fd := Fn OP Fm */
[0]	{"ADDF",	fadd},
[1]	{"MULF",	fmul},
[2]	{"SUBF",	fsub},
[3]	{"RSUBF",	fsubr},
[4]	{"DIVF",	fdiv},
[5]	{"RDIVF",	fdivr},
/* POW, RPW deprecated */
[8]	{"REMF",	/*frem*/0},
[9]	{"FMF",	fmul},	/* fast multiply */
[10]	{"FDV",	fdiv},	/* fast divide */
[11]	{"FRD",	fdivr},	/* fast reverse divide */
/* POL deprecated */
};

/*
 * ARM VFP opcodes
 */

static	FP1	voptab1[32] = {	/* Vd := OP Vm */
[0]	{"MOVF",	fmov},
[1]	{"ABSF",	fabsf},
[2]	{"NEGF",	fmovn},
[15]	{"CVTF",	fmov},
};

static	FP2	voptab2[16] = {	/* Vd := Vn FOP Fm */
[4]	{"MULF",	fmul},
[6]	{"ADDF",	fadd},
[7]	{"SUBF",	fsub},
[8]	{"DIVF",	fdiv},
};

static ulong
fcmp(Internal *n, Internal *m)
{
	int i;
	Internal rm, rn;

	if(IsWeird(m) || IsWeird(n)){
		/* BUG: should trap if not masked */
		return V|C;
	}
	rn = *n;
	rm = *m;
	fpiround(&rn);
	fpiround(&rm);
	i = fpicmp(&rn, &rm);
	if(i > 0)
		return C;
	else if(i == 0)
		return C|Z;
	else
		return N;
}

static void
fld(void (*f)(Internal*, void*), int d, ulong ea, int n, FPsave *ufp)
{
	void *mem;

	mem = (void*)ea;
	(*f)(&FR(ufp, d), mem);
	if(fpemudebug)
		print("MOV%c #%lux, F%d\n", n==8? 'D': 'F', ea, d);
}

static void
fst(void (*f)(void*, Internal*), ulong ea, int s, int n, FPsave *ufp)
{
	Internal tmp;
	void *mem;

	mem = (void*)ea;
	tmp = FR(ufp, s);
	if(fpemudebug)
		print("MOV%c	F%d,#%lux\n", n==8? 'D': 'F', s, ea);
	(*f)(mem, &tmp);
}

static int
condok(int cc, int c)
{
	switch(c){
	case 0:	/* Z set */
		return cc&Z;
	case 1:	/* Z clear */
		return (cc&Z) == 0;
	case 2:	/* C set */
		return cc&C;
	case 3:	/* C clear */
		return (cc&C) == 0;
	case 4:	/* N set */
		return cc&N;
	case 5:	/* N clear */
		return (cc&N) == 0;
	case 6:	/* V set */
		return cc&V;
	case 7:	/* V clear */
		return (cc&V) == 0;
	case 8:	/* C set and Z clear */
		return cc&C && (cc&Z) == 0;
	case 9:	/* C clear or Z set */
		return (cc&C) == 0 || cc&Z;
	case 10:	/* N set and V set, or N clear and V clear */
		return (~cc&(N|V))==0 || (cc&(N|V)) == 0;
	case 11:	/* N set and V clear, or N clear and V set */
		return (cc&(N|V))==N || (cc&(N|V))==V;
	case 12:	/* Z clear, and either N set and V set or N clear and V clear */
		return (cc&Z) == 0 && ((~cc&(N|V))==0 || (cc&(N|V))==0);
	case 13:	/* Z set, or N set and V clear or N clear and V set */
		return (cc&Z) || (cc&(N|V))==N || (cc&(N|V))==V;
	case 14:	/* always */
		return 1;
	case 15:	/* never (reserved) */
		return 0;
	}
	return 0;	/* not reached */
}

static void
unimp(ulong pc, ulong op)
{
	char buf[60];

	snprint(buf, sizeof(buf), "sys: fp: pc=%lux unimp fp 0x%.8lux", pc, op);
	if(fpemudebug)
		print("FPE: %s\n", buf);
	error(buf);
	/* no return */
}

static void
fpaemu(ulong pc, ulong op, Ureg *ur, FPsave *ufp)
{
	int rn, rd, tag, o;
	long off;
	ulong ea;
	Internal tmp, *fm, *fn;

	/* note: would update fault status here if we noted numeric exceptions */

	/*
	 * LDF, STF; 10.1.1
	 */
	if(((op>>25)&7) == 6){
		if(op & (1<<22))
			unimp(pc, op);	/* packed or extended */
		rn = (op>>16)&0xF;
		off = (op&0xFF)<<2;
		if((op & (1<<23)) == 0)
			off = -off;
		ea = REG(ur, rn);
		if(rn == REGPC)
			ea += 8;
		if(op & (1<<24))
			ea += off;
		rd = (op>>12)&7;
		if(op & (1<<20)){
			if(op & (1<<15))
				fld(fpid2i, rd, ea, 8, ufp);
			else
				fld(fpis2i, rd, ea, 4, ufp);
		}else{
			if(op & (1<<15))
				fst(fpii2d, ea, rd, 8, ufp);
			else
				fst(fpii2s, ea, rd, 4, ufp);
		}
		if((op & (1<<24)) == 0)
			ea += off;
		if(op & (1<<21))
			REG(ur, rn) = ea;
		return;
	}

	/*
	 * CPRT/transfer, 10.3
	 */
	if(op & (1<<4)){
		rd = (op>>12) & 0xF;

		/*
		 * compare, 10.3.1
		 */
		if(rd == 15 && op & (1<<20)){
			rn = (op>>16)&7;
			fn = &FR(ufp, rn);
			if(op & (1<<3)){
				fm = &fpconst[op&7];
				if(fpemudebug)
					tag = 'C';
			}else{
				fm = &FR(ufp, op&7);
				if(fpemudebug)
					tag = 'F';
			}
			switch((op>>21)&7){
			default:
				unimp(pc, op);
			case 4:	/* CMF: Fn :: Fm */
			case 6:	/* CMFE: Fn :: Fm (with exception) */
				ur->psr &= ~(N|C|Z|V);
				ur->psr |= fcmp(fn, fm);
				break;
			case 5:	/* CNF: Fn :: -Fm */
			case 7:	/* CNFE: Fn :: -Fm (with exception) */
				tmp = *fm;
				tmp.s ^= 1;
				ur->psr &= ~(N|C|Z|V);
				ur->psr |= fcmp(fn, &tmp);
				break;
			}
			if(fpemudebug)
				print("CMPF	%c%d,F%ld =%#lux\n",
					tag, rn, op&7, ur->psr>>28);
			return;
		}

		/*
		 * other transfer, 10.3
		 */
		switch((op>>20)&0xF){
		default:
			unimp(pc, op);
		case 0:	/* FLT */
			rn = (op>>16) & 7;
			fpiw2i(&FR(ufp, rn), &REG(ur, rd));
			if(fpemudebug)
				print("MOVW[FD]	R%d, F%d\n", rd, rn);
			break;
		case 1:	/* FIX */
			if(op & (1<<3))
				unimp(pc, op);
			rn = op & 7;
			tmp = FR(ufp, rn);
			fpii2w(&REG(ur, rd), &tmp);
			if(fpemudebug)
				print("MOV[FD]W	F%d, R%d =%ld\n", rn, rd, REG(ur, rd));
			break;
		case 2:	/* FPSR := Rd */
			ufp->status = REG(ur, rd);
			if(fpemudebug)
				print("MOVW	R%d, FPSR\n", rd);
			break;
		case 3:	/* Rd := FPSR */
			REG(ur, rd) = ufp->status;
			if(fpemudebug)
				print("MOVW	FPSR, R%d\n", rd);
			break;
		case 4:	/* FPCR := Rd */
			ufp->control = REG(ur, rd);
			if(fpemudebug)
				print("MOVW	R%d, FPCR\n", rd);
			break;
		case 5:	/* Rd := FPCR */
			REG(ur, rd) = ufp->control;
			if(fpemudebug)
				print("MOVW	FPCR, R%d\n", rd);
			break;
		}
		return;
	}

	/*
	 * arithmetic
	 */

	if(op & (1<<3)){	/* constant */
		fm = &fpconst[op&7];
		if(fpemudebug)
			tag = 'C';
	}else{
		fm = &FR(ufp, op&7);
		if(fpemudebug)
			tag = 'F';
	}
	rd = (op>>12)&7;
	o = (op>>20)&0xF;
	if(op & (1<<15)){	/* monadic */
		FP1 *fp;
		fp = &optab1[o];
		if(fp->f == nil)
			unimp(pc, op);
		if(fpemudebug)
			print("%s	%c%ld,F%d\n", fp->name, tag, op&7, rd);
		(*fp->f)(fm, &FR(ufp, rd));
	} else {
		FP2 *fp;
		fp = &optab2[o];
		if(fp->f == nil)
			unimp(pc, op);
		rn = (op>>16)&7;
		if(fpemudebug)
			print("%s	%c%ld,F%d,F%d\n", fp->name, tag, op&7, rn, rd);
		(*fp->f)(*fm, FR(ufp, rn), &FR(ufp, rd));
	}
}

static void
vfpoptoi(Internal *fm, uchar o2, uchar o4)
{
	fm->s = o2>>3;
	fm->e = ((o2>>3) | ~(o2 & 4)) - 3 + ExpBias;
	fm->l = 0;
	fm->h = o4 << (20+NGuardBits);
	if(fm->e)
		fm->h |= HiddenBit;
	else
		fm->e++;
}

static void
vfpemu(ulong pc, ulong op, Ureg *ur, FPsave *ufp)
{
	int sz, vd, o1, o2, o3, o4, o, tag;
	long off;
	ulong ea;
	Word w;
	
	Internal *fm, fc;

	/* note: would update fault status here if we noted numeric exceptions */

	sz = op & (1<<8);
	o1 = (op>>20) & 0xF;
	o2 = (op>>16) & 0xF;
	vd = (op>>12) & 0xF;

	switch((op>>24) & 0xF){
	default:
		unimp(pc, op);
	case 0xD:
		/* 
		 * Extension Register load/store A7.6 
		 */
		off = (op&0xFF)<<2;
		if((op & (1<<23)) == 0)
			off = -off;
		ea = REG(ur, o2) + off;
		switch(o1&0x7){	/* D(Bit 22) = 0 (5l) */
		default:
			unimp(pc, op);
		case 0:
			if(sz)
				fst(fpii2d, ea, vd, sz, ufp);
			else
				fst(fpii2s, ea, vd, sz, ufp);
			break;
		case 1:
			if(sz)
				fld(fpid2i, vd, ea, sz, ufp);
			else
				fld(fpis2i, vd, ea, sz, ufp);
			break;
		}
		break;
	case 0xE:
		if(op & (1<<4)){
			/* 
			 * Register transfer between Core & Extension A7.8
			 */
			if(sz)	/* C(Bit 8) != 0 */
				unimp(pc, op);
			switch(o1){
			default:
				unimp(pc, op);
			case 0:	/* Fn := Rt */
				*((Word*)&FR(ufp, o2)) = REG(ur, vd);
				if(fpemudebug)
					print("MOVWF	R%d, F%d\n", vd, o2);
				break;
			case 1:	/* Rt := Fn */
				REG(ur, vd) = *((Word*)&FR(ufp, o2));
				if(fpemudebug)
					print("MOVFW	F%d, R%d =%ld\n", o2, vd, REG(ur, vd));
				break;
			case 0xE:	/* FPSCR := Rt */
				ufp->status = REG(ur, vd);
				if(fpemudebug)
					print("MOVW	R%d, FPSCR\n", vd);
				break;
			case 0xF:	/* Rt := FPSCR */
				if(vd == 0xF){
					ur->psr = ufp->status;
					if(fpemudebug)
						print("MOVW	FPSCR, PSR\n");
				}else{
					REG(ur, vd) = ufp->status;
					if(fpemudebug)
						print("MOVW	FPSCR, R%d\n", vd);
				}
				break;
			}
		}
		else{
			/*
			 * VFP data processing instructions A7.5
			 * Note: As per 5l we ignore (D, N, M) bits
			 */
			if(fpemudebug)
				tag = 'F';
			o3 = (op>>6) & 0x3;
			o4 = op & 0xF;
			fm = &FR(ufp, o4);

			if(o1 == 0xB){	/* A7-17 */
				if(o3 & 0x1){
					switch(o2){
					default:
						o = (o2<<1) | (o3>>1);
						break;
					case 0x8:	/* CVT int -> float/double */
						w = *((Word*)fm);
						fpiw2i(&FR(ufp, vd), &w);
						if(fpemudebug)
							print("CVTW%c	F%d, F%d\n", sz?'D':'F', o4, vd);
						return;
					case 0xD:	/* CVT float/double -> int */
						fpii2w(&w, fm);
						*((Word*)&FR(ufp, vd)) = w;
						if(fpemudebug)
							print("CVT%cW	F%d, F%d\n", sz?'D':'F', o4, vd);
						return;
					case 0x5:	/* CMPF(E) */
							fm = &fpconst[0];
							if(fpemudebug)
								tag = 'C';
					case 0x4:	/* CMPF(E) */
						ufp->status &= ~(N|C|Z|V);
						ufp->status |= fcmp(&FR(ufp, vd), fm);
						if(fpemudebug)
							print("CMPF	%c%d,F%d =%#lux\n",
									tag, (o2&0x1)? 0: o4, vd, ufp->status>>28);
						return;
					}
				}else{	/* VMOV imm (VFPv3 & v4) (5l doesn't generate) */
					vfpoptoi(&fc, o2, o4);
					fm = &fc;
					o = 0;
					if(fpemudebug)
						tag = 'C';
				}
				FP1 *vfp;
				vfp = &voptab1[o];
				if(vfp->f == nil)
					unimp(pc, op);
				if(fpemudebug)
					print("%s	%c%d,F%d\n", vfp->name, tag, o4, vd);
				(*vfp->f)(fm, &FR(ufp, vd));
			}
			else {	/* A7-16 */
				FP2 *vfp;
				o = ((o1&0x3)<<1) | (o1&0x8) | (o3&0x1);
				vfp = &voptab2[o];
				if(vfp->f == nil)
					unimp(pc, op);
				if(fpemudebug)
					print("%s	F%d,F%d,F%d\n", vfp->name, o4, o2, vd);
				(*vfp->f)(*fm, FR(ufp, o2), &FR(ufp, vd));
			}
		}
		break;
	}
}


/*
 * returns the number of FP instructions emulated
 */
int
fpiarm(Ureg *ur)
{
	ulong op, o, cp;
	FPsave *ufp;
	int n;
	void (*fpemu)(ulong , ulong , Ureg *, FPsave *);

	if(up == nil)
		panic("fpiarm not in a process");
	ufp = up->fpsave;
	/*
	 * because all the emulated fp state is in the proc structure,
	 * it need not be saved/restored
	 */
	switch(up->fpstate){
	case FPactive:
	case FPinactive:
		error("illegal instruction: emulated fpu opcode in VFP mode");
	case FPinit:
		assert(sizeof(Internal) <= sizeof(ufp->regs[0]));
		up->fpstate = FPemu;
		ufp->control = 0;
		ufp->status = (0x01<<28)|(1<<12); /* sw emulation, alt. C flag */
		for(n = 0; n < Nfpctlregs; n++)
			FR(ufp, n) = fpconst[0];
	}
	for(n=0; ;n++){
		validaddr(ur->pc, 4, 0);
		op = *(ulong*)(ur->pc);
		if(fpemudebug)
			print("%#lux: %#8.8lux ", ur->pc, op);
		o = (op>>24) & 0xF;
		cp = (op>>8) & 0xF;
		if(ISFPAOP(cp, o))
			fpemu = fpaemu;
		else if(ISVFPOP(cp, o))
			fpemu = vfpemu;
		else
			break;
		if(condok(ur->psr, op>>28))
			fpemu(ur->pc, op, ur, ufp);
		
		ur->pc += 4;		/* pretend cpu executed the instr */
	}
	if(fpemudebug)
		print("\n");
	return n;
}
