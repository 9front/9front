#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <mach.h>
#include "dat.h"
#include "fns.h"
#include "x86.h"

typedef struct VMemReq VMemReq;
struct VMemReq {
	QLock;
	uintptr va, len;
	void *buf;
	uintptr rc;
	int wr;
};

static uintptr
translateflat(uintptr va, uintptr *pa, int *perm)
{
	if(sizeof(uintptr) != 4 && va >> 32 != 0) return 0;
	*pa = va;
	if(va == 0)
		return 0xFFFFFFFFUL;
	if(perm != 0) *perm = -1;
	return -va;
}

static uintptr
translate32(uintptr va, uintptr *pa, int *perm)
{
	void *pd, *pt;
	u32int pde, pte;

	if(sizeof(uintptr) != 4 && va >> 32 != 0) return -1;
	pd = gptr(rget("cr3") & ~0xfff, 4096);
	if(pd == nil) return 0;
	pde = GET32(pd, (va >> 22) * 4);
	if(perm != nil) *perm = pde;
	if((pde & 1) == 0) return 0;
	if((pde & 0x80) != 0 && (rget("cr4real") & Cr4Pse) != 0){
		*pa = pde & 0xffc00000 | (uintptr)(pde & 0x3fe000) << 19 | va & 0x3fffff;
		return 0x400000 - (va & 0x3fffff);
	}
	pt = gptr(pde & ~0xfff, 4096);
	if(pt == nil) return 0;
	pte = GET32(pt, va >> 10 & 0xffc);
	if((pte & 1) == 0) return 0;
	if(perm != nil) *perm &= pte;
	*pa = pte & ~0xfff | va & 0xfff;
	return 0x1000 - (va & 0xfff);
}

static uintptr
translatepae(uintptr, uintptr *, int *)
{
	vmerror("PAE translation not implemented");
	return 0;
}

static uintptr
translate64(uintptr va, uintptr *pa, int *perm)
{
	void *pml4, *pdp, *pd, *pt;
	u64int pml4e, pdpe, pde, pte;

	pml4 = gptr(rget("cr3") & 0xffffffffff000ULL, 4096);
	if(pml4 == nil) return 0;
	pml4e = GET64(pml4, (va & (511ULL<<39)) >> (39-3));
	if(perm != nil) *perm = pml4e & 15;
	if((pml4e & 1) == 0) return 0;

	pdp = gptr(pml4e & 0xffffffffff000ULL, 4096);
	if(pdp == nil) return 0;
	pdpe = GET64(pdp, (va & (511ULL<<30)) >> (30-3));
	if((pdpe & 1) == 0) return 0;
	if(perm != nil) *perm &= pdpe;
	if((pdpe & 0x80) != 0){
		*pa = (pdpe & 0xfffffc0000000ULL) | (va & 0x3fffffffULL);
		return 0x40000000ULL - (va & 0x3fffffffULL);
	}

	pd = gptr(pdpe & 0xffffffffff000ULL, 4096);
	if(pd == nil) return 0;
	pde = GET64(pd, (va & (511ULL<<21)) >> (21-3));
	if((pde & 1) == 0) return 0;
	if(perm != nil) *perm &= pde;
	if((pde & 0x80) != 0){
		*pa = (pde & 0xfffffffe00000ULL) | (va & 0x1fffffULL);
		return 0x200000ULL - (va & 0x1fffffULL);
	}

	pt = gptr(pde & 0xffffffffff000ULL, 4096);
	if(pt == nil) return 0;
	pte = GET64(pt, (va & (511ULL<<12)) >> (12-3));
	if((pte & 1) == 0) return 0;
	if(perm != nil) *perm &= pte;
	*pa = (pte & 0xffffffffff000ULL) | (va & 0xfffULL);
	return 0x1000ULL - (va & 0xfffULL);
}

static uintptr (*
translator(void))(uintptr, uintptr *, int *)
{
	uintptr cr0, cr4, efer;
	
	cr0 = rget("cr0real");
	if((cr0 & Cr0Pg) == 0)
		return translateflat;
	efer = rget("efer");
	if((efer & EferLme) != 0)
		return translate64;
	cr4 = rget("cr4real");
	if((cr4 & Cr4Pae) != 0)
		return translatepae;
	return translate32;
}

static void
vmemread0(void *aux)
{
	VMemReq *req;
	uintptr va, pa, n, ok, pok;
	void *v;
	uintptr (*trans)(uintptr, uintptr *, int *);
	uchar *p;
	int wr;
	
	req = aux;
	va = req->va;
	n = req->len;
	p = req->buf;
	wr = req->wr;
	trans = translator();
	while(n > 0){
		ok = trans(va, &pa, nil);
		if(ok == 0) break;
		if(ok > n) ok = n;
		v = gptr(pa, 1);
		if(v == nil) break;
		pok = gavail(v);
		if(ok > pok) ok = pok;
		if(wr)
			memmove(v, p, ok);
		else
			memmove(p, v, ok);
		n -= ok;
		p += ok;
		va += ok;
	}
	req->rc = req->len - n;
	qunlock(req);
}

uintptr
vmemread(void *buf, uintptr len, uintptr va)
{
	VMemReq req;
	
	memset(&req, 0, sizeof(VMemReq));
	req.wr = 0;
	req.buf = buf;
	req.len = len;
	req.va = va;
	qlock(&req);
	sendnotif(vmemread0, &req);
	qlock(&req);
	return req.rc;
}

uintptr
vmemwrite(void *buf, uintptr len, uintptr va)
{
	VMemReq req;
	
	memset(&req, 0, sizeof(VMemReq));
	req.wr = 1;
	req.buf = buf;
	req.len = len;
	req.va = va;
	qlock(&req);
	sendnotif(vmemread0, &req);
	qlock(&req);
	return req.rc;
}

int
x86access(int seg, uintptr addr0, int asz, uvlong *val, int sz, int acc, TLB *tlb)
{
	int cpl;
	static char *baser[] = {"csbase", "dsbase", "esbase", "fsbase", "gsbase", "ssbase"};
	static char *limitr[] = {"cslimit", "dslimit", "eslimit", "fslimit", "gslimit", "sslimit"};
	static char *permr[] = {"csperm", "dsperm", "esperm", "fsperm", "gsperm", "ssperm"};
	uvlong tval;
	u32int limit, perm;
	uintptr addr, base, szmax;
	int pperm, wp, i;
	uintptr pa[8], pav;
	uintptr l;
	uchar *ptr;
	Region *r;

	switch(asz){
	case 2: addr0 = (u16int)addr0; break;
	case 4: addr0 = (u32int)addr0; break;
	case 8: break;
	default:
		vmerror("invalid asz=%d in x86access", asz);
		assert(0);
	}
	assert(seg < SEGMAX && (u8int)acc <= ACCX);
	addr = addr0;
	if(tlb != nil && tlb->asz == asz && tlb->seg == seg && tlb->acc == (u8int)acc && addr >= tlb->start && addr + sz >= addr && addr + sz < tlb->end){
		ptr = tlb->base + addr;
		pa[0] = tlb->pabase + addr;
		r = tlb->reg;
		goto fast;
	}
	if(sizeof(uintptr) == 8 && asz == 8){
		if(seg == SEGFS || seg == SEGGS)
			addr += rget(baser[seg]);
		if((u16int)(((u64int)addr >> 48) + 1) > 1){
		gpf:
			if((acc & ACCSAFE) == 0){
				vmdebug("gpf");
				postexc("#gp", 0);
			}
			return -1;
		}
		if((vlong)addr >= 0)
			szmax = (1ULL<<48) - addr;
		else
			szmax = -addr;
	}else{
		limit = rget(limitr[seg]);
		perm = rget(permr[seg]);
		if((perm & 0xc) == 0x4){
			if((u32int)(addr + sz - 1) < addr || addr <= limit)
				goto limfault;
			szmax = (u32int)-addr;
		}else{
			if((u64int)addr + sz - 1 >= limit){
			limfault:
				if((acc & ACCSAFE) == 0){
					vmdebug("limit fault");
					postexc(seg == SEGSS ? "#ss" : "#gp", 0);
				}
				return -1;
			}
			szmax = limit - addr + 1;
		}
		if((perm & 0x10080) != 0x80)
			goto gpf;
		switch((u8int)acc){
		case ACCR: if((perm & 0xa) == 8) goto gpf; break;
		case ACCW: if((perm & 0xa) != 2) goto gpf; break;
		case ACCX: if((perm & 8) == 0) goto gpf; break;
		}
		base = rget(baser[seg]);
		addr = (u32int)(addr + base);
	}
	cpl = rget("cs") & 3;
	wp = (rget("cr0real") & 1<<16) != 0;
	for(i = 0; i < sz; ){
		pperm = 0;
		l = translator()(addr+i, &pav, &pperm);
		if(l == 0){
		pf:
			if((acc & ACCSAFE) == 0){
				vmdebug("page fault @ %#p", addr+i);
				postexc("#pf", pperm & 1 | ((u8int)acc == ACCW) << 1 | (cpl == 3) << 2 | ((u8int)acc == ACCX) << 4);
				rset("cr2", addr+i);
			}
			return -1;
		}
		if((cpl == 3 || wp) && (u8int)acc == ACCW && (pperm & 2) == 0)
			goto pf;
		if(cpl == 3 && (pperm & 4) == 0)
			goto pf;
		if(i == 0 && l < szmax) szmax = l;
		while(i < sz && l-- > 0)
			pa[i++] = pav++;
	}
	if(szmax >= sz){
		r = regptr(pa[0]);
		if(r == nil || pa[0]+sz > r->end) goto slow;
		ptr = (uchar*)r->v + (pa[0] - r->start);
		if(tlb != nil){
			l = gavail(ptr);
			if(l < szmax) szmax = l;
			tlb->asz = asz;
			tlb->seg = seg;
			tlb->acc = (u8int)acc;
			tlb->start = addr0;
			tlb->end = addr0 + szmax;
			tlb->reg = r;
			tlb->base = ptr - addr0;
			tlb->pabase = pa[0] - addr0;
		}
	fast:
		if(r->mmio != nil)
			r->mmio(pa[0], val, sz, (u8int)acc == ACCW);
		else if(acc == ACCW)
			switch(sz){
			case 1: PUT8(ptr, 0, *val); break;
			case 2: PUT16(ptr, 0, *val); break;
			case 4: PUT32(ptr, 0, *val); break;
			case 8: PUT64(ptr, 0, *val); break;
			default: goto slow;
			}
		else
			switch(sz){
			case 1: *val = GET8(ptr, 0); break;
			case 2: *val = GET16(ptr, 0); break;
			case 4: *val = GET32(ptr, 0); break;
			case 8: *val = GET64(ptr, 0); break;
			default: goto slow;
			}
	}else{
	slow:
		if(acc != ACCW)
			*val = 0;
		for(i = 0; i < sz; i++){
			r = regptr(pa[i]);
			if(r == nil)
				vmerror("x86access: access to unmapped address %#p", pa[i]);
			else if(acc == ACCW){
				tval = GET8(val, i);
				if(r->mmio != nil)
					r->mmio(pa[i], &tval, 1, 1);
				else
					PUT8(r->v, pa[i] - r->start, tval);
			}else{
				if(r->mmio != nil)
					r->mmio(pa[i], &tval, 1, 0);
				else
					tval = GET8(r->v, pa[i] - r->start);
				PUT8(val, i, tval);
			}	
		}
	}
	return 0;
}

enum {
	ONOPE,	OADC,	OADD,	OAND,	OASZ,	OCALL,	OCMP,	OCMPS,	ODEC,
	OENTER,	OEX,	OIMUL,	OINC,	OINS,	OLEAVE,	OLOCK,	OLODS,	OMOV,	OMOVS,
	OOR,	OOSZ,	OOUTS,	OPOP,	OPOPA,	OPOPF,	OPUSH,	OPUSHA,	OPUSHF,
	OREP,	OREPNE,	ORET,	OSBB,	OSCAS,	OSEG,	OSTOS,	OSUB,
	OTEST,	OXCHG,	OXLAT,	OXOR,	OROL,	OROR,	ORCL,	ORCR,
	OSHL,	OSHR,	OSAR,	ONOT,	ONEG,	ODIV,	OIDIV,	OMUL,
	OJMP,
};

static char *onames[] = {
	[ONOPE]"ONOPE", [OADC]"OADC", [OADD]"OADD", [OAND]"OAND", [OASZ]"OASZ", [OCALL]"OCALL", [OCMP]"OCMP", [OCMPS]"OCMPS", [ODEC]"ODEC",
	[OENTER]"OENTER", [OIMUL]"OIMUL", [OINC]"OINC", [OINS]"OINS", [OLEAVE]"OLEAVE", [OLOCK]"OLOCK", [OLODS]"OLODS", [OMOV]"OMOV", [OMOVS]"OMOVS",
	[OOR]"OOR", [OOSZ]"OOSZ", [OOUTS]"OOUTS", [OPOP]"OPOP", [OPOPA]"OPOPA", [OPOPF]"OPOPF", [OPUSH]"OPUSH", [OPUSHA]"OPUSHA", [OPUSHF]"OPUSHF",
	[OREP]"OREP", [OREPNE]"OREPNE", [ORET]"ORET", [OSBB]"OSBB", [OSCAS]"OSCAS", [OSEG]"OSEG", [OSTOS]"OSTOS", [OSUB]"OSUB",
	[OTEST]"OTEST", [OXCHG]"OXCHG", [OXLAT]"OXLAT", [OXOR]"OXOR", [OEX]"OEX", [OROL]"OROL", [OROR]"OROR", [ORCL]"ORCL", [ORCR]"ORCR",
	[OSHL]"OSHL", [OSHR]"OSHR", [OSAR]"OSAR", [ONOT]"ONOT", [ONEG]"ONEG", [ODIV]"ODIV", [OIDIV]"OIDIV", [OMUL]"OMUL", [OJMP]"OJMP"
};
#define enumconv(x,buf,tab) ((x)<nelem(tab)?(tab)[x]:(sprint(buf,"%d",(x)),buf))

/*
	size fields:
	0 b byte
	1 v short/long/vlong (16-bit,32-bit,64-bit mode)
	2 z short/long/long
	3 w short
*/

enum {
	ANOPE,
	A1 = 1, /* constant 1 */
	
	/* general purpose registers with size+1 in high nibble */
	AGPRb = 0x10,
	AGPRv = 0x20,
	AGPRz = 0x30,
	AAXb = 0x10,	ACXb = 0x11,	ADXb = 0x12,	ABXb = 0x13,	ASPb = 0x14,	ABPb = 0x15,	ASIb = 0x16,	ADIb = 0x17,
	AAXv = 0x20,	ACXv = 0x21,	ADXv = 0x22,	ABXv = 0x23,	ASPv = 0x24,	ABPv = 0x25,	ASIv = 0x26,	ADIv = 0x27,
	AAXz = 0x30,	ACXz = 0x31,	ADXz = 0x32,	ABXz = 0x33,	ASPz = 0x34,	ABPz = 0x35,	ASIz = 0x36,	ADIz = 0x37,
	
	ASEG = 0x40,	ACS = 0x40,	ADS = 0x41,	AES = 0x42,	AFS = 0x43,	AGS = 0x44,	ASS = 0x45,
	
	/* below has valid size in lower nibble */
	AGOTSZ = 0x50,
	AOb = 0x50,	AOv = 0x51,
	
	AIMM = 0x70,
	AIb = 0x70,		AIz = 0x72,
	/* below involves modrm */
	AMODRM = 0x80,
	AEb = 0x80,	AEv = 0x81,
	AGb = 0x90,	AGv = 0x91,
	ASw = 0xA3,
};

static char *anames[] = {
	[ANOPE]"ANOPE",	[AEb]"AEb",	[AEv]"AEv",	[AGb]"AGb",	[AGv]"AGv",	[AIb]"AIb",	[AIz]"AIz",
	[ASw]"ASw",	[AOb]"AOb",	[AOv]"AOv",
	[ACS]"ACS",	[ADS]"ADS",	[AES]"AES",	[AFS]"AFS",	[AGS]"AGS",	[ASS]"ASS",
	[AAXb]"AAXb",	[ABXb]"ABXb",	[ACXb]"ACXb",	[ADXb]"ADXb",	[ABPb]"ABPb",	[ASPb]"ASPb",	[ASIb]"ASIb",	[ADIb]"ADIb",
	[AAXv]"AAXv",	[ABXv]"ABXv",	[ACXv]"ACXv",	[ADXv]"ADXv",	[ABPv]"ABPv",	[ASPv]"ASPv",	[ASIv]"ASIv",	[ADIv]"ADIv",
	[AAXz]"AAXz",	[ABXz]"ABXz",	[ACXz]"ACXz",	[ADXz]"ADXz",	[ABPz]"ABPz",	[ASPz]"ASPz",	[ASIz]"ASIz",	[ADIz]"ADIz",
};
/* typically b is dst and c is src */
#define O(a,b,c) ((a)|(b)<<8|(c)<<16)

/* we only care about operations that can go to memory */
static u32int optab[256] = {
/*0*/	O(OADD,AEb,AGb), O(OADD,AEv,AGv), O(OADD,AGb,AEb), O(OADD,AGv,AEv), O(OADD,AAXb,AIb), O(OADD,AAXz,AIz), O(OPUSH,AES,0), O(OPOP,AES,0),
	O(OOR,AEb,AGb), O(OOR,AEv,AGv), O(OOR,AGb,AEb), O(OOR,AGv,AEv), O(OOR,AAXb,AIb), O(OOR,AAXz,AIz), O(OPUSH,ACS,0), 0,
	
/*1*/	O(OADC,AEb,AGb), O(OADC,AEv,AGv), O(OADC,AGb,AEb), O(OADC,AGv,AEv), O(OADC,AAXb,AIb), O(OADC,AAXz,AIz), O(OPUSH,ASS,0), O(OPOP,ASS,0),
	O(OSBB,AEb,AGb), O(OSBB,AEv,AGv), O(OSBB,AGb,AEb), O(OSBB,AGv,AEv), O(OSBB,AAXb,AIb), O(OSBB,AAXz,AIz), O(OPUSH,ADS,0), O(OPOP,ADS,0),
	
/*2*/	O(OAND,AEb,AGb), O(OAND,AEv,AGv), O(OAND,AGb,AEb), O(OAND,AGv,AEv), O(OAND,AAXb,AIb), O(OAND,AAXz,AIz), O(OSEG,AES,0), 0/*DAA*/,
	O(OSUB,AEb,AGb), O(OSUB,AEv,AGv), O(OSUB,AGb,AEb), O(OSUB,AGv,AEv), O(OSUB,AAXb,AIb), O(OSUB,AAXz,AIz), O(OSEG,ACS,0), 0/*DAS*/,

/*3*/	O(OXOR,AEb,AGb), O(OXOR,AEv,AGv), O(OXOR,AGb,AEb), O(OXOR,AGv,AEv), O(OXOR,AAXb,AIb), O(OXOR,AAXz,AIz), O(OSEG,ASS,0), 0/*AAA*/,
	O(OCMP,AEb,AGb), O(OCMP,AEv,AGv), O(OCMP,AGb,AEb), O(OCMP,AGv,AEv), O(OCMP,AAXb,AIb), O(OCMP,AAXz,AIz), O(OSEG,ADS,0), 0/*AAS*/,
	
/*4*/	0, 0, 0, 0, 0, 0, 0, 0, /* rex prefixes */
	0, 0, 0, 0, 0, 0, 0, 0,
	
/*5*/	O(OPUSH,AAXv,0), O(OPUSH,ACXv,0), O(OPUSH,ADXv,0), O(OPUSH,ABXv,0), O(OPUSH,ASPv,0), O(OPUSH,ABPv,0), O(OPUSH,ASIv,0), O(OPUSH,ADIv,0),
	O(OPOP,AAXv,0), O(OPOP,ACXv,0), O(OPOP,ADXv,0), O(OPOP,ABXv,0), O(OPOP,ASPv,0), O(OPOP,ABPv,0), O(OPOP,ASIv,0), O(OPOP,ADIv,0),
	
/*6*/	OPUSHA, OPOPA, 0/*BOUND*/, 0/*ARPL*/, O(OSEG,AFS,0), O(OSEG,AGS,0), OOSZ, OASZ,
	O(OPUSH,AIz,0), O(OIMUL,AGv,AIz), O(OPUSH,AIb,0), O(OIMUL,AGv,AIb), OINS, OINS, OOUTS, OOUTS,
	
/*7*/	0, 0, 0, 0, 0, 0, 0, 0, /* jumps */
	0, 0, 0, 0, 0, 0, 0, 0,
	
/*8*/	OEX, OEX, OEX, OEX, O(OTEST,AEb,AGb), O(OTEST,AEv,AGv), O(OXCHG,AEb,AGb), O(OXCHG,AEv,AGv),
	O(OMOV,AEb,AGb), O(OMOV,AEv,AGv), O(OMOV,AGb,AEb), O(OMOV,AGv,AEv), O(OMOV,AEv,ASw), 0/*LEA*/, O(OMOV,ASw,AEv), OEX,

/*9*/	0, 0, 0, 0, 0, 0, 0, 0, /* register exchange */
	0/*CBW*/, 0/*CWD*/, OCALL, 0/*FWAIT*/, OPUSHF, OPOPF, 0/*OSAHF*/, 0/*OLAHF*/,
	
/*A*/	O(OMOV,AAXb,AOb), O(OMOV,AAXv,AOv), O(OMOV,AOb,AAXb), O(OMOV,AOv,AAXv), OMOVS, OMOVS, OCMPS, OCMPS,
	0, 0/*TEST Reg,Imm*/, OSTOS, OSTOS, OLODS, OLODS, OSCAS, OSCAS,

/*B*/	0, 0, 0, 0, 0, 0, 0, 0, /* move immediate to register */
	0, 0, 0, 0, 0, 0, 0, 0,

/*C*/	OEX, OEX, ORET, ORET, 0/*LES*/, 0/*LDS*/, OEX, OEX,
	OENTER, OLEAVE, ORET, ORET, 0/*INT3*/, 0/*INTn*/, 0/*INTO*/, 0/*IRET*/,
	
/*D*/	OEX, OEX, OEX, OEX, 0/*AAM*/, 0/*AAD*/, 0, OXLAT,
	0, 0, 0, 0, 0, 0, 0, 0, /* fpu */

/*E*/	0, 0, 0/*LOOPx*/, 0/*JrCXZ*/, 0, 0/*IN*/, 0, 0/*OUT*/,
	OCALL, OCALL, 0, 0/*JMP*/, 0, 0/*IN*/, 0, 0/*OUT*/,

/*F*/	OLOCK, 0, OREPNE, OREP, 0/*HALT*/, 0/*CMC*/, OEX, OEX,
	0/*CLC*/, 0/*STC*/, 0/*CLI*/, 0/*STI*/, 0/*CLD*/, 0/*STD*/, OEX, OEX,
};
/* OEX tables (operations determined by modrm byte) */
static u32int optab80[8] = {O(OADD,AEb,AIb), O(OOR,AEb,AIb), O(OADC,AEb,AIb), O(OSBB,AEb,AIb), O(OAND,AEb,AIb), O(OSUB,AEb,AIb), O(OXOR,AEb,AIb), O(OCMP,AEb,AIb)};
static u32int optab81[8] = {O(OADD,AEv,AIz), O(OOR,AEv,AIz), O(OADC,AEv,AIz), O(OSBB,AEv,AIz), O(OAND,AEv,AIz), O(OSUB,AEv,AIz), O(OXOR,AEv,AIz), O(OCMP,AEv,AIz)};
/* 0x82 is identical to 0x80 */
static u32int optab83[8] = {O(OADD,AEv,AIb), O(OOR,AEv,AIb), O(OADC,AEv,AIb), O(OSBB,AEv,AIb), O(OAND,AEv,AIb), O(OSUB,AEv,AIb), O(OXOR,AEv,AIb), O(OCMP,AEv,AIb)};
static u32int optab8F[8] = {O(OPOP,AEv,0)};
static u32int optabC0[8] = {O(OROL,AEb,AIb), O(OROR,AEb,AIb), O(ORCL,AEb,AIb), O(ORCR,AEb,AIb), O(OSHL,AEb,AIb), O(OSHR,AEb,AIb), 0, O(OSAR,AEb,AIb)};
static u32int optabC1[8] = {O(OROL,AEv,AIb), O(OROR,AEv,AIb), O(ORCL,AEv,AIb), O(ORCR,AEv,AIb), O(OSHL,AEv,AIb), O(OSHR,AEv,AIb), 0, O(OSAR,AEv,AIb)};
static u32int optabD0[8] = {O(OROL,AEb,A1), O(OROR,AEb,A1), O(ORCL,AEb,A1), O(ORCR,AEb,A1), O(OSHL,AEb,A1), O(OSHR,AEb,A1), 0, O(OSAR,AEb,A1)};
static u32int optabD1[8] = {O(OROL,AEv,A1), O(OROR,AEv,A1), O(ORCL,AEv,A1), O(ORCR,AEv,A1), O(OSHL,AEv,A1), O(OSHR,AEv,A1), 0, O(OSAR,AEv,A1)};
static u32int optabD2[8] = {O(OROL,AEb,ACXb), O(OROR,AEb,ACXb), O(ORCL,AEb,ACXb), O(ORCR,AEb,ACXb), O(OSHL,AEb,ACXb), O(OSHR,AEb,ACXb), 0, O(OSAR,AEb,ACXb)};
static u32int optabD3[8] = {O(OROL,AEv,ACXb), O(OROR,AEv,ACXb), O(ORCL,AEv,ACXb), O(ORCR,AEv,ACXb), O(OSHL,AEv,ACXb), O(OSHR,AEv,ACXb), 0, O(OSAR,AEv,ACXb)};
static u32int optabC6[8] = {O(OMOV,AEb,AIb)};
static u32int optabC7[8] = {O(OMOV,AEv,AIz)};
static u32int optabF6[8] = {O(OTEST,AEb,AIb), 0, O(ONOT,AEb,0), O(ONEG,AEb,0), O(OMUL,AAXb,AEb), O(OIMUL,AAXb,0), O(ODIV,AAXb,0), O(OIDIV,AAXb,0)};
static u32int optabF7[8] = {O(OTEST,AEv,AIz), 0, O(ONOT,AEv,0), O(ONEG,AEv,0), O(OMUL,AAXv,AEv), O(OIMUL,AAXv,0), O(ODIV,AAXv,0), O(OIDIV,AAXv,0)};
static u32int optabFE[8] = {O(OINC,AEb,0),O(ODEC,AEb,0)};
static u32int optabFF[8] = {O(OINC,AEv,0),O(ODEC,AEv,0),OCALL,OCALL,OJMP,OJMP,O(OPUSH,AEv,0),0};

typedef struct Instr Instr;
typedef struct Oper Oper;
/* for registers we put the number in addr and add +0x10 for "high bytes" (AH etc) */
struct Oper {
	enum { OPNONE, OPREG, OPSEG, OPIMM, OPMEM } type;
	uintptr addr;
	int sz;
	uvlong val;
};
struct Instr {
	u8int bytes[16];
	int nbytes;
	u8int opcode; /* first byte after the prefixes */
	u32int inf;
	u8int modrm, sib;
	vlong disp;
	uvlong imm;
	enum {
		INSLOCK = 0x1,
		INSREP = 0x2,
		INSREPNE = 0x4,
		INSOSZ = 0x8,
		INSASZ = 0x10,
		INSMODRM = 0x20,
		INSSIB = 0x40,
		INSDISP8 = 0x80,
		INSDISP16 = 0x100,
		INSDISP32 = 0x200,
		INSDISP64 = 0x400,
		INSIMM8 = 0x800,
		INSIMM16 = 0x1000,
		INSIMM32 = 0x2000,
	/*	INSIMM64 = 0x4000, not yet */
	} flags;
	int seg;
	u8int osz, asz;
	Oper op[2];
};

struct Step {
	uintptr pc, npc;
	u8int mode;
	TLB tlb;
	Instr;
} step;

static int
fetch8(int acc)
{
	uvlong v;
	
	if(step.nbytes >= sizeof(step.bytes)){
		if((acc & ACCSAFE) == 0){
			vmerror("x86step: instruction too long (pc=%#p)", step.pc);
			postexc("#ud", NOERRC);
		}
		return -1;
	}
	if(x86access(SEGCS, step.npc, step.mode, &v, 1, ACCX|acc, &step.tlb) < 0){
		vmerror("x86step: fault while trying to load %#p, shouldn't happen", step.pc);
		return -1;
	}
	step.npc++;
	step.bytes[step.nbytes++] = v;
	return (u8int)v;
}

static int
fetch16(int acc)
{
	int r0, r1;
	
	if(r0 = fetch8(acc), r0 < 0) return -1;
	if(r1 = fetch8(acc), r1 < 0) return -1;
	return r0 | r1 << 8;
}

static vlong
fetch32(int acc)
{
	int r0, r1, r2, r3;
	
	if(r0 = fetch8(acc), r0 < 0) return -1;
	if(r1 = fetch8(acc), r1 < 0) return -1;
	if(r2 = fetch8(acc), r2 < 0) return -1;
	if(r3 = fetch8(acc), r3 < 0) return -1;
	return r0 | r1 << 8 | r2 << 16 | r3 << 24;
}

static int
fetch64(int acc, uvlong *p)
{
	vlong r0, r1;
	
	if(r0 = fetch32(acc), r0 < 0) return -1;
	if(r1 = fetch32(acc), r1 < 0) return -1;
	*p = r0 | r1 << 32;
	return 0;
}

static long
machread(int, void *vb, long n, vlong soff)
{
	uvlong o;
	
	o = soff;
	if(o < step.pc) return 0;
	if(o >= step.pc+step.nbytes) return 0;
	if(n > step.pc+step.nbytes-o)
		n = step.pc+step.nbytes-o;
	memmove(vb, step.bytes+(o-step.pc), n);
	return n;
}

static void
giveup(void)
{
	static Map *m;
	char buf[128];
	char *p, *e;
	extern Machdata i386mach;
	int i, rc;
	
	if(m == nil){
		m = newmap(nil, 1);
		setmap(m, -1, 0, -1, 0, "text");
		m->seg[0].read = machread;
	}
	p = buf;
	e = buf + sizeof(buf);
	while(fetch8(ACCSAFE) >= 0)
		;
	if(rc = i386mach.das(m, step.pc, 0, buf, sizeof(buf)), rc >= 0){
		p += strlen(buf);
		p = seprint(p, e, " # ");
	}else
		rc = step.nbytes;
	for(i = 0; i < rc; i++)
		p = seprint(p, e, "%.2x ", step.bytes[i]);
	vmerror("x86step: unimplemented instruction %s", buf);	
}

static int
grab(void)
{
	int op;
	int rc;
	vlong vrc;
	u32int inf;
	u32int *tab;

again:
	op = fetch8(0);
	if(op < 0) return -1;
	inf = optab[op];
	if(inf == 0){ giveup(); return -1; }
	switch((u8int)inf){
	case OLOCK: step.flags |= INSLOCK; goto again;
	case OREP: step.flags |= INSREP; goto again;
	case OREPNE: step.flags |= INSREPNE; goto again;
	case OOSZ: step.flags |= INSOSZ; step.osz = step.osz == 2 ? 4 : 2; goto again;
	case OASZ: step.flags |= INSASZ; step.asz = step.asz == 2 ? 4 : 2; goto again;
	case OSEG: step.seg = inf >> 8; goto again;
	}
	step.opcode = op;
	if((u8int)(inf >> 8) >= AMODRM || (u8int)(inf >> 16) >= AMODRM || inf == OEX){
		rc = fetch8(0);
		if(rc < 0) return -1;
		step.modrm = rc;
		step.flags |= INSMODRM;
		if(step.asz != 2 && (step.modrm & 0x07) == 0x04 && step.modrm < 0xc0){
			rc = fetch8(0); if(rc < 0) return -1;
			step.sib = rc;
			step.flags |= INSSIB;
		}
		switch(step.modrm >> 6){
		case 1:
			rc = fetch8(0); if(rc < 0) return -1;
			step.disp = (s8int)rc;
			step.flags |= INSDISP8;
			break;
		case 0:
			if((step.modrm & 7) != (step.asz == 2) + 5 && (step.sib & 7) != 5)
				break;
			/* wet floor */
		case 2:
			if(step.asz == 2){
				rc = fetch16(0); if(rc < 0) return -1;
				step.disp = (s16int)rc;
				step.flags |= INSDISP16;
			}else{
				vrc = fetch32(0); if(vrc < 0) return -1;
				step.disp = (s32int)vrc;
				step.flags |= INSDISP32;
			}
			break;
		}
	}
	if(inf == OEX){
		switch(op){
		case 0x80: case 0x82: tab = optab80; break;
		case 0x81: tab = optab81; break;
		case 0x83: tab = optab83; break;
		case 0x8f: tab = optab8F; break;
		case 0xc0: tab = optabC0; break;
		case 0xc1: tab = optabC1; break;
		case 0xd0: tab = optabD0; break;
		case 0xd1: tab = optabD1; break;
		case 0xd2: tab = optabD2; break;
		case 0xd3: tab = optabD3; break;
		case 0xc6: tab = optabC6; break;
		case 0xc7: tab = optabC7; break;
		case 0xf6: tab = optabF6; break;
		case 0xf7: tab = optabF7; break;
		case 0xfe: tab = optabFE; break;
		case 0xff: tab = optabFF; break;
		default: tab = nil;
		}
		if(tab == nil || (inf = tab[step.modrm >> 3 & 7]) == 0){
			giveup();
			return -1;
		}
	}
	if(((u8int)(inf >> 8) & 0xf0) == AIMM){
		rc = inf >> 8 & 0xf;
	imm:
		switch(rc){
		case 0:
			rc = fetch8(0); if(rc < 0) return -1;
			step.imm = rc;
			step.flags |= INSIMM8;
			break;
		case 2:
			switch(step.osz){
			case 2:
				rc = fetch16(0); if(rc < 0) return -1;
				step.imm = rc;
				step.flags |= INSIMM16;
				break;
			case 4:
			case 8:
				vrc = fetch32(0); if(vrc < 0) return -1;
				step.imm = vrc;
				step.flags |= INSIMM32;
				break;
			}
			break;
		default:
			vmerror("x86step: grab: immediate size=%d, shouldn't happen", rc);
			giveup();
			return -1;
		}
	}else if((u8int)(inf >> 16 & 0xf0) == AIMM){
		rc = inf >> 16 & 0xf;
		goto imm;
	}
	if(((u8int)(inf >> 8) & 0xf0) == AOb || (u8int)(inf >> 16 & 0xf0) == AOb)
		switch(step.asz){
		case 2:
			rc = fetch16(0); if(rc < 0) return -1;
			step.disp = rc;
			step.flags |= INSDISP16;
			break;
		case 4:
			vrc = fetch32(0); if(vrc < 0) return -1;
			step.disp = vrc;
			step.flags |= INSDISP32;
			break;
		case 8:
			if(fetch64(0, (uvlong *) &step.disp) < 0) return -1;
			step.flags |= INSDISP64;
			break;			
		}
	step.inf = inf;
	return 0;
}

static void
decreg(Oper *o, int n, int sz)
{
	o->type = OPREG;
	o->sz = sz;
	if(sz == 1 && n >= 4){
		o->addr = n ^ 0x14;
		o->val = (u8int)(rget(x86reg[n&3]) >> 8);
	}else{
		o->addr = n;
		o->val = rgetsz(x86reg[n], sz);
	}
}

static void
decmodrm(Oper *o, int sz)
{
	u8int mod, m;
	
	mod = step.modrm >> 6;
	m = step.modrm & 7;
	if(mod == 3){
		decreg(o, m, sz);
		return;
	}
	o->type = OPMEM;
	o->sz = sz;
	if(step.asz == 2){
		switch(m){
		case 0: o->addr = rget(RBX) + rget(RSI); break;
		case 1: o->addr = rget(RBX) + rget(RDI); break;
		case 2: o->addr = rget(RBP) + rget(RSI); break;
		case 3: o->addr = rget(RBX) + rget(RDI); break;
		case 4: o->addr = rget(RSI); break;
		case 5: o->addr = rget(RDI); break;
		case 6: o->addr = mod == 0 ? 0 : rget(RBP); break;
		case 7: o->addr = rget(RBX); break;
		}
		o->addr = (u16int)(o->addr + step.disp);
		if(step.seg < 0)
			if(m == 6 && mod != 0)
				step.seg = SEGSS;
			else
				step.seg = SEGDS;
		return;
	}
	if(m != 4){
		if((step.modrm & 0xc7) == 5)
			o->addr = 0;
		else
			o->addr = rget(x86reg[m]);
		o->addr = (u32int)(o->addr + step.disp);
		if(step.seg < 0)
			if(m == 5 && mod != 0)
				step.seg = SEGSS;
			else
				step.seg = SEGDS; 
		return;
	}
	if((step.sib >> 3 & 7) != 4)
		o->addr = rget(x86reg[step.sib >> 3 & 7]);
	else
		o->addr = 0;
	o->addr <<= step.sib >> 6;
	if((step.sib & 7) != 5 || mod != 0)
		o->addr += rget(x86reg[step.sib & 7]);
	o->addr = (u32int)(o->addr + step.disp);
	if(step.seg < 0)
		if((step.sib & 7) == 4 || (step.sib & 7) == 5 && mod != 0)
			step.seg = SEGSS;
		else
			step.seg = SEGDS;
}

static int
parseoper(void)
{
	int i;
	u8int f;
	Oper *o;
	u8int sizes[4] = {1, step.osz, step.osz == 8 ? 4 : step.osz, 2};
	
	for(i = 0; i < 2; i++){
		f = step.inf >> 8 * (i + 1);
		o = &step.op[i];
		switch(f & 0xf0){
		case AGPRb:
		case AGPRv:
		case AGPRz:
			o->type = OPREG;
			o->addr = f & 0xf;
			o->val = rget(x86reg[f & 0xf]);
			o->sz = sizes[(f >> 4) - 1];
			break;
		case ASEG:
			o->type = OPSEG;
			o->addr = f & 0xf;
			o->val = rget(x86segreg[f & 0xf]);
			o->sz = 2;
			break;
		case AOb:
			o->type = OPMEM;
			o->addr = step.disp;
			o->sz = sizes[f & 0xf];
			if(step.seg < 0)
				step.seg = SEGDS;
			break;
		case AIMM:
			o->type = OPIMM;
			o->val = step.imm;
			o->sz = sizes[f & 0xf];
			break;
		case AEb:
			decmodrm(o, sizes[f & 0xf]);
			break;
		case A1:
			o->type = OPIMM;
			o->val = 1;
			o->sz = 1;
			break;
		case AGb:
			decreg(o, step.modrm >> 3 & 7, sizes[f & 0xf]);
			break;
		}
	}
	return 0;
}

static int
opwrite(Oper *o, uvlong v)
{
	char *n;
	
	switch(o->type){
	case OPREG:
		n = x86reg[o->addr & 0xf];
		if((o->addr & 0x10) != 0)
			rset(n, rget(n) & ~0xff00ULL | (u8int)v << 8);
		else
			rsetsz(n, v, o->sz);
		return 0;
	case OPMEM:
		if(x86access(step.seg, o->addr, step.asz, &v, o->sz, ACCW, &step.tlb) < 0)
			return -1;
		return 0;
	case OPSEG:
		giveup();
		return -1;
	default:
		vmerror("x86step: opwrite: unhandled o->type==%d, shouldn't happen", o->type);
		giveup();
		return -1;
	}
}

static int
opread(Oper *o, uvlong *v)
{
	switch(o->type){
	case OPREG:
	case OPSEG:
	case OPIMM:
		*v = o->val;
		return 0;
	case OPMEM:
		if(x86access(step.seg, o->addr, step.asz, v, o->sz, ACCR, &step.tlb) < 0)
			return -1;
		return 0;
	default:
		vmerror("x86step: opread: unhandled o->type==%d, shouldn't happen", o->type);
		giveup();
		return -1;
	}
}

static vlong
alu(int op, vlong a, int asz, vlong b, int bsz, uvlong *flags)
{
	vlong c;
	vlong amsk, sbit;
	u32int flout;
	u8int p;
	
	flout = 0;
	amsk = (-1ULL)>>64-8*asz;
	sbit = 1<<8*asz-1;
	b = b << 64 - 8*bsz >> 64 - 8*bsz;
	switch(op){
	case OADD:
	case OADC:
		c = (a & amsk) + (b & amsk);
		if(op == OADC) c += *flags & 1;
		if((~(a ^ b) & (a ^ c) & 1<<sbit) != 0) flout |= OF;
		if((a & 0xf) + (b & 0xf) >= 0x10) flout |= AF;
		goto addsub;
	case OSUB:
	case OSBB:
	case OCMP:
		c = (a & amsk) - (b & amsk);
		if(op == OSBB) c -= *flags & 1;
		if(((a ^ b) & (a ^ c) & 1<<sbit) != 0) flout |= OF;
		if((a & 0xf) < (b & 0xf)) flout |= AF;
	addsub:
		if((c & ~amsk) != 0) flout |= CF;
	logic:
		if((c & 1<<sbit) != 0) flout |= SF;
		if((c & amsk) == 0) flout |= ZF;
		p = c;
		if(0x69966996 << (p ^ p >> 4) < 0) flout |= PF;
		break;
	case OAND: c = a & b; goto logic;
	case OOR: c = a | b; goto logic;
	case OXOR: c = a ^ b; goto logic;
	default:
		vmerror("x86step: alu: unhandled case op==%d, shouldn't happen", op);
		return 0;
	}
	*flags ^= (*flags ^ flout) & (CF|SF|ZF|OF|AF|PF);
	return c & amsk;
}

static int
opcstring(void)
{
	int sz, srcseg, rc, inc;
	uvlong srcaddr, dstaddr;
	uvlong v;
	uvlong cx;
	char buf[16];
	
	if((step.opcode & 1) != 0)
		sz = step.osz;
	else
		sz = 1;
	srcseg = step.seg >= 0 ? step.seg : SEGDS;
	srcaddr = rget(RSI);
	dstaddr = rget(RDI);
	if((step.flags & INSREP) != 0)
		cx = rgetsz(RCX, step.asz);
	else
		cx = 1;
	if((rget(RFLAGS) & 0x400) != 0)
		inc = -sz;
	else
		inc = sz;

	rc = 1;
	switch((u8int)step.inf){
	case OLODS:
		for(; cx > 0; cx--){
			if(x86access(srcseg, srcaddr, step.asz, &v, sz, ACCR, &step.tlb) < 0){
				rc = 0;
				break;
			}
			rsetsz(RAX, v, sz);
			srcaddr += inc;
		}
		break;
	case OSTOS:
		v = rget(RAX);
		for(; cx > 0; cx--){
			if(x86access(SEGES, dstaddr, step.asz, &v, sz, ACCW, &step.tlb) < 0){
				rc = 0;
				break;
			}
			dstaddr += inc;
		}
		break;
	case OMOVS:
		for(; cx > 0; cx--){
			if(x86access(srcseg, srcaddr, step.asz, &v, sz, ACCR, &step.tlb) < 0 ||
			   x86access(SEGES, dstaddr, step.asz, &v, sz, ACCW, &step.tlb) < 0){
				rc = 0;
				break;
			}
			srcaddr += inc;
			dstaddr += inc;
		}
		break;
	default:
		vmerror("x86step: opcstring: unhandled case %s", enumconv((u8int)step.inf, buf, onames));
		giveup();
		return 0;
	}
	rsetsz(RSI, srcaddr, step.asz);
	rsetsz(RDI, dstaddr, step.asz);

	if((step.flags & (INSREP|INSREPNE)) != 0)
		rsetsz(RCX, cx, step.asz);
	return rc;
}

static int
opcstack(void)
{
	uvlong val, sp;
	int spsz;
	
	/* todo: get stack pointer size from stack segment */
	spsz = step.mode;
	sp = rgetsz(RSP, spsz);
	switch((u8int)step.inf){
	case OPUSH:
		if(opread(&step.op[0], &val) < 0) return 0;
		if(step.op[0].sz < step.osz && step.op[0].type != OPSEG)
			val = (vlong)val << 64 - 8 * step.op[0].sz >> 64 - 8 * step.op[0].sz;
		sp -= step.osz;
		if(x86access(SEGSS, sp, spsz, &val, step.osz, ACCW, &step.tlb) < 0) return 0;
		break;
	case OPOP:
		if(x86access(SEGSS, sp, spsz, &val, step.osz, ACCR, &step.tlb) < 0) return 0;
		if(opwrite(&step.op[0], val) < 0) return 0;
		sp += step.osz;
		break;
	default:
		vmerror("x86step: stack: unhandled case op==%d, shouldn't happen", (u8int)step.inf);
		return 0;
	}
	rsetsz(RSP, sp, spsz);
	return 1;
}

int
x86step(void)
{
	uvlong val, valb;
	uvlong rflags;
	char buf[16];
	
	memset(&step, 0, sizeof(step));
	step.seg = -1;
	step.pc = rget(RPC);
	step.npc = step.pc;
	step.mode = 4;
	step.asz = step.osz = step.mode;
	if(grab() < 0 || parseoper() < 0)
		return 0;
//	print("flags=%#ux modrm=%#ux sib=%#ux disp=%#ullx imm=%#ullx\n", step.flags, step.modrm, step.sib, step.disp, step.imm);
//	print("op0: type=%#ux addr=%#ullx val=%#ullx sz=%d\n", , );
//	print("op1: type=%#ux addr=%#ullx val=%#ullx sz=%d\n", step.op[1].type, step.op[1].addr, step.op[1].val, step.op[1].sz);
	print("%#.*p %s (%#ux,%d,%#ullx,%#ullx) (%#ux,%d,%#ullx,%#ullx) si %#llux di %#llux\n", 2*step.mode, step.pc, enumconv((u8int)step.inf,buf,onames), step.op[0].type, step.op[0].sz, (uvlong)step.op[0].addr, step.op[0].val, step.op[1].type, step.op[1].sz, (uvlong)step.op[1].addr, step.op[1].val, rget(RSI), rget(RDI));
	switch((u8int)step.inf){
	case OMOV:
		if((step.flags & (INSREP|INSREPNE|INSLOCK)) != 0) {giveup(); return 0;}
		if(opread(&step.op[1], &val) < 0) return 0;
		if(opwrite(&step.op[0], val) < 0) return 0;
		return 1;
	case OSTOS: case OLODS: case OMOVS:
		if((step.flags & (INSREPNE|INSLOCK)) != 0) {giveup(); return 0;}
		return opcstring();
	case OADD: case OADC: case OSUB: case OSBB: case OCMP: case OAND: case OOR: case OXOR:
		if((step.flags & (INSREP|INSREPNE)) != 0) {giveup(); return 0;}
		if(opread(&step.op[0], &val) < 0) return 0;
		if(opread(&step.op[1], &valb) < 0) return 0;
		rflags = rget(RFLAGS);
		val = alu((u8int)step.inf, val, step.op[0].sz, valb, step.op[1].sz, &rflags);
		if((u8int)step.inf != OCMP && opwrite(&step.op[0], val) < 0) return 0;
		rset(RFLAGS, rflags);
		return 1;
	case OPUSH: case OPOP:
		if((step.flags & (INSLOCK|INSREPNE|INSLOCK)) != 0) {giveup(); return 0;}
		return opcstack();
	default:
		vmerror("x86step: unhandled case %s", enumconv((u8int)step.inf, buf, onames));
		giveup();
		return 0;
	}
}
