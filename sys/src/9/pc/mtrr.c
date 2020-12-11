#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum {
	/*
	 * MTRR Physical base/mask are indexed by
	 *	MTRRPhys{Base|Mask}N = MTRRPhys{Base|Mask}0 + 2*N
	 */
	MTRRPhysBase0 = 0x200,
	MTRRPhysMask0 = 0x201,

	MTRRDefaultType = 0x2FF,
		Deftype = 0xFF,		/* default MTRR type */
		Deffixena = 1<<10,	/* fixed-range MTRR enable */
		Defena	= 1<<11,	/* MTRR enable */

	MTRRCap = 0xFE,
		Capvcnt = 0xFF,		/* mask: # of variable-range MTRRs we have */
		Capwc = 1<<8,		/* flag: have write combining? */
		Capfix = 1<<10,		/* flag: have fixed MTRRs? */

	AMDK8SysCfg = 0xC0010010,
		Tom2Enabled = 1<<21,
		Tom2ForceMemTypeWB = 1<<22,

	AMDK8TopMem2 = 0xC001001D,
};

enum {
	Nvarreg = 8,
	Nfixreg = 11*8,
	Nranges = Nfixreg+Nvarreg*2+1,
};

typedef struct Varreg Varreg;
struct Varreg {
	vlong	base;
	vlong	mask;
};

typedef struct Fixreg Fixreg;
struct Fixreg {
	int	msr;
	ulong	base;
	ulong	size;
};

typedef struct State State;
struct State {
	uvlong	mask;
	vlong	cap;
	vlong	def;
	vlong	tom2;
	int	nvarreg;
	Varreg	varreg[Nvarreg];
	vlong	fixreg[Nfixreg/8];
};

typedef struct Range Range;
struct Range {
	uvlong	base;
	uvlong	size;
	int	type;
};

enum {
	Uncacheable	= 0,
	Writecomb	= 1,
	Unknown1	= 2,
	Unknown2	= 3,
	Writethru	= 4,
	Writeprot	= 5,
	Writeback	= 6,
};

static char *types[] = {
	[Uncacheable]	"uc",
	[Writecomb]	"wc",
	[Unknown1]	"uk1",
	[Unknown2]	"uk2",
	[Writethru]	"wt",
	[Writeprot]	"wp",
	[Writeback]	"wb",
};

static char *
type2str(int type)
{
	if(type < 0 || type >= nelem(types))
		return nil;
	return types[type];
}

static int
str2type(char *str)
{
	int type;

	for(type = 0; type < nelem(types); type++){
		if(strcmp(str, types[type]) == 0)
			return type;
	}
	return -1;
}

static int
getvarreg(State *s, Range *rp, int index)
{
	Varreg *reg = &s->varreg[index];

	if((reg->mask & (1<<11)) == 0)
		return 0;
	rp->base = reg->base & ~0xFFFULL;
	rp->type = reg->base & 0xFF;
	rp->size = (s->mask ^ (reg->mask & ~0xFFFULL)) + 1;
	return 1;
}

static void
setvarreg(State *s, Range *rp, int index)
{
	Varreg *reg = &s->varreg[index];

	if(rp == nil || rp->size == 0){
		reg->base = 0;
		reg->mask = 0;
		return;
	}
	reg->base = rp->base | (rp->type & 0xFF);
	reg->mask = (s->mask & ~(rp->size-1)) | 1<<11;
}

static Fixreg fixreg[Nfixreg/8] = {
	0x250, 0x00000, 0x10000,

	0x258, 0x80000, 0x04000,
	0x259, 0xA0000, 0x04000,

	0x268, 0xC0000, 0x01000,
	0x269, 0xC8000, 0x01000,
	0x26A, 0xD0000, 0x01000,
	0x26B, 0xD8000, 0x01000,
	0x26C, 0xE0000, 0x01000,
	0x26D, 0xE8000, 0x01000,
	0x26E, 0xF0000, 0x01000,
	0x26F, 0xF8000, 0x01000,
};

static int
getfixreg(State *s, Range *rp, int index)
{
	Fixreg *reg = &fixreg[index >> 3];

	index &= 7;
	rp->base = reg->base + reg->size * index;
	rp->size = reg->size;
	rp->type = ((uvlong)s->fixreg[reg - fixreg] >> 8*index) & 0xFF;
	return 1;
}

static void
setfixreg(State *s, Range *rp, int index)
{
	Fixreg *reg = &fixreg[index >> 3];
	int type;

	index &= 7;
	if(rp == nil || rp->size == 0)
		type = Uncacheable;
	else
		type = rp->type & 0xFF;
	s->fixreg[reg - fixreg] &= ~(0xFFULL << 8*index);
	s->fixreg[reg - fixreg] |= (uvlong)type << 8*index;
}

static int
preftype(int a, int b)
{
	if(a == b)
		return a;
	if(a == Uncacheable || b == Uncacheable)
		return Uncacheable;
	if(a == Writethru && b == Writeback
	|| a == Writeback && b == Writethru)
		return Writethru;
	return -1;
}

static int
gettype(State *s, uvlong pa, Range *new)
{
	int i, type;
	Range r;

	if(new != nil && pa >= new->base && pa < new->base + new->size)
		return new->type;

	if((s->def & Defena) == 0)
		return Uncacheable;

	if(pa < 0x100000 && (s->def & Deffixena) != 0){
		for(i = 0; i < Nfixreg; i++){
			if(getfixreg(s, &r, i) && pa < r.base + r.size && pa >= r.base)
				return r.type;
		}
	}

	if(pa >= 0x100000000ULL && pa < s->tom2)
		return Writeback;

	type = -1;
	for(i = 0; i < s->nvarreg; i++){
		if(!getvarreg(s, &r, i))
			continue;
		if((pa & -r.size) == r.base)
			type = (type == -1) ? r.type : preftype(r.type, type);
	}
	if(type == -1)
		type = s->def & Deftype;
	return type;
}

static uvlong
getnext(State *s, uvlong pa, Range *new)
{
	uvlong end;
	Range r;
	int i;

	if(new != nil){
		end = getnext(s, pa, nil);
		if(pa < new->base && end > new->base)
			return new->base;
		if(pa < new->base + new->size && end > new->base + new->size)
			return new->base + new->size;
		return end;
	}

	end = s->mask+1;
	if((s->def & Defena) == 0)
		return end;

	if(pa < 0x100000 && (s->def & Deffixena) != 0){
		for(i = 0; i < Nfixreg; i++){
			if(getfixreg(s, &r, i) && pa < r.base + r.size && pa >= r.base)
				return r.base + r.size;
		}
	}

	if(pa >= 0x100000000ULL && pa < s->tom2)
		return s->tom2;

	for(i = 0; i < s->nvarreg; i++){
		if(!getvarreg(s, &r, i))
			continue;
		if((pa & -r.size) == r.base)
			r.base += r.size;
		else if(r.base <= pa)
			continue;
		if(r.base < end)
			end = r.base;
	}

	if(pa < 0x100000000ULL && end > 0x100000000ULL)
		end = 0x100000000ULL;

	return end;
}

enum {
	Exthighfunc = 1ul << 31,
	Extprocsigamd,
	Extprocname0,
	Extprocname1,
	Extprocname2,
	Exttlbl1,
	Extl2,
	Extapm,
	Extaddrsz,
};

static uvlong
physmask(void)
{
	ulong regs[4];
	uvlong mask;

	cpuid(Exthighfunc, 0, regs);
	if(regs[0] >= Extaddrsz) {			/* ax */
		cpuid(Extaddrsz, 0, regs);
		mask = (1ULL << (regs[0] & 0xFF)) - 1;	/* ax */
	} else {
		mask = (1ULL << 36) - 1;
	}
	return mask;
}

static int
getstate(State *s)
{
	vlong v;
	int i;

	if(rdmsr(MTRRCap, &s->cap) < 0)
		return -1;

	if((s->cap & (Capfix|Capvcnt)) == 0)
		return -1;

	if(rdmsr(MTRRDefaultType, &s->def) < 0)
		return -1;

	if(s->cap & Capfix){
		for(i = 0; i < nelem(fixreg); i++){
			if(rdmsr(fixreg[i].msr, &s->fixreg[i]) < 0)
				return -1;
		}
	} else {
		s->def &= ~(vlong)Deffixena;
	}

	s->nvarreg = s->cap & Capvcnt;
	if(s->nvarreg > Nvarreg)
		s->nvarreg = Nvarreg;

	for(i = 0; i < s->nvarreg; i++){
		if(rdmsr(MTRRPhysBase0 + 2*i, &s->varreg[i].base) < 0)
			return -1;
		if(rdmsr(MTRRPhysMask0 + 2*i, &s->varreg[i].mask) < 0)
			return -1;
	}

	s->mask = physmask();

	if(strcmp(m->cpuidid, "AuthenticAMD") != 0
	|| m->cpuidfamily < 15
	|| rdmsr(AMDK8SysCfg, &v) < 0
	|| (v & (Tom2Enabled|Tom2ForceMemTypeWB)) != (Tom2Enabled|Tom2ForceMemTypeWB)
	|| rdmsr(AMDK8TopMem2, &s->tom2) < 0)
		s->tom2 = 0;
	else {
		s->tom2 &= s->mask;
		s->tom2 &= -0x800000LL;
	}

	return 0;
}

enum {
	CR4PageGlobalEnable = 1 << 7,
	CR0CacheDisable = 1 << 30,
};

static void
putstate(State *s)
{
	uintptr cr0, cr4;
	int i, x;

	x = splhi();

	/* disable cache */
	cr0 = getcr0();
	putcr0(cr0 | CR0CacheDisable);
	wbinvd();

	/* disable PGE */
	cr4 = getcr4();
	putcr4(cr4 & ~CR4PageGlobalEnable);

	/* flush tlb */
	putcr3(getcr3());

	/* disable MTRRs */
	wrmsr(MTRRDefaultType, s->def & ~(vlong)(Defena|Deffixena|Deftype));
	wbinvd();

	/* write all registers */
	if(s->cap & Capfix){
		for(i = 0; i < nelem(fixreg); i++)
			wrmsr(fixreg[i].msr, s->fixreg[i]);
	}
	for(i = 0; i < s->nvarreg; i++){
		wrmsr(MTRRPhysBase0 + 2*i, s->varreg[i].base);
		wrmsr(MTRRPhysMask0 + 2*i, s->varreg[i].mask);
	}

	/* flush tlb */
	putcr3(getcr3());

	/* enable MTRRs */
	wrmsr(MTRRDefaultType, s->def);

	/* reenable cache */
	putcr0(cr0);

	/* reenable PGE */
	putcr4(cr4);

	splx(x);
}

static int
fls64(uvlong x)
{
	int i;

	for(i = 0; i < 64; i++)
		if(x & (1ULL<<i))
			break;
	return i;
}

static int
fms64(uvlong x)
{
	int i;

	if(x == 0)
		return 0;
	for(i = 63; i >= 0; i--)
		if(x & (1ULL<<i))
			break;
	return i;
}

static int
range2varreg(State *s, Range r, int index, int doit)
{
	uvlong len;

	if(index < 0)
		return -1;

	if(r.base <= 0x100000 && (s->def & Deffixena) != 0){
		r.size += r.base;
		r.base = 0;
	}

	if(r.base >= 0x100000000ULL && r.base <= s->tom2){
		if(r.base + r.size <= s->tom2){
			if(r.type != Writeback)
				return -1;
			return index;
		}
	}

	len = r.size;
	while(len){
		if(index >= s->nvarreg)
			return -1;
		if(fls64(r.base) > fms64(len))
			r.size = 1ULL << fms64(len);
		else
			r.size = 1ULL << fls64(r.base);
		if(doit)
			setvarreg(s, &r, index);
		index++;
		len -= r.size;
		r.base += r.size;
	}
	return index;
}

static int ranges2varregs(State*, Range*, int, int, int);

/*
 * try to combine same type ranges that are split by
 * higher precedence ranges.
 */
static int
ranges2varregscomb(State *s, Range *rp, int nr, int index, int doit)
{
	Range rr;
	int i, j;

	if(nr < 2 || rp[0].type == rp[1].type)
		return -1;
	rr = rp[0];
	if(preftype(rr.type, rp[1].type) == rr.type)
		rr.type = rp[1].type;
	for(j = 1; j < nr; j++){
		if(rp[j].type != rr.type
		&& preftype(rp[j].type, rr.type) != rp[j].type)
			return -1;
		rr.size += rp[j].size;
	}
	i = ranges2varregs(s, &rr, 1, index, doit);
	for(j = 0; j < nr && i >= index; j++){
		if(rp[j].type != rr.type)
			i = range2varreg(s, rp[j], i, doit);
	}
	return i;
}

static int
ranges2varregs(State *s, Range *rp, int nr, int index, int doit)
{
	int i, j, k;

	if(nr == 1){
		if(rp->type == (s->def & Deftype))
			return index;
		return range2varreg(s, *rp, index, doit);
	}

	/* try combining */
	i = ranges2varregscomb(s, rp, nr, index, doit);

	/*
	 * now see if we can find a better solution using
	 * different splittings.
	 */
	for(k = 1; k < nr; k++){
		j = ranges2varregs(s, rp+k, nr-k,
			ranges2varregs(s, rp, k, index, 0), 0);
		if(j < 0)
			continue;
		if(i < 0 || j < i)
			i = doit ? ranges2varregs(s, rp+k, nr-k,
				ranges2varregs(s, rp, k, index, 1), 1) : j;
	}
	return i;
}

static int
range2fixreg(State *s, Range r)
{
	Range rr;
	int i;

	for(i = 0; i < Nfixreg; i++){
		if(!getfixreg(s, &rr, i) || rr.base + rr.size <= r.base)
			continue;
		if(rr.base >= r.base + r.size)
			break;
		if(r.base > rr.base || r.base + r.size < rr.base + rr.size)
			return -1;
		rr.type = r.type;
		setfixreg(s, &rr, i);
	}
	return 0;
}

static int
setranges(State *s, Range *rp, int nr)
{
	int i, j;

	if(nr < 1 || nr > Nranges)
		return -1;

	s->def &= ~(vlong)(Defena|Deffixena|Deftype);

	i = 0;
	if(rp[0].size != s->mask+1 || rp[0].type != Uncacheable){
		s->def |= Defena;

		/* first handle ranges below 1MB using fixed registers */
		if(rp[0].size < 0x100000 && (s->cap & Capfix) != 0){
			s->def |= Deffixena;

			for(i = 0; i < Nfixreg; i++)
				setfixreg(s, nil, i);

			while(nr > 0 && rp->base < 0x100000){
				if(range2fixreg(s, *rp) < 0)
					return -1;
				if(rp->base + rp->size > 0x100000)
					break;
				rp++;
				nr--;
			}
		}

		/* remaining ranges to to variable registers */
		if(nr > 0){
			/* make sure the algorithm doesnt explode */
			if(nr > Nvarreg+1)
				return -1;

			/* try with UC default type */
			s->def = (s->def & ~(vlong)Deftype) | Uncacheable;
			i = ranges2varregs(s, rp, nr, 0, 1);

			/* try with WB default type, dont do it yet */
			s->def = (s->def & ~(vlong)Deftype) | Writeback;
			j = ranges2varregs(s, rp, nr, 0, 0);
			if(j < 0 || (i >= 0 && i <= j)){
				/* WB not better or worse, use UC solution */
				s->def = (s->def & ~(vlong)Deftype) | Uncacheable;
			} else {
				/* WB default is better, doit! */
				i = ranges2varregs(s, rp, nr, 0, 1);
			}
			if(i < 0)
				return -1;
		}
	}

	/* clear unused variable registers */
	for(; i < s->nvarreg; i++)
		setvarreg(s, nil, i);

	return 0;
}

static int
checkranges(State *s, Range *rp, int nr)
{
	uvlong base, next;
	int i;

	for(i = 0; i < nr; i++){
		next = rp[i].base + rp[i].size;
		for(base = rp[i].base; base < next; base = getnext(s, base, nil)){
			if(gettype(s, base, nil) != rp[i].type)
				return -1;
		}
	}
	return 0;
}

static int
getranges(State *s, Range *rp, int nr, Range *new)
{
	uvlong base, next;
	Range *rs, *re;
	int type;

	rs = rp;
	re = rp + nr;
	for(base = 0; base <= s->mask; base = next) {
		if(rp >= re)
			return -1;
		type = gettype(s, base, new);
		next = getnext(s, base, new);
		while(next <= s->mask && (gettype(s, next, new) == type))
			next = getnext(s, next, new);
		rp->base = base;
		rp->size = next - base;
		rp->type = type;
		rp++;
	}
	return rp - rs;
}

static int dosync;
static QLock mtrrlk;
static State cpu0state;
static Range ranges[Nranges];

char*
mtrr(uvlong base, uvlong size, char *tstr)
{
	static State newstate;
	Range new;
	int nr;

	if(cpu0state.mask == 0)
		return "mtrr not supported";

	if(size < 0x1000)
		return "size too small";
	if((base | size) & 0xFFF)
		return "base or size not page aligned";
	if(base & ~cpu0state.mask)
		return "base out of range";
	if(base + size > cpu0state.mask+1)
		return "size out of range";

	new.base = base;
	new.size = size;
	if((new.type = str2type(tstr)) < 0)
		return "bad cache type";

	if(new.type == Writecomb
	&& (cpu0state.cap & Capwc) == 0)
		return "write combining not supported";

	qlock(&mtrrlk);
	newstate = cpu0state;
	nr = getranges(&newstate, ranges, Nranges, &new);
	if(setranges(&newstate, ranges, nr) < 0
	|| checkranges(&newstate, ranges, nr) < 0){
		qunlock(&mtrrlk);
		return "cache range not satisfiable";
	}
	cpu0state = newstate;
	coherence();
	dosync = 1;
	mtrrclock();
	qunlock(&mtrrlk);

	return nil;
}

char*
mtrrattr(uvlong pa, uvlong *pnext)
{
	if(cpu0state.mask == 0)
		return nil;
	if(pnext != nil)
		*pnext = getnext(&cpu0state, pa, nil);
	return type2str(gettype(&cpu0state, pa, nil));
}

int
mtrrprint(char *buf, long bufsize)
{
	char *cp, *ep;
	int i, nr;

	if(cpu0state.mask == 0)
		return 0;

	cp = buf;
	ep = buf + bufsize;

	qlock(&mtrrlk);
	nr = getranges(&cpu0state, ranges, Nranges, nil);
	for(i = 0; i < nr; i++){
		cp = seprint(cp, ep, "cache %#.16llux %15llud %s\n",
			ranges[i].base,
			ranges[i].size,
			type2str(ranges[i].type));
	}
	qunlock(&mtrrlk);

	return cp - buf;
}

/* called from clock interrupt */
void
mtrrclock(void)
{
	static Ref bar1, bar2;
	int x;

	if(dosync == 0 || cpu0state.mask == 0)
		return;

	x = splhi();

	/*
	 * wait for all CPUs to sync here, so that the MTRR setup gets
	 * done at roughly the same time on all processors.
	 */
	incref(&bar1);
	while(bar1.ref < conf.nmach)
		microdelay(10);

	putstate(&cpu0state);

	/*
	 * wait for all CPUs to sync up again, so that we don't continue
	 * executing while the MTRRs are still being set up.
	 */
	incref(&bar2);
	while(bar2.ref < conf.nmach)
		microdelay(10);
	decref(&bar1);
	while(bar1.ref > 0)
		microdelay(10);
	decref(&bar2);

	dosync = 0;
	splx(x);
}

/* called from cpuidentify() */
void
mtrrsync(void)
{
	State s;

	if(getstate(&s) < 0)
		return;
	if(cpu0state.mask == 0){
		cpu0state = s;
		coherence();
		return;
	}
	putstate(&cpu0state);
}
