#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Segment *
newseg(u32int start, u32int size, int idx)
{
	Segment *s;
	
	s = emallocz(sizeof *s);
	incref(s);
	s->start = start;
	s->size = size;
	s->ref = emalloc(size + sizeof(Ref));
	memset(s->ref, 0, sizeof(Ref));
	incref(s->ref);
	s->data = s->ref + 1;
	if(idx == SEGBSS)
		s->flags = SEGFLLOCK;
	P->S[idx] = s;
	return s;
}

void
freesegs(void)
{
	Segment **s;
	
	for(s = P->S; s < P->S + SEGNUM; s++) {
		if(*s == nil)
			continue;
		if(decref((*s)->ref) == 0)
			free((*s)->ref);
		if(decref(*s) == 0)
			free(*s);
		*s = nil;
	}
}

void *
vaddr(u32int addr, Segment **seg)
{
	Segment **ss, *s;

	for(ss = P->S; ss < P->S + SEGNUM; ss++) {
		if(*ss == nil)
			continue;
		s = *ss;
		if(addr >= s->start && addr < s->start + s->size) {
			if(s->flags & SEGFLLOCK)
				rlock(&s->rw);
			*seg = s;
			return (char *)s->data + (addr - s->start);
		}
	}
	sysfatal("fault %.8ux @ %.8ux", addr, P->R[15]);
	return nil;
}

void *
vaddrnol(u32int addr)
{
	Segment *seg;
	void *ret;
	
	ret = vaddr(addr, &seg);
	segunlock(seg);
	return ret;
}

/* might be made a macro for hurr durr performance */
void
segunlock(Segment *s)
{
	if(s->flags & SEGFLLOCK)
		runlock(&s->rw);
}

void *
copyifnec(u32int addr, int len, int *copied)
{
	void *targ, *ret;
	Segment *seg;
	
	targ = vaddr(addr, &seg);
	if((seg->flags & SEGFLLOCK) == 0) {
		*copied = 0;
		return targ;
	}
	if(len < 0)
		len = strlen(targ) + 1;
	ret = emalloc(len);
	memcpy(ret, targ, len);
	segunlock(seg);
	*copied = 1;
	return ret;
}

void *
bufifnec(u32int addr, int len, int *buffered)
{
	void *targ;
	Segment *seg;
	
	targ = vaddr(addr, &seg);
	if((seg->flags & SEGFLLOCK) == 0) {
		*buffered = 0;
		return targ;
	}
	segunlock(seg);
	*buffered = 1;
	return emalloc(len);
}

void
copyback(u32int addr, int len, void *data)
{
	void *targ;
	Segment *seg;

	if(len <= 0)
		return;
	targ = vaddr(addr, &seg);
	memmove(targ, data, len);
	segunlock(seg);
	free(data);
}
