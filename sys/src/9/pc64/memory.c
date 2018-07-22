/*
 * Size memory and create the kernel page-tables on the fly while doing so.
 * Called from main(), this code should only be run by the bootstrap processor.
 *
 * MemMin is what the bootstrap code in l.s has already mapped;
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"

u32int	MemMin;		/* set by l.s */

#define MEMDEBUG	0

enum {
	MemUPA		= 0,		/* unbacked physical address */
	MemRAM		= 1,		/* physical memory */
	MemUMB		= 2,		/* upper memory block (<16MB) */
	MemACPI		= 3,		/* ACPI tables */
	MemReserved	= 4,
	NMemType	= 5,

	KB		= 1024,
};

typedef struct Map Map;
struct Map {
	uintptr	size;
	uintptr	addr;
};

typedef struct RMap RMap;
struct RMap {
	char*	name;
	Map*	map;
	Map*	mapend;

	Lock;
};

/* 
 * Memory allocation tracking.
 */
static Map mapupa[64];
static RMap rmapupa = {
	"unallocated unbacked physical memory",
	mapupa,
	&mapupa[nelem(mapupa)-1],
};

static Map mapram[16];
static RMap rmapram = {
	"physical memory",
	mapram,
	&mapram[nelem(mapram)-1],
};

static Map mapumb[64];
static RMap rmapumb = {
	"upper memory block",
	mapumb,
	&mapumb[nelem(mapumb)-1],
};

static Map mapumbrw[16];
static RMap rmapumbrw = {
	"UMB device memory",
	mapumbrw,
	&mapumbrw[nelem(mapumbrw)-1],
};

static Map mapacpi[16];
static RMap rmapacpi = {
	"ACPI tables",
	mapacpi,
	&mapacpi[nelem(mapacpi)-1],
};

void
mapprint(RMap *rmap)
{
	Map *mp;

	print("%s\n", rmap->name);	
	for(mp = rmap->map; mp->size; mp++)
		print("\t%#p %#p (%#p)\n", mp->addr, mp->addr+mp->size, mp->size);
}


void
memdebug(void)
{
	ulong maxpa, maxpa1, maxpa2;

	maxpa = (nvramread(0x18)<<8)|nvramread(0x17);
	maxpa1 = (nvramread(0x31)<<8)|nvramread(0x30);
	maxpa2 = (nvramread(0x16)<<8)|nvramread(0x15);
	print("maxpa = %luX -> %luX, maxpa1 = %luX maxpa2 = %luX\n",
		maxpa, MB+maxpa*KB, maxpa1, maxpa2);

	mapprint(&rmapram);
	mapprint(&rmapumb);
	mapprint(&rmapumbrw);
	mapprint(&rmapupa);
	mapprint(&rmapacpi);
}

static void
mapfree(RMap* rmap, uintptr addr, uintptr size)
{
	Map *mp;
	uintptr t;

	if(size <= 0)
		return;

	lock(rmap);
	for(mp = rmap->map; mp->addr <= addr && mp->size; mp++)
		;

	if(mp > rmap->map && (mp-1)->addr+(mp-1)->size == addr){
		(mp-1)->size += size;
		if(addr+size == mp->addr){
			(mp-1)->size += mp->size;
			while(mp->size){
				mp++;
				(mp-1)->addr = mp->addr;
				(mp-1)->size = mp->size;
			}
		}
	}
	else{
		if(addr+size == mp->addr && mp->size){
			mp->addr -= size;
			mp->size += size;
		}
		else do{
			if(mp >= rmap->mapend){
				print("mapfree: %s: losing %#p, %#p\n",
					rmap->name, addr, size);
				break;
			}
			t = mp->addr;
			mp->addr = addr;
			addr = t;
			t = mp->size;
			mp->size = size;
			mp++;
		}while(size = t);
	}
	unlock(rmap);
}

static uintptr
mapalloc(RMap* rmap, uintptr addr, int size, int align)
{
	Map *mp;
	uintptr maddr, oaddr;

	lock(rmap);
	for(mp = rmap->map; mp->size; mp++){
		maddr = mp->addr;

		if(addr){
			/*
			 * A specific address range has been given:
			 *   if the current map entry is greater then
			 *   the address is not in the map;
			 *   if the current map entry does not overlap
			 *   the beginning of the requested range then
			 *   continue on to the next map entry;
			 *   if the current map entry does not entirely
			 *   contain the requested range then the range
			 *   is not in the map.
			 */
			if(maddr > addr)
				break;
			if(mp->size < addr - maddr)	/* maddr+mp->size < addr, but no overflow */
				continue;
			if(addr - maddr > mp->size - size)	/* addr+size > maddr+mp->size, but no overflow */
				break;
			maddr = addr;
		}

		if(align > 0)
			maddr = ((maddr+align-1)/align)*align;
		if(mp->addr+mp->size-maddr < size)
			continue;

		oaddr = mp->addr;
		mp->addr = maddr+size;
		mp->size -= maddr-oaddr+size;
		if(mp->size == 0){
			do{
				mp++;
				(mp-1)->addr = mp->addr;
			}while((mp-1)->size = mp->size);
		}

		unlock(rmap);
		if(oaddr != maddr)
			mapfree(rmap, oaddr, maddr-oaddr);

		return maddr;
	}
	unlock(rmap);

	return 0;
}

/*
 * Allocate from the ram map directly to make page tables.
 * Called by mmuwalk during e820scan.
 */
void*
rampage(void)
{
	uintptr m;
	
	m = mapalloc(&rmapram, 0, BY2PG, BY2PG);
	if(m == 0)
		return nil;
	return KADDR(m);
}

static void
umbexclude(void)
{
	int size;
	ulong addr;
	char *op, *p, *rptr;

	if((p = getconf("umbexclude")) == nil)
		return;

	while(p && *p != '\0' && *p != '\n'){
		op = p;
		addr = strtoul(p, &rptr, 0);
		if(rptr == nil || rptr == p || *rptr != '-'){
			print("umbexclude: invalid argument <%s>\n", op);
			break;
		}
		p = rptr+1;

		size = strtoul(p, &rptr, 0) - addr + 1;
		if(size <= 0){
			print("umbexclude: bad range <%s>\n", op);
			break;
		}
		if(rptr != nil && *rptr == ',')
			*rptr++ = '\0';
		p = rptr;

		mapalloc(&rmapumb, addr, size, 0);
	}
}

static void
umbscan(void)
{
	uchar *p;

	/*
	 * Scan the Upper Memory Blocks (0xA0000->0xF0000) for pieces
	 * which aren't used; they can be used later for devices which
	 * want to allocate some virtual address space.
	 * Check for two things:
	 * 1) device BIOS ROM. This should start with a two-byte header
	 *    of 0x55 0xAA, followed by a byte giving the size of the ROM
	 *    in 512-byte chunks. These ROM's must start on a 2KB boundary.
	 * 2) device memory. This is read-write.
	 * There are some assumptions: there's VGA memory at 0xA0000 and
	 * the VGA BIOS ROM is at 0xC0000. Also, if there's no ROM signature
	 * at 0xE0000 then the whole 64KB up to 0xF0000 is theoretically up
	 * for grabs; check anyway.
	 */
	p = KADDR(0xD0000);
	while(p < (uchar*)KADDR(0xE0000)){
		/*
		 * Test for 0x55 0xAA before poking obtrusively,
		 * some machines (e.g. Thinkpad X20) seem to map
		 * something dynamic here (cardbus?) causing weird
		 * problems if it is changed.
		 */
		if(p[0] == 0x55 && p[1] == 0xAA){
			p += p[2]*512;
			continue;
		}

		p[0] = 0xCC;
		p[2*KB-1] = 0xCC;
		if(p[0] != 0xCC || p[2*KB-1] != 0xCC){
			p[0] = 0x55;
			p[1] = 0xAA;
			p[2] = 4;
			if(p[0] == 0x55 && p[1] == 0xAA){
				p += p[2]*512;
				continue;
			}
			if(p[0] == 0xFF && p[1] == 0xFF)
				mapfree(&rmapumb, PADDR(p), 2*KB);
		}
		else
			mapfree(&rmapumbrw, PADDR(p), 2*KB);
		p += 2*KB;
	}

	p = KADDR(0xE0000);
	if(p[0] != 0x55 || p[1] != 0xAA){
		p[0] = 0xCC;
		p[64*KB-1] = 0xCC;
		if(p[0] != 0xCC && p[64*KB-1] != 0xCC)
			mapfree(&rmapumb, PADDR(p), 64*KB);
	}

	umbexclude();
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

static uintptr
convmemsize(void)
{
	uintptr top;
	uchar *bda;

	bda = KADDR(0x400);
	top = ((bda[0x14]<<8) | bda[0x13])*KB;

	if(top < 64*KB || top > 640*KB)
		top = 640*KB;	/* sanity */

	/* reserved for bios tables (EBDA) */
	top -= 1*KB;

	return top;
}

void*
sigsearch(char* signature, int size)
{
	uintptr p;
	uchar *bda;
	void *r;

	/*
	 * Search for the data structure:
	 * 1) within the first KiB of the Extended BIOS Data Area (EBDA), or
	 * 2) within the last KiB of system base memory if the EBDA segment
	 *    is undefined, or
	 * 3) within the BIOS ROM address space between 0xf0000 and 0xfffff
	 *    (but will actually check 0xe0000 to 0xfffff).
	 */
	bda = KADDR(0x400);
	if(memcmp(KADDR(0xfffd9), "EISA", 4) == 0){
		if((p = (bda[0x0f]<<8)|bda[0x0e]) != 0){
			if((r = sigscan(KADDR(p<<4), 1024, signature, size, 16)) != nil)
				return r;
		}
	}
	if((r = sigscan(KADDR(convmemsize()), 1024, signature, size, 16)) != nil)
		return r;

	/* hack for virtualbox: look in KiB below 0xa0000 */
	if((r = sigscan(KADDR(0xa0000-1024), 1024, signature, size, 16)) != nil)
		return r;

	return sigscan(KADDR(0xe0000), 0x20000, signature, size, 16);
}

void*
rsdsearch(void)
{
	static char signature[] = "RSD PTR ";
	uchar *v, *p;
	Map *m;

	if((p = sigsearch(signature, 36)) != nil)
		return p;
	if((p = sigsearch(signature, 20)) != nil)
		return p;
	for(m = rmapacpi.map; m < rmapacpi.mapend && m->size; m++){
		if(m->size > 0x7FFFFFFF)
			continue;
		if((v = vmap(m->addr, m->size)) != nil){
			p = sigscan(v, m->size, signature, 36, 4);
			if(p == nil)
				p = sigscan(v, m->size, signature, 20, 4);
			vunmap(v, m->size);
			if(p != nil)
				return vmap(m->addr + (p - v), 64);
		}
	}
	return nil;
}

static void
lowraminit(void)
{
	uintptr pa, x;

	/*
	 * Initialise the memory bank information for conventional memory
	 * (i.e. less than 640KB). The base is the first location after the
	 * bootstrap processor MMU information and the limit is obtained from
	 * the BIOS data area.
	 */
	x = PADDR(CPU0END);
	pa = convmemsize();
	if(x < pa){
		mapfree(&rmapram, x, pa-x);
		memset(KADDR(x), 0, pa-x);		/* keep us honest */
	}

	x = PADDR(PGROUND((uintptr)end));
	pa = MemMin;
	if(x > pa)
		panic("kernel too big");
	mapfree(&rmapram, x, pa-x);
	memset(KADDR(x), 0, pa-x);		/* keep us honest */
}

typedef struct Emap Emap;
struct Emap
{
	int type;
	uvlong base;
	uvlong top;
};
static Emap emap[128];
int nemap;

static int
emapcmp(const void *va, const void *vb)
{
	Emap *a, *b;
	
	a = (Emap*)va;
	b = (Emap*)vb;
	if(a->top < b->top)
		return -1;
	if(a->top > b->top)
		return 1;
	if(a->base < b->base)
		return -1;
	if(a->base > b->base)
		return 1;
	return 0;
}

static void
map(uintptr base, uintptr len, int type)
{
	uintptr n, flags, maxkpa;

	/*
	 * Split any call crossing MemMin to make below simpler.
	 */
	if(base < MemMin && len > MemMin-base){
		n = MemMin - base;
		map(base, n, type);
		map(MemMin, len-n, type);
	}
	
	/*
	 * Let umbscan hash out the low MemMin.
	 */
	if(base < MemMin)
		return;

	/*
	 * Any non-memory below 16*MB is used as upper mem blocks.
	 */
	if(type == MemUPA && base < 16*MB && len > 16*MB-base){
		map(base, 16*MB-base, MemUMB);
		map(16*MB, len-(16*MB-base), MemUPA);
		return;
	}
	
	/*
	 * Memory below CPU0END is reserved for the kernel
	 * and already mapped.
	 */
	if(base < PADDR(CPU0END)){
		n = PADDR(CPU0END) - base;
		if(len <= n)
			return;
		map(PADDR(CPU0END), len-n, type);
		return;
	}
	
	/*
	 * Memory between KTZERO and end is the kernel itself
	 * and is already mapped.
	 */
	if(base < PADDR(KTZERO) && len > PADDR(KTZERO)-base){
		map(base, PADDR(KTZERO)-base, type);
		return;
	}
	if(PADDR(KTZERO) < base && base < PADDR(PGROUND((uintptr)end))){
		n = PADDR(PGROUND((uintptr)end));
		if(len <= n)
			return;
		map(PADDR(PGROUND((uintptr)end)), len-n, type);
		return;
	}
	
	/*
	 * Now we have a simple case.
	 */
	switch(type){
	case MemRAM:
		mapfree(&rmapram, base, len);
		flags = PTEWRITE|PTEVALID;
		break;
	case MemUMB:
		mapfree(&rmapumb, base, len);
		flags = PTEWRITE|PTEUNCACHED|PTEVALID;
		break;
	case MemUPA:
		mapfree(&rmapupa, base, len);
		flags = 0;
		break;
	case MemACPI:
		mapfree(&rmapacpi, base, len);
		flags = 0;
		break;
	case MemReserved:
	default:
		flags = 0;
		break;
	}
	
	if(flags){
		maxkpa = -KZERO;
		if(base >= maxkpa)
			return;
		if(len > maxkpa-base)
			len = maxkpa - base;
		pmap(m->pml4, base|flags, base+KZERO, len);
	}
}

static int
e820scan(void)
{
	uintptr base, len, last;
	Emap *e;
	char *s;
	int i;

	/* passed by bootloader */
	if((s = getconf("*e820")) == nil)
		if((s = getconf("e820")) == nil)
			return -1;
	nemap = 0;
	while(nemap < nelem(emap)){
		while(*s == ' ')
			s++;
		if(*s == 0)
			break;
		e = emap + nemap;
		e->type = 1;
		if(s[1] == ' '){	/* new format */
			e->type = s[0] - '0';
			s += 2;
		}
		e->base = strtoull(s, &s, 16);
		if(*s != ' ')
			break;
		e->top  = strtoull(s, &s, 16);
		if(*s != ' ' && *s != 0)
			break;
		if(e->base < e->top)
			nemap++;
	}
	if(nemap == 0)
		return -1;
	qsort(emap, nemap, sizeof emap[0], emapcmp);
	last = 0;
	for(i=0; i<nemap; i++){	
		e = &emap[i];
		/*
		 * pull out the info but only about the low 32 bits...
		 */
		if(e->top <= last)
			continue;
		if(e->base < last)
			base = last;
		else
			base = e->base;
		len = e->top - base;
		/*
		 * If the map skips addresses, mark them available.
		 */
		if(last < base)
			map(last, base-last, MemUPA);
		switch(e->type){
		case 1:
			map(base, len, MemRAM);
			break;
		case 3:
			map(base, len, MemACPI);
			break;
		default:
			map(base, len, MemReserved);
		}
		last = base + len;
		if(last == 0)
			break;
	}
	if(last != 0)
		map(last, -last, MemUPA);
	return 0;
}

void
meminit(void)
{
	int i;
	Map *mp;
	Confmem *cm;
	uintptr lost;

	umbscan();
	lowraminit();
	e820scan();

	/*
	 * Set the conf entries describing banks of allocatable memory.
	 */
	for(i=0; i<nelem(mapram) && i<nelem(conf.mem); i++){
		mp = &rmapram.map[i];
		cm = &conf.mem[i];
		cm->base = mp->addr;
		cm->npage = mp->size/BY2PG;
	}

	lost = 0;
	for(; i<nelem(mapram); i++)
		lost += rmapram.map[i].size;
	if(lost)
		print("meminit - lost %llud bytes\n", lost);

	if(MEMDEBUG)
		memdebug();
}

/*
 * Allocate memory from the upper memory blocks.
 */
uintptr
umbmalloc(uintptr addr, int size, int align)
{
	uintptr a;

	if(a = mapalloc(&rmapumb, addr, size, align))
		return (uintptr)KADDR(a);

	return 0;
}

void
umbfree(uintptr addr, int size)
{
	mapfree(&rmapumb, PADDR(addr), size);
}

uintptr
umbrwmalloc(uintptr addr, int size, int align)
{
	uintptr a;
	uchar *p;

	if(a = mapalloc(&rmapumbrw, addr, size, align))
		return (uintptr)KADDR(a);

	/*
	 * Perhaps the memory wasn't visible before
	 * the interface is initialised, so try again.
	 */
	if((a = umbmalloc(addr, size, align)) == 0)
		return 0;
	p = (uchar*)a;
	p[0] = 0xCC;
	p[size-1] = 0xCC;
	if(p[0] == 0xCC && p[size-1] == 0xCC)
		return a;
	umbfree(a, size);

	return 0;
}

void
umbrwfree(uintptr addr, int size)
{
	mapfree(&rmapumbrw, PADDR(addr), size);
}

/*
 * Give out otherwise-unused physical address space
 * for use in configuring devices.  Note that upaalloc
 * does not map the physical address into virtual memory.
 * Call vmap to do that.
 */
uintptr
upaalloc(int size, int align)
{
	uintptr a;

	a = mapalloc(&rmapupa, 0, size, align);
	if(a == 0){
		print("out of physical address space allocating %d\n", size);
		mapprint(&rmapupa);
	}
	return a;
}

void
upafree(uintptr pa, int size)
{
	mapfree(&rmapupa, pa, size);
}

void
upareserve(uintptr pa, int size)
{
	uintptr a;
	
	a = mapalloc(&rmapupa, pa, size, 0);
	if(a != pa){
		/*
		 * This can happen when we're using the E820
		 * map, which might have already reserved some
		 * of the regions claimed by the pci devices.
		 */
	//	print("upareserve: cannot reserve pa=%#p size=%d\n", pa, size);
		if(a != 0)
			mapfree(&rmapupa, a, size);
	}
}

void
memorysummary(void)
{
	memdebug();
}

