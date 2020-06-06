#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"

enum {
	MemUPA		= 0,	/* unbacked physical address */
	MemUMB		= 1,	/* upper memory block (<16MB) */
	MemRAM		= 2,	/* physical memory */
	MemACPI		= 3,	/* ACPI tables */
	MemReserved	= 4,	/* don't allocate */

	KB = 1024,
};

u32int	MemMin;		/* set by l.s */

void*
rampage(void)
{
	uintptr pa;
	
	if(conf.mem[0].npage != 0)
		return xspanalloc(BY2PG, BY2PG, 0);

	/*
	 * Allocate from the map directly to make page tables.
	 */
	pa = memmapalloc(-1, BY2PG, BY2PG, MemRAM);
	if(pa == -1 || cankaddr(pa) == 0)
		panic("rampage: out of memory\n");
	return KADDR(pa);
}

static void
mapkzero(uintptr base, uintptr len, int type)
{
	uintptr flags, n;

	if(base < MemMin && base+len > MemMin){
		mapkzero(base, MemMin-base, type);
		len = base+len-MemMin;
		base = MemMin;
	}

	n = cankaddr(base);
	if(n == 0)
		return;
	if(len > n)
		len = n;

	switch(type){
	default:
		return;
	case MemRAM:
		if(base < MemMin)
			return;
		flags = PTEWRITE|PTEVALID;
		break;
	case MemUMB:
		if(base < MemMin)
			punmap(base+KZERO, len);
		flags = PTEWRITE|PTEUNCACHED|PTEVALID;
		break;
	}
#ifdef PTENOEXEC
	flags |= PTENOEXEC;
#endif
	pmap(base|flags, base+KZERO, len);
}

static uintptr
ebdaseg(void)
{
	uchar *bda;

	if(memcmp(KADDR(0xfffd9), "EISA", 4) != 0)
		return 0;
	bda = KADDR(0x400);
	return ((bda[0x0f]<<8)|bda[0x0e]) << 4;
}

static uintptr
convmemsize(void)
{
	uintptr top;
	uchar *bda;

	bda = KADDR(0x400);
	top = ((bda[0x14]<<8) | bda[0x13])*KB;

	if(top < 64*KB || top > 640*KB)
		top = 640*KB;	/* sanity */

	/* Reserved for BIOS tables */
	top -= 1*KB;

	return top;
}

static void
lowraminit(void)
{
	uintptr base, pa, len;
	uchar *p;

	/*
	 * Discover the memory bank information for conventional memory
	 * (i.e. less than 640KB). The base is the first location after the
	 * bootstrap processor MMU information and the limit is obtained from
	 * the BIOS data area.
	 */
	base = PADDR(CPU0END);
	pa = convmemsize();
	if(base < pa)
		memmapadd(base, pa-base, MemRAM);

	/* Reserve BIOS tables */
	memmapadd(pa, 1*KB, MemReserved);

	/* Reserve EBDA */
	if((pa = ebdaseg()) != 0)
		memmapadd(pa, 1*KB, MemReserved);
	memmapadd(0xA0000-1*KB, 1*KB, MemReserved);

	/* Reserve the VGA frame buffer */
	umballoc(0xA0000, 128*KB, 0);

	/* Reserve VGA ROM */
	memmapadd(0xC0000, 64*KB, MemReserved);

	/*
	 * Scan the Upper Memory Blocks (0xD0000->0xF0000) for device BIOS ROMs.
	 * This should start with a two-byte header of 0x55 0xAA, followed by a
	 * byte giving the size of the ROM in 512-byte chunks.
	 * These ROM's must start on a 2KB boundary.
	 */
	for(p = (uchar*)KADDR(0xD0000); p < (uchar*)KADDR(0xF0000); p += len){
		len = 2*KB;
		if(p[0] == 0x55 && p[1] == 0xAA){
			if(p[2] != 0)
				len = p[2]*512;
			memmapadd(PADDR(p), len, MemReserved);
			len = ROUND(len, 2*KB);
		}
	}

	/* Reserve BIOS ROM */
	memmapadd(0xF0000, 64*KB, MemReserved);
}

int
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
sigscan(uchar *addr, int len, char *sig, int size, int step)
{
	uchar *e, *p;
	int sl;

	sl = strlen(sig);
	e = addr+len-(size > sl ? size : sl);
	for(p = addr; p <= e; p += step){
		if(memcmp(p, sig, sl) != 0)
			continue;
		if(size && checksum(p, size) != 0)
			continue;
		return p;
	}
	return nil;
}

void*
sigsearch(char* signature, int size)
{
	uintptr p;
	void *r;

	/*
	 * Search for the data structure:
	 * 1) within the first KiB of the Extended BIOS Data Area (EBDA), or
	 * 2) within the last KiB of system base memory if the EBDA segment
	 *    is undefined, or
	 * 3) within the BIOS ROM address space between 0xf0000 and 0xfffff
	 *    (but will actually check 0xe0000 to 0xfffff).
	 */
	if((p = ebdaseg()) != 0){
		if((r = sigscan(KADDR(p), 1*KB, signature, size, 16)) != nil)
			return r;
	}
	if((r = sigscan(KADDR(convmemsize()), 1*KB, signature, size, 16)) != nil)
		return r;

	/* hack for virtualbox: look in KiB below 0xa0000 */
	if((r = sigscan(KADDR(0xA0000-1*KB), 1*KB, signature, size, 16)) != nil)
		return r;

	return sigscan(KADDR(0xE0000), 128*KB, signature, size, 16);
}

void*
rsdsearch(void)
{
	static char signature[] = "RSD PTR ";
	uintptr base, size;
	uchar *v, *p;

	if((p = sigsearch(signature, 36)) != nil)
		return p;
	if((p = sigsearch(signature, 20)) != nil)
		return p;

	for(base = memmapnext(-1, MemACPI); base != -1; base = memmapnext(base, MemACPI)){
		size = memmapsize(base, 0);
		if(size == 0 || size > 0x7fffffff)
			continue;
		if((v = vmap(base, size)) != nil){
			p = sigscan(v, size, signature, 36, 4);
			if(p == nil)
				p = sigscan(v, size, signature, 20, 4);
			vunmap(v, size);
			if(p != nil)
				return vmap(base + (p - v), 64);
		}
	}
	return nil;
}

/*
 * Give out otherwise-unused physical address space
 * for use in configuring devices.  Note that upaalloc
 * does not map the physical address into virtual memory.
 * Call vmap to do that.
 */
uvlong
upaalloc(uvlong pa, ulong size, ulong align)
{
	return memmapalloc(pa, size, align, MemUPA);
}

void
upafree(uvlong pa, ulong size)
{
	memmapfree(pa, size, MemUPA);
}

/*
 * Allocate memory from the upper memory blocks.
 */
ulong
umballoc(ulong pa, ulong size, ulong align)
{
	return (ulong)memmapalloc(pa == -1UL ? -1ULL : (uvlong)pa, size, align, MemUMB);
}

void
umbfree(ulong pa, ulong size)
{
	memmapfree(pa, size, MemUMB);
}

static void
umbexclude(void)
{
	ulong pa, size;
	char *op, *p, *rptr;

	if((p = getconf("umbexclude")) == nil)
		return;

	while(p && *p != '\0' && *p != '\n'){
		op = p;
		pa = strtoul(p, &rptr, 0);
		if(rptr == nil || rptr == p || *rptr != '-'){
			print("umbexclude: invalid argument <%s>\n", op);
			break;
		}
		p = rptr+1;

		size = strtoul(p, &rptr, 0) - pa + 1;
		if(size <= 0){
			print("umbexclude: bad range <%s>\n", op);
			break;
		}
		if(rptr != nil && *rptr == ',')
			*rptr++ = '\0';
		p = rptr;

		memmapalloc(pa, size, 0, MemUMB);
	}
}

static int
e820scan(void)
{
	uvlong base, top, size;
	int type;
	char *s;

	/* passed by bootloader */
	if((s = getconf("*e820")) == nil)
		if((s = getconf("e820")) == nil)
			return -1;

	for(;;){
		while(*s == ' ')
			s++;
		if(*s == 0)
			break;
		type = 1;
		if(s[1] == ' '){	/* new format */
			type = s[0] - '0';
			s += 2;
		}
		base = strtoull(s, &s, 16);
		if(*s != ' ')
			break;
		top  = strtoull(s, &s, 16);
		if(*s != ' ' && *s != 0)
			break;
		if(base >= top)
			continue;
		switch(type){
		case 1:
			memmapadd(base, top - base, MemRAM);
			break;
		case 3:
			memmapadd(base, top - base, MemACPI);
			break;
		default:
			memmapadd(base, top - base, MemReserved);
		}
	}

	for(base = memmapnext(-1, MemRAM); base != -1; base = memmapnext(base, MemRAM)){
		size = memmapsize(base, BY2PG) & ~(BY2PG-1);
		if(size != 0)
			mapkzero(PGROUND(base), size, MemRAM);
	}

	return 0;
}

static void
ramscan(uintptr pa, uintptr top, uintptr chunk)
{
	ulong save, pat, seed, *v, *k0;
	int i, n, w;

	pa += chunk-1;
	pa &= ~(chunk-1);
	top &= ~(chunk-1);

	n = chunk/sizeof(*v);
	w = BY2PG/sizeof(*v);

	k0 = KADDR(0);
	save = *k0;

	pat = 0x12345678UL;
	for(; pa < top; pa += chunk){
		/* write pattern */
		seed = pat;
		if((v = vmap(pa, chunk)) == nil)
			continue;
		for(i = 0; i < n; i += w){
			pat += 0x3141526UL;
			v[i] = pat;
			*k0 = ~pat;
			if(v[i] != pat)
				goto Bad;
		}
		vunmap(v, chunk);

		/* verify pattern */
		pat = seed;
		if((v = vmap(pa, chunk)) == nil)
			continue;
		for(i = 0; i < n; i += w){
			pat += 0x3141526UL;
			if(v[i] != pat)
				goto Bad;
		}
		vunmap(v, chunk);

		memmapadd(pa, chunk, MemRAM);
		mapkzero(pa, chunk, MemRAM);
		continue;

	Bad:
		vunmap(v, chunk);

		if(pa+chunk <= 16*MB)
			memmapadd(pa, chunk, MemUMB);

		/*
		 * If we encounter a chunk of missing memory
		 * at a sufficiently high offset, call it the end of
		 * memory.  Otherwise we run the risk of thinking
		 * that video memory is real RAM.
		 */
		if(pa >= 32*MB)
			break;
	}

	*k0 = save;
}

/*
 * Sort out initial memory map and discover RAM.
 */
void
meminit0(void)
{
	/*
	 * Add the already mapped memory after the kernel.
	 */
	if(MemMin < PADDR(PGROUND((uintptr)end)))
		panic("kernel too big");
	memmapadd(PADDR(PGROUND((uintptr)end)), MemMin-PADDR(PGROUND((uintptr)end)), MemRAM);

	/*
	 * Memory between KTZERO and end is the kernel itself.
	 */
	memreserve(PADDR(KTZERO), PADDR(PGROUND((uintptr)end))-PADDR(KTZERO));

	/*
	 * Memory below CPU0END is reserved for the kernel.
	 */
	memreserve(0, PADDR(CPU0END));

	/*
	 * Addresses below 16MB default to be upper
	 * memory blocks usable for ISA devices.
	 */
	memmapadd(0, 16*MB, MemUMB);

	/*
	 * Everything between 16MB and 4GB defaults
	 * to unbacked physical addresses usable for
	 * device mappings.
	 */
	memmapadd(16*MB, (u32int)-16*MB, MemUPA);

	/*
	 * On 386, reserve >= 4G as we have no PAE support.
	 */
	if(sizeof(void*) == 4)
		memmapadd((u32int)-BY2PG, -((uvlong)((u32int)-BY2PG)), MemReserved);

	/*
	 * Discover conventional RAM, ROMs and UMBs.
	 */
	lowraminit();

	/*
	 * Discover more RAM and map to KZERO.
	 */
	if(e820scan() < 0)
		ramscan(MemMin, -((uintptr)MemMin), 4*MB);
}

/*
 * Until the memory map is finalized by meminit(),
 * archinit() should reserve memory of discovered BIOS
 * and ACPI tables by calling memreserve() to prevent
 * them from getting allocated and trashed.
 * This is due to the UEFI and BIOS memory map being
 * unreliable and sometimes marking these ranges as RAM.
 */
void
memreserve(uintptr pa, uintptr size)
{
	assert(conf.mem[0].npage == 0);

	size += (pa & BY2PG-1);
	size &= ~(BY2PG-1);
	pa &= ~(BY2PG-1);
	memmapadd(pa, size, MemReserved);
}

/*
 * Finalize the memory map:
 *  (re-)map the upper memory blocks
 *  allocate all usable ram to the conf.mem[] banks
 */
void
meminit(void)
{
	uintptr base, size;
	Confmem *cm;

	umbexclude();
	for(base = memmapnext(-1, MemUMB); base != -1; base = memmapnext(base, MemUMB)){
		size = memmapsize(base, BY2PG) & ~(BY2PG-1);
		if(size != 0)
			mapkzero(PGROUND(base), size, MemUMB);
	}

	cm = &conf.mem[0];
	for(base = memmapnext(-1, MemRAM); base != -1; base = memmapnext(base, MemRAM)){
		size = memmapsize(base, BY2PG) & ~(BY2PG-1);
		if(size == 0)
			continue;
		cm->base = memmapalloc(base, size, BY2PG, MemRAM);
		if(cm->base == -1)
			continue;
		base = cm->base;
		cm->npage = size/BY2PG;
		if(++cm >= &conf.mem[nelem(conf.mem)])
			break;
	}

	if(0) memmapdump();
}
