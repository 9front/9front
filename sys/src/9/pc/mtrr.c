/*
 * memory-type region registers.
 *
 * due to the possibility of extended addresses (for PAE)
 * as large as 36 bits coming from the e820 memory map and the like,
 * we'll use vlongs to hold addresses and lengths, even though we don't
 * implement PAE in Plan 9.
 */
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
	MTRRCap = 0xFE,
	Nmtrr = 8,

	/* cpuid extended function codes */
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

enum {
	CR4PageGlobalEnable	= 1 << 7,
	CR0CacheDisable		= 1 << 30,
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

enum {
	Capvcnt = 0xff,		/* mask: # of variable-range MTRRs we have */
	Capwc	= 1<<8,		/* flag: have write combining? */
	Capfix	= 1<<10,	/* flag: have fixed MTRRs? */
	Deftype = 0xff,		/* default MTRR type */
	Deffixena = 1<<10,	/* fixed-range MTRR enable */
	Defena	= 1<<11,	/* MTRR enable */
};

typedef struct Mtrreg Mtrreg;
typedef struct Mtrrop Mtrrop;

struct Mtrreg {
	vlong	base;
	vlong	mask;
};

static char *types[] = {
[Uncacheable]	"uc",
[Writecomb]	"wc",
[Unknown1]	"uk1",
[Unknown2]	"uk2",
[Writethru]	"wt",
[Writeprot]	"wp",
[Writeback]	"wb",
		nil
};

static int dosync;
static Mtrreg mtrreg[Nmtrr];

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
	char **p;

	for(p = types; *p != nil; p++)
		if (strcmp(str, *p) == 0)
			return p - types;
	return -1;
}

static uvlong
physmask(void)
{
	ulong regs[4];
	static vlong mask = -1;

	if (mask != -1)
		return mask;
	cpuid(Exthighfunc, regs);
	if(regs[0] >= Extaddrsz) {			/* ax */
		cpuid(Extaddrsz, regs);
		mask = (1LL << (regs[0] & 0xFF)) - 1;	/* ax */
	} else {
		mask &= (1LL << 36) - 1;
	}
	return mask;
}

static int
ispow2(uvlong ul)
{
	return (ul & (ul - 1)) == 0;
}

/* true if mtrr is valid */
static int
mtrrdec(Mtrreg *mtrr, uvlong *ptr, uvlong *size, int *type)
{
	*ptr =  mtrr->base & ~(BY2PG-1);
	*type = mtrr->base & 0xff;
	*size = (physmask() ^ (mtrr->mask & ~(BY2PG-1))) + 1;
	return (mtrr->mask >> 11) & 1;
}

static void
mtrrenc(Mtrreg *mtrr, uvlong ptr, uvlong size, int type, int ok)
{
	mtrr->base = ptr | (type & 0xff);
	mtrr->mask = (physmask() & ~(size - 1)) | (ok? 1<<11: 0);
}

/*
 * i is the index of the MTRR, and is multiplied by 2 because
 * mask and base offsets are interleaved.
 */
static void
mtrrget(Mtrreg *mtrr, uint i)
{
	rdmsr(MTRRPhysBase0 + 2*i, &mtrr->base);
	rdmsr(MTRRPhysMask0 + 2*i, &mtrr->mask);
}

static void
mtrrput(Mtrreg *mtrr, uint i)
{
	wrmsr(MTRRPhysBase0 + 2*i, mtrr->base);
	wrmsr(MTRRPhysMask0 + 2*i, mtrr->mask);
}

static int
mtrrvcnt(void)
{
	vlong cap;
	int vcnt;

	rdmsr(MTRRCap, &cap);
	vcnt = cap & Capvcnt;
	if(vcnt > Nmtrr)
		vcnt = Nmtrr;
	return vcnt;
}

static int
mtrrgetall(void)
{
	int i, vcnt;

	vcnt = mtrrvcnt();
	for(i = 0; i < vcnt; i++)
		mtrrget(&mtrreg[i], i);
	return vcnt;
}

static void
mtrrputall(void)
{
	int s, i, vcnt;
	ulong cr0, cr4;
	vlong def;

	s = splhi();

	cr4 = getcr4();
	putcr4(cr4 & ~CR4PageGlobalEnable);
	cr0 = getcr0();
	wbinvd();
	putcr0(cr0 | CR0CacheDisable);
	wbinvd();
	rdmsr(MTRRDefaultType, &def);
	wrmsr(MTRRDefaultType, def & ~(vlong)Defena);

	vcnt = mtrrvcnt();
	for(i=0; i<vcnt; i++)
		mtrrput(&mtrreg[i], i);

	wbinvd();
	wrmsr(MTRRDefaultType, def);
	putcr0(cr0);
	putcr4(cr4);

	splx(s);
}

void
mtrrclock(void)				/* called from clock interrupt */
{
	static Ref bar1, bar2;
	int s;

	if(dosync == 0)
		return;

	s = splhi();

	/*
	 * wait for all CPUs to sync here, so that the MTRR setup gets
	 * done at roughly the same time on all processors.
	 */
	incref(&bar1);
	while(bar1.ref < conf.nmach)
		microdelay(10);

	mtrrputall();

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
	splx(s);
}

static char*
mtrr0(uvlong base, uvlong size, char *tstr)
{
	int i, vcnt, slot, type, mtype, mok;
	vlong def, cap;
	uvlong mp, msize;

	if(!(m->cpuiddx & Mtrr))
		return "mtrrs not supported";
	if(base & (BY2PG-1) || size & (BY2PG-1) || size == 0)
		return "mtrr base or size not 4k aligned or zero size";
	if(!ispow2(size))
		return "mtrr size not power of 2";
	if(base & (size - 1))
		return "mtrr base not naturally aligned";

	if((type = str2type(tstr)) == -1)
		return "mtrr bad type";

	rdmsr(MTRRCap, &cap);
	rdmsr(MTRRDefaultType, &def);

	switch(type){
	default:
		return "mtrr unknown type";
	case Writecomb:
		if(!(cap & Capwc))
			return "mtrr type wc (write combining) unsupported";
		/* fallthrough */
	case Uncacheable:
	case Writethru:
	case Writeprot:
	case Writeback:
		break;
	}

	vcnt = mtrrgetall();

	slot = -1;
	for(i = 0; i < vcnt; i++){
		mok = mtrrdec(&mtrreg[i], &mp, &msize, &mtype);
		if(slot == -1 && (!mok || mtype == (def & Deftype)))
			slot = i;	/* good, but look further for exact match */
		if(mok && mp == base && msize == size){
			slot = i;
			break;
		}
	}
	if(slot == -1)
		return "no free mtrr slots";

	mtrrenc(&mtrreg[slot], base, size, type, 1);

	coherence();

	dosync = 1;
	mtrrclock();

	return nil;
}

char*
mtrr(uvlong base, uvlong size, char *tstr)
{
	static QLock mtrrlk;
	char *err;

	qlock(&mtrrlk);
	err = mtrr0(base, size, tstr);
	qunlock(&mtrrlk);

	return err;
}

int
mtrrprint(char *buf, long bufsize)
{
	int i, n, vcnt, type;
	uvlong base, size;
	Mtrreg mtrr;
	vlong def;

	if(!(m->cpuiddx & Mtrr))
		return 0;
	rdmsr(MTRRDefaultType, &def);
	n = snprint(buf, bufsize, "cache default %s\n",
		type2str(def & Deftype));
	vcnt = mtrrvcnt();
	for(i = 0; i < vcnt; i++){
		mtrrget(&mtrr, i);
		if (mtrrdec(&mtrr, &base, &size, &type))
			n += snprint(buf+n, bufsize-n,
				"cache 0x%llux %llud %s\n",
				base, size, type2str(type));
	}
	return n;
}

void
mtrrsync(void)
{
	static vlong cap0, def0;
	vlong cap, def;

	rdmsr(MTRRCap, &cap);
	rdmsr(MTRRDefaultType, &def);

	if(m->machno == 0){
		cap0 = cap;
		def0 = def;
		mtrrgetall();
		return;
	}

	if(cap0 != cap)
		print("mtrrcap%d: %lluX %lluX\n",
			m->machno, cap0, cap);
	if(def0 != def)
		print("mtrrdef%d: %lluX %lluX\n",
			m->machno, def0, def);
	mtrrputall();
}
