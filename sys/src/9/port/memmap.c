#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

enum {
	Allocated	= 1UL<<31,
};

typedef struct Mapent Mapent;
struct Mapent
{
	ulong	type;
	uvlong	addr;
	uvlong	size;
};

static struct {
	Lock;
	int	n;
	int	m;
	Mapent	a[256];
} mapalloc;

static void
dump1(Mapent *e)
{
	print("%.16llux-%.16llux %lux\n", e->addr, e->addr + e->size, e->type);
}

static int
insert(uvlong addr, uvlong size, ulong type)
{
	Mapent *e;

	if(size == 0 || addr == -1 || addr + size-1 < addr)
		return 0;

	if(mapalloc.n+mapalloc.m >= nelem(mapalloc.a))
		return 0;

	e = &mapalloc.a[mapalloc.n + mapalloc.m++];
	e->type = type;
	e->addr = addr;
	e->size = size;

	return 1;
}

static Mapent*
lookup(uvlong addr)
{
	Mapent *i, *e;

	if(addr == -1)
		return nil;
	for(i = mapalloc.a, e = i + mapalloc.n; i < e; i++){
		if(i->addr > addr)
			break;
		if(addr - i->addr < i->size)
			return i;
	}
	return nil;
}

static int
compare(void *a, void *b)
{
	Mapent *ma = a, *mb = b;

	if(ma->addr < mb->addr)
		return -1;
	if(ma->addr > mb->addr)
		return 1;

	if(ma->type < mb->type)
		return -1;
	if(ma->type > mb->type)
		return 1;

	return 0;
}

static void
sort(void)
{
	Mapent *d, *i, *j, *e;

Again:
	if(mapalloc.m == 0)
		return;
	mapalloc.n += mapalloc.m;
	mapalloc.m = 0;

	qsort(mapalloc.a, mapalloc.n, sizeof(*e), compare);

	d = i = mapalloc.a;
	e = i + mapalloc.n;
	while(i < e){
		if(i->size == 0)
			goto Skip;
		for(j = i+1; j < e; j++){
			if(j->size == 0)
				continue;
			if(j->addr - i->addr >= i->size)
				break;
			if(j->type <= i->type){
				if(j->addr - i->addr + j->size <= i->size)
					j->size = 0;
				else {
					j->size -= i->addr + i->size - j->addr;
					j->addr = i->addr + i->size;
				}
				continue;
			}
			if(j->addr - i->addr + j->size < i->size)
				if(!insert(j->addr + j->size, i->size - (j->addr + j->size - i->addr), i->type))
					continue;
			i->size = j->addr - i->addr;
			if(i->size == 0)
				goto Skip;
		}
		if(d > mapalloc.a){
			j = d-1;
			if(i->addr - j->addr == j->size && i->type == j->type){
				j->size += i->size;
				i->size = 0;
				goto Skip;
			}
		}
		memmove(d, i, sizeof(*i));
		d++;
	Skip:
		i++;
	}
	if(mapalloc.m > 0)
		memmove(d, e, mapalloc.m*sizeof(*e));
	mapalloc.n = d - mapalloc.a;
	goto Again;
}

void
memmapdump(void)
{
	int i;

	lock(&mapalloc);
	sort();
	for(i = 0; i < mapalloc.n; i++)
		dump1(&mapalloc.a[i]);
	unlock(&mapalloc);
}

uvlong
memmapnext(uvlong addr, ulong type)
{
	Mapent *i, *e;

	lock(&mapalloc);
	sort();
	for(i = mapalloc.a, e = i+mapalloc.n; i < e; i++){
		if(((i->type ^ type) & ~Allocated) == 0
		&& (addr == -1 || i->addr > addr)){
			addr = i->addr;
			unlock(&mapalloc);
			return addr;
		}
	}
	unlock(&mapalloc);
	return -1;
}

uvlong
memmapsize(uvlong addr, uvlong align)
{
	Mapent *i;
	uvlong size;

	size = 0;
	lock(&mapalloc);
	sort();
	if((i = lookup(addr)) != nil){
		if(align){
			addr += align-1;
			addr &= ~(align-1);
		}
		if(addr - i->addr < i->size)
			size = i->size - (addr - i->addr);
	}
	unlock(&mapalloc);
	return size;
}

void
memmapadd(uvlong addr, uvlong size, ulong type)
{
	type &= ~Allocated;
	lock(&mapalloc);
	if(insert(addr, size, type))
		if(mapalloc.n+mapalloc.m >= nelem(mapalloc.a)-1)
			sort();
	unlock(&mapalloc);
}

uvlong
memmapalloc(uvlong addr, uvlong size, uvlong align, ulong type)
{
	Mapent *i, *e;

	type &= ~Allocated;
	lock(&mapalloc);
	sort();
	if(addr != -1){
		i = lookup(addr);
		if(i == nil || i->type != type)
			goto Fail;
		if(align){
			addr += align-1;
			addr &= ~(align-1);
			if(addr - i->addr >= i->size)
				goto Fail;
		}
		if(addr - i->addr + size > i->size)
			goto Fail;
Alloc:
		if(size > 0 && !insert(addr, size, type|Allocated))
			goto Fail;
		unlock(&mapalloc);
		return addr;
	}
	e = mapalloc.a + mapalloc.n;
	for(i = mapalloc.a; i < e; i++){
		if(i->type != type)
			continue;
		addr = i->addr;
		if(align){
			addr += align-1;
			addr &= ~(align-1);
			if(addr - i->addr >= i->size)
				continue;
		}
		if(addr - i->addr + size <= i->size)
			goto Alloc;
	}
Fail:
	unlock(&mapalloc);
	return -1;
}

void
memmapfree(uvlong addr, uvlong size, ulong type)
{
	Mapent *i;

	lock(&mapalloc);
	sort();
	i = lookup(addr);
	if(i == nil
	|| i->type != (type|Allocated)
	|| addr - i->addr + size > i->size){
		unlock(&mapalloc);
		return;
	}
	if(i->addr < addr)
		insert(i->addr, addr - i->addr, i->type);
	if(addr - i->addr + size < i->size)
		insert(addr+size, addr - i->addr + i->size - size, i->type);
	i->type &= ~Allocated;
	unlock(&mapalloc);
}
