#include <u.h>
#include <libc.h>
#include <thread.h>
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
translate64(uintptr, uintptr *, int *)
{
	vmerror("long mode translation not implemented");
	return 0;	
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
	u32int limit, perm;
	uintptr addr, base, szmax;
	int pperm, wp, i;
	uintptr pa[8], pav;
	uintptr l;
	uchar *ptr;

	switch(asz){
	case 2: addr0 = (u16int)addr0; break;
	case 4: addr0 = (u32int)addr0; break;
	case 8: break;
	default:
		vmerror("invalid asz=%d in x86access", asz);
		assert(0);
	}
	assert(seg < SEGMAX && (uint)acc <= ACCX);
	addr = addr0;
	if(tlb != nil && tlb->asz == asz && tlb->seg == seg && tlb->acc == acc && addr >= tlb->start && addr + sz >= addr && addr + sz < tlb->end){
		ptr = tlb->base + addr;
		goto fast;
	}
	if(sizeof(uintptr) == 8 && asz == 8){
		if(seg == SEGFS || seg == SEGGS)
			addr += rget(baser[seg]);
		if((u16int)((addr >> 48) + 1) > 1){
		gpf:
			vmdebug("gpf");
			postexc("#gp", 0);
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
				vmdebug("limit fault");
				postexc(seg == SEGSS ? "#ss" : "#gp", 0);
				return -1;
			}
			szmax = limit - addr + 1;
		}
		if((perm & 0x10080) != 0x80)
			goto gpf;
		switch(acc){
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
		l = translator()(addr+i, &pav, &pperm);
		if(l == 0){
		pf:
			vmdebug("page fault @ %#p", addr+i);
			postexc("#pf", pperm & 1 | (acc == ACCW) << 1 | (cpl == 3) << 2 | (acc == ACCX) << 4);
			rset("cr2", addr+i);
			return -1;
		}
		if((cpl == 3 || wp) && acc == ACCW && (pperm & 2) == 0)
			goto pf;
		if(cpl == 3 && (pperm & 4) == 0)
			goto pf;
		if(i == 0 && l < szmax) szmax = l;
		while(i < sz && l-- > 0)
			pa[i++] = pav++;
	}
	if(szmax >= sz){
		ptr = gptr(pa[0], sz);
		if(ptr == nil) goto slow;
		if(tlb != nil){
			l = gavail(ptr);
			if(l < szmax) szmax = l;
			tlb->asz = asz;
			tlb->seg = seg;
			tlb->acc = acc;
			tlb->start = addr0;
			tlb->end = addr0 + szmax;
			tlb->base = ptr - addr0;
		}
	fast:
		if(acc == ACCW)
			switch(sz){
			case 1: PUT8(ptr, 0, *val); break;
			case 2: PUT16(ptr, 0, *val); break;
			case 4: PUT32(ptr, 0, *val); break;
			case 8: PUT64(ptr, 0, *val); break;
			}
		else
			switch(sz){
			case 1: *val = GET8(ptr, 0); break;
			case 2: *val = GET16(ptr, 0); break;
			case 4: *val = GET32(ptr, 0); break;
			case 8: *val = GET64(ptr, 0); break;
			}
	}else{
	slow:
		if(acc != ACCW)
			*val = 0;
		for(i = 0; i < sz; i++){
			ptr = gptr(pa[i], 1);
			if(ptr == nil)
				vmerror("x86access: access to unmapped address %#p", pa[i]);
			else if(acc == ACCW)
				*ptr = GET8(val, i);
			else
				PUT8(val, i, *ptr);	
		}
	}
	return 0;
}
