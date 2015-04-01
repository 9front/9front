#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

extern Var cpuvars[], ppuvars[], memvars[], apuvars[], evvars[];
extern Event *events[NEVENT], *elist;
static Biobuf *bp;
Var apuvars[] = {{nil, 0, 0}};

static void
putevents(void)
{
	int i, j;
	Event *e;
	
	for(i = 0; i < NEVENT; i++)
		if(elist == events[i])
			break;
	if(i == NEVENT && elist != nil)
		print("unknown event %p in chain\n", elist);
	Bputc(bp, i);
	for(i = 0; i < NEVENT; i++){
		e = events[i];
		Bputc(bp, e->time);
		Bputc(bp, e->time >> 8);
		Bputc(bp, e->time >> 16);
		Bputc(bp, e->time >> 24);
		for(j = 0; j < NEVENT; j++)
			if(e->next == events[j])
				break;
		if(j == NEVENT && e->next != nil)
			print("unknown event %p in chain\n", e->next);
		Bputc(bp, j);
	}
		
}

static void
getevents(void)
{
	int i, j;
	Event *e;
	
	i = Bgetc(bp);
	elist = i >= NEVENT ? nil : events[i];
	for(i = 0; i < NEVENT; i++){
		e = events[i];
		e->time = Bgetc(bp);
		e->time |= Bgetc(bp) << 8;
		e->time |= Bgetc(bp) << 16;
		e->time |= Bgetc(bp) << 24;
		j = Bgetc(bp);
		e->next = j >= NEVENT ? nil : events[j];
	}
}

void
getvars(Var *v)
{
	int n;
	u16int *p, w;
	u32int *q, l;

	for(; v->a != nil; v++)
		switch(v->s){
		case 1:
			Bread(bp, v->a, v->n);
			break;
		case 2:
			n = v->n;
			p = v->a;
			while(n--){
				w = Bgetc(bp);
				*p++ = w | Bgetc(bp) << 8;
			}
			break;
		case 4:
			n = v->n;
			q = v->a;
			while(n--){
				l = Bgetc(bp);
				l |= Bgetc(bp) << 8;
				l |= Bgetc(bp) << 16;
				*q++ = l | Bgetc(bp) << 24;
			}
			break;
		}

}

void
putvars(Var *v)
{
	int n;
	u16int *p;
	u32int *q;

	for(; v->a != nil; v++)
		switch(v->s){
		case 1:
			Bwrite(bp, v->a, v->n);
			break;
		case 2:
			n = v->n;
			p = v->a;
			while(n--){
				Bputc(bp, *p & 0xff);
				Bputc(bp, *p++ >> 8);
			}
			break;
		case 4:
			n = v->n;
			q = v->a;
			while(n--){
				Bputc(bp, *q);
				Bputc(bp, *q >> 8);
				Bputc(bp, *q >> 16);
				Bputc(bp, *q++ >> 24);
			}
			break;
		}
}

void
savestate(char *file)
{
	flushback();
	bp = Bopen(file, OWRITE);
	if(bp == nil){
		print("open: %r\n");
		return;
	}
	putvars(cpuvars);
	putvars(ppuvars);
	putvars(memvars);
	putvars(apuvars);
	putvars(evvars);
	putevents();
	mapper(SAVE, 0);
	Bterm(bp);
}

void
loadstate(char *file)
{
	bp = Bopen(file, OREAD);
	if(bp == nil){
		print("open: %r\n");
		return;
	}
	getvars(cpuvars);
	getvars(ppuvars);
	getvars(memvars);
	getvars(apuvars);
	getvars(evvars);
	getevents();
	mapper(RSTR, 0);
	memload();
	Bterm(bp);
}
