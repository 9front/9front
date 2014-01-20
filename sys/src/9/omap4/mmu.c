#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "arm.h"

char iopages[NIOPAGES / 8];
Lock iopagelock;
uchar *periph;

static int
isfree(int i)
{
	return (iopages[i / 8] & (1 << (i % 8))) == 0;
}

static void
freeio(int i)
{
	iopages[i / 8] &= ~(1 << (i % 8));
}

static int
getiopages(int n)
{
	int i, j;

	lock(&iopagelock);
	for(i = 0; i <= NIOPAGES - n; i++){
		for(j = 0; j < n; j++)
			if(!isfree(i + j))
				goto next;
		for(j = 0; j < n; j++)
			iopages[(i + j) / 8] |= (1 << ((i + j) % 8));
		unlock(&iopagelock);
		return i;
	next: ;
	}
	panic("out of i/o pages");
	return 0;
}

static void
putiopages(int i, int n)
{
	lock(&iopagelock);
	while(n--)
		freeio(i++);
	unlock(&iopagelock);
}

void *
vmap(ulong phys, ulong length)
{
	ulong virt, off, *l2;

	off = phys % BY2PG;
	length = (ROUNDUP(phys + length, BY2PG) - ROUNDDN(phys, BY2PG)) / BY2PG;
	if(length == 0)
		return nil;
	phys = ROUNDDN(phys, BY2PG);
	virt = getiopages(length);
	l2 = KADDR(IOPT);
	l2 += virt;
	while(length--){
		*l2++ = phys | L2AP(Krw) | Small | PTEIO;
		phys += BY2PG;
	}
	flushtlb();
	return (void *) (IZERO + BY2PG * virt + off);
}

void
vunmap(void *virt, ulong length)
{
	ulong v, *l2;
	
	if((ulong)virt < IZERO || (ulong)virt >= IZERO + NIOPAGES * BY2PG)
		panic("vunmap: virt=%p", virt);
	v = (ROUNDDN((ulong) virt, BY2PG) - IZERO) / BY2PG;
	length = (ROUNDUP(((ulong) virt) + length, BY2PG) - ROUNDDN((ulong) virt, BY2PG)) / BY2PG;
	if(length == 0)
		return;
	l2 = KADDR(IOPT);
	l2 += v;
	lock(&iopagelock);
	while(length--){
		*l2++ = 0;
		freeio(v++);
	}
	unlock(&iopagelock);
	flushtlb();
}

void
markidle(int n)
{
	setgpio(7 + m->machno, !n);
}

void
mmuinit(void)
{
	ulong *l1, l2, *pl2;
	int i, n;
	extern ulong *uart;

	l1 = KADDR(L1PT);
	l2 = IOPT;
	n = NIOPAGES / 256;
	memset(KADDR(l2), 0, n * L2SIZ);
	for(i = 0; i < n; i++){
		l1[(IZERO / MiB) + i] = l2 | Coarse;
		l2 += L2SIZ;
	}
	uart = vmap((ulong) uart, BY2PG);
	periph = vmap(0x48240000, 2 * BY2PG);
	memset(l1, 0, sizeof(ulong) * (IZERO / MiB));
	l1[4095] = PRIVL2 | Coarse;
	pl2 = KADDR(PRIVL2);
	for(i = 0; i < 240; i++)
		pl2[i] = (0x8FF00000 + i * BY2PG) | L2AP(Krw) | Small | Cached | Buffered;
	pl2[240] = PHYSVECTORS | L2AP(Krw) | Small | Cached | Buffered;
	pl2[241] = FIRSTMACH | L2AP(Krw) | Small | Cached | Buffered;
	flushtlb();
	m = (Mach *) MACHADDR;
}

void
mmuswitch(Proc *p)
{
	ulong *l1;
	
	l1 = KADDR(L1PT);
	memmove(l1, p->l1, sizeof p->l1);
	flushtlb();
}

void
putmmu(uintptr va, uintptr pa, Page *)
{
	ulong *l1a, *l1b, *l2;
	int l1o, l2o;
	
	l1o = va / MiB;
	l2o = (va % MiB) / BY2PG;
	l1a = KADDR(L1PT);
	l1b = up->l1;
	if(l1a[l1o] == 0){
		if((pa & PTEVALID) == 0)
			return;
		l2 = xspanalloc(L2SIZ, L2SIZ, 0);
		l1a[l1o] = l1b[l1o] = PADDR(l2) | Coarse;
	} else
		l2 = KADDR(ROUNDDN(l1a[l1o], L2SIZ));
	l2 += l2o;
	if((pa & PTEVALID) == 0){
		*l2 = 0;
		flushtlb();
		return;
	}
	*l2 = ROUNDDN(pa, BY2PG) | Small;
	if((pa & PTEWRITE) == 0)
		*l2 |= L2AP(Uro);
	else
		*l2 |= L2AP(Urw);
	if((pa & PTEUNCACHED) == 0)
		*l2 |= Buffered | Cached;
	flushtlb();
}

void
flushmmu(void)
{
	int s, i;
	ulong p;
	ulong *l1;

	l1 = KADDR(L1PT);
	s = splhi();
	for(i = 0; i < nelem(up->l1); i++){
		p = l1[i];
		if(p & Small)
			free(KADDR(ROUNDDN(p, BY2PG)));
	}
	memset(up->l1, 0, sizeof up->l1);
	memset(l1, 0, sizeof up->l1);
	flushtlb();
	splx(s);
}

void
mmurelease(Proc *p)
{
	int i;
	ulong pg;

	if(p == up){
		flushmmu();
		return;
	}
	for(i = 0; i < nelem(p->l1); i++){
		pg = p->l1[i];
		if(pg & Small)
			free(KADDR(ROUNDDN(pg, BY2PG)));
	}
	memset(p->l1, 0, sizeof p->l1);
}

void
countpagerefs()
{
	panic("countpagerefs");
}

void*
KADDR(ulong pa)
{
	if(pa < (ulong)PHYSDRAM || pa > (ulong)(PHYSDRAM + VECTORS - KZERO))
		panic("kaddr: pa=%#.8lux, pc=%p", pa, getcallerpc(&pa));
	return (void*)(pa + KZERO - PHYSDRAM);
}

ulong
paddr(void* v)
{
	ulong va;
	
	va = (ulong) v;
	if(va < KZERO)
		panic("paddr: v=%p", v);
	return va - KZERO + PHYSDRAM;
}

ulong
cankaddr(ulong arg)
{
	if(arg < PHYSDRAM || arg > (ulong)(PHYSDRAM + VECTORS - KZERO))
		return 0;
	return PHYSDRAM - KZERO - arg;
}
