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
};

static uintptr
translateflat(uintptr va, uintptr *pa, uintptr)
{
	*pa = va;
	if(va == 0)
		return -1;
	return 0;
}

static uintptr
translate32(uintptr va, uintptr *pa, uintptr cr4)
{
	void *pd, *pt;
	u32int pde, pte;

	if(sizeof(uintptr) != 4 && va >> 32 != 0) return -1;
	pd = gptr(rget("cr3") & ~0xfff, 4096);
	if(pd == nil) return 0;
	pde = GET32(pd, (va >> 22) * 4);
	if((pde & 1) == 0) return 0;
	if((pde & 0x80) != 0 && (cr4 & Cr4Pse) != 0){
		*pa = pde & (1<<22) - 1 | (uintptr)(pde & 0xfe000) << 19;
		return (1<<22) - (va & (1<<22)-1);
	}
	pt = gptr(pde & ~0xfff, 4096);
	if(pt == nil) return 0;
	pte = GET32(pt, va >> 10 & 0xffc);
	if((pte & 1) == 0) return 0;
	*pa = pte & ~0xfff | va & 0xfff;
	return 0x1000 - (va & 0xfff);
}

static uintptr
translatepae(uintptr, uintptr *, uintptr)
{
	vmerror("PAE translation not implemented");
	return 0;
}

static uintptr
translate64(uintptr, uintptr *, uintptr)
{
	vmerror("long mode translation not implemented");
	return 0;	
}

static uintptr (*
translator(uintptr *cr4p))(uintptr, uintptr *, uintptr)
{
	uintptr cr0, cr4, efer;
	
	cr0 = rget("cr0real");
	if((cr0 & Cr0Pg) == 0)
		return translateflat;
	efer = rget("efer");
	if((efer & EferLme) != 0)
		return translate64;
	cr4 = rget("cr4real");
	*cr4p = cr4;
	if((cr4 & Cr4Pae) != 0)
		return translatepae;
	return translate32;
}

static void
vmemread0(void *aux)
{
	VMemReq *req;
	uintptr va, pa, n, ok, pok, cr4;
	void *v;
	uintptr (*trans)(uintptr, uintptr *, uintptr);
	uchar *p;
	
	req = aux;
	va = req->va;
	n = req->len;
	p = req->buf;
	trans = translator(&cr4);
	while(n > 0){
		ok = trans(va, &pa, cr4);
		if(ok == 0) break;
		if(ok > n) ok = n;
		v = gptr(pa, 1);
		if(v == nil) break;
		pok = gavail(v);
		if(ok > pok) ok = pok;
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
	req.buf = buf;
	req.len = len;
	req.va = va;
	qlock(&req);
	sendnotif(vmemread0, &req);
	qlock(&req);
	return req.rc;
}
