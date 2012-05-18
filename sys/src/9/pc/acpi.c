#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

typedef struct Rsd Rsd;
typedef struct Tbl Tbl;

struct Rsd {
	uchar	sig[8];
	uchar	csum;
	uchar	oemid[6];
	uchar	rev;
	uchar	raddr[4];
	uchar	len[4];
	uchar	xaddr[8];
	uchar	xcsum;
	uchar	reserved[3];
};

struct Tbl {
	uchar	sig[4];
	uchar	len[4];
	uchar	rev;
	uchar	csum;
	uchar	oemid[6];
	uchar	oemtid[8];
	uchar	oemrev[4];
	uchar	cid[4];
	uchar	crev[4];
	uchar	data[];
};

static int ntbltab;
static Tbl *tbltab[64];

static ushort
get16(uchar *p){
	return p[1]<<8 | p[0];
}

static uint
get32(uchar *p){
	return p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
}

static uvlong
get64(uchar *p){
	uvlong u;

	u = get32(p+4);
	return u<<32 | get32(p);
}

static int
checksum(void *v, int n)
{
	uchar *p, s;

	s = 0;
	p = v;
	while(n-- > 0)
		s += *p++;
	return s;
}

static void*
rsdscan(uchar* addr, int len, char* sig)
{
	int sl;
	uchar *e, *p;

	e = addr+len;
	sl = strlen(sig);
	for(p = addr; p+sl < e; p += 16){
		if(memcmp(p, sig, sl))
			continue;
		return p;
	}
	return nil;
}

static void*
rsdsearch(char* sig)
{
	uintptr p;
	uchar *bda;
	Rsd *rsd;

	/*
	 * Search for the data structure signature:
	 * 1) in the first KB of the EBDA;
	 * 2) in the BIOS ROM between 0xE0000 and 0xFFFFF.
	 */
	if(strncmp((char*)KADDR(0xFFFD9), "EISA", 4) == 0){
		bda = KADDR(0x400);
		if((p = (bda[0x0F]<<8)|bda[0x0E]))
			if(rsd = rsdscan(KADDR(p), 1024, sig))
				return rsd;
	}
	return rsdscan(KADDR(0xE0000), 0x20000, sig);
}

static void
maptable(uvlong xpa)
{
	uchar *p, *e;
	Tbl *t, *a;
	uintptr pa;
	u32int l;
	int i;

	pa = xpa;
	if((uvlong)pa != xpa || pa == 0)
		return;
	if(ntbltab >= nelem(tbltab))
		return;
	if((t = vmap(pa, 8)) == nil)
		return;
	l = get32(t->len);
	if(l < sizeof(Tbl) || t->sig[0] == 0){
		vunmap(t, 8);
		return;
	}
	for(i=0; i<ntbltab; i++){
		if(memcmp(tbltab[i]->sig, t->sig, sizeof(t->sig)) == 0)
			break;
	}
	vunmap(t, 8);
	if(i < ntbltab)
		return;
	if((a = malloc(l)) == nil)
		return;
	if((t = vmap(pa, l)) == nil){
		free(a);
		return;
	}
	if(checksum(t, l)){
		vunmap(t, l);
		free(a);
		return;
	}
	memmove(a, t, l);
	vunmap(t, l);

	tbltab[ntbltab++] = t = a;

	if(0) print("acpi: %llux %.4s %d\n", xpa, t->sig, l);

	p = (uchar*)t;
	e = p + l;
	if(memcmp("RSDT", t->sig, 4) == 0){
		for(p = t->data; p+3 < e; p += 4)
			maptable(get32(p));
		return;
	}
	if(memcmp("XSDT", t->sig, 4) == 0){
		for(p = t->data; p+7 < e; p += 8)
			maptable(get64(p));
		return;
	}
	if(memcmp("FACP", t->sig, 4) == 0){
		if(l < 44)
			return;
		maptable(get32(p + 40));
		if(l < 148)
			return;
		maptable(get64(p + 140));
		return;
	}
}

static long
readtbls(Chan*, void *v, long n, vlong o)
{
	int i, l, m;
	uchar *p;
	Tbl *t;

	p = v;
	for(i=0; n > 0 && i < ntbltab; i++){
		t = tbltab[i];
		l = get32(t->len);
		if(o >= l){
			o -= l;
			continue;
		}
		m = l - o;
		if(m > n)
			m = n;
		memmove(p, (uchar*)t + o, m);
		p += m;
		n -= m;
		o = 0;
	}
	return p - (uchar*)v;
}

void
acpilink(void)
{
	Rsd *r;

	/*
	 * this is a debug driver to dump the acpi tables.
	 * do nothing unless *acpi gets explicitly enabled.
	 */
	if(getconf("*acpi") == nil)
		return;
	if((r = rsdsearch("RSD PTR ")) == nil)
		return;
	if(checksum(r, 20) == 0)
		maptable(get32(r->raddr));
	if(r->rev >= 2)
		if(checksum(r, 36) == 0)
			maptable(get64(r->xaddr));
	addarchfile("acpitbls", 0444, readtbls, nil);
}
