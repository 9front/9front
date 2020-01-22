#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static Segment *
seglook(u32int a, int n, u32int **lp)
{
	Segment *s;
	
	for(s = segs; s < segs + nelem(segs); s++)
		if(a >= s->start && a < s->start + s->size)
			break;
	if(s == segs + nelem(segs)) return nil;
	if(a + n > s->start + s->size) return nil;
	if(lp != nil) *lp = s->data + (a - s->start >> 2);
	return s;
}

u8int
memread8(u32int a)
{
	u32int *p;
	
	if(seglook(a, 1, &p) == nil) sysfatal("invalid read from %.8ux (pc=%.8ux)", a, curpc);
	switch(a & 3){
	case 0: return *p;
	case 1: return *p >> 8;
	case 2: return *p >> 16;
	case 3: return *p >> 24;
	}
	return 0;
}

u16int
memread16(u32int a)
{
	u32int *p;
	
	if(seglook(a, 2, &p) == nil) sysfatal("invalid read from %.8ux (pc=%.8ux)", a, curpc);
	switch(a & 3){
	case 0: return *p;
	case 1: return *p >> 8;
	case 2: return *p >> 16;
	case 3: return *p >> 24 | p[1] << 8;
	}
	return 0;
}

u32int
memread32(u32int a)
{
	u32int *p;
	
	if(seglook(a, 4, &p) == nil) sysfatal("invalid read from %.8ux (pc=%.8ux)", a, curpc);
	switch(a & 3){
	case 0: return *p;
	case 1: return *p >> 8 | p[1] << 24;
	case 2: return *p >> 16 | p[1] << 16;
	case 3: return *p >> 24 | p[1] << 8;
	}
	return *p;
}

void
memwrite(u32int a, u32int v, u32int m)
{
	u32int *p;
	Segment *s;
	
	s = seglook(a, 4, &p);
	if(s == nil || (s->flags & SEGRO) != 0) sysfatal("invalid write to %.8ux=%.8ux (mask=%.8ux, pc=%.8ux)", a, v, m, curpc);
	*p = *p & ~m | v & m;
}
