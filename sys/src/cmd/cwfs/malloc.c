#include "all.h"
#include "io.h"

#include <pool.h>

static uvlong
memsize(void)
{
	ulong pgsize, userpgs, userused;
	char *s, *f[2];
	int n, mpcnt;
	Biobuf *bp;

	mpcnt = 25;
	pgsize = userpgs = userused = 0;
	if(bp = Bopen("/dev/swap", OREAD)) {
		while(s = Brdline(bp, '\n')) {
			if((n = Blinelen(bp)) < 1)
				continue;
			s[n-1] = '\0';
			if(tokenize(s, f, nelem(f)) != 2)
				continue;
			if(strcmp(f[1], "pagesize") == 0)
				pgsize = strtoul(f[0], 0, 0);
			else if(strcmp(f[1], "user") == 0) {
				userused =  strtoul(f[0], &s, 0);
				if(*s == '/')
					userpgs = strtoul(s+1, 0, 0);
			}
		}
		Bterm(bp);
	}
	if(pgsize && userused < userpgs){
		userpgs -= userused;
		if(s = getenv("fsmempercent")){
			mpcnt = atoi(s);
			free(s);
		}
		if(mpcnt < 1)
			mpcnt = 1;
		userpgs = (userpgs*mpcnt)/100;
		return (uvlong)userpgs*pgsize;
	}
	return 16*MB;
}

uint	niob;
uint	nhiob;
Hiob	*hiob;

/*
 * Called to allocate permanent data structures
 * Alignment is in number of bytes. It pertains both to the start and
 * end of the allocated memory.
 */
void*
ialloc(uintptr n, int align)
{
	char *p;
	int m;

	if(align <= 0)
		align = sizeof(uintptr);

	mainmem->lock(mainmem);

	p = sbrk(0);
	if(m = n % align)
		n += align - m;
	if(m = (uintptr)p % align)
		p += align - m;
	if(brk(p+n) < 0)
		panic("ialloc: out of memory");

	mainmem->unlock(mainmem);

	return p;
}

enum { HWIDTH = 8 };		/* buffers per hash */

/*
 * allocate rest of mem
 * for io buffers.
 */
void
iobufinit(void)
{
	int i;
	char *xiop;
	Iobuf *p, *q;
	Hiob *hp;

	wlock(&mainlock);	/* init */
	wunlock(&mainlock);

	niob = memsize() / (sizeof(Iobuf) + RBUFSIZE + sizeof(Hiob)/HWIDTH);
	nhiob = niob / HWIDTH;
	while(!prime(nhiob))
		nhiob++;
	if(chatty)
		print("\t%ud buffers; %ud hashes\n", niob, nhiob);
	hiob = ialloc((uintptr)nhiob * sizeof(Hiob), 0);
	hp = hiob;
	for(i=0; i<nhiob; i++) {
		lock(hp);
		unlock(hp);
		hp++;
	}
	p = ialloc((uintptr)niob * sizeof(Iobuf), 0);
	xiop = ialloc((uintptr)niob * RBUFSIZE, 0);
	hp = hiob;
	for(i=0; i < niob; i++) {
		qlock(p);
		qunlock(p);
		if(hp == hiob)
			hp = hiob + nhiob;
		hp--;
		q = hp->link;
		if(q) {
			p->fore = q;
			p->back = q->back;
			q->back = p;
			p->back->fore = p;
		} else {
			hp->link = p;
			p->fore = p;
			p->back = p;
		}
		p->dev = devnone;
		p->addr = -1;
		p->xiobuf = xiop;
		p->iobuf = (char*)-1;
		p++;
		xiop += RBUFSIZE;
	}
}

void*
iobufmap(Iobuf *p)
{
	return p->iobuf = p->xiobuf;
}

void
iobufunmap(Iobuf *p)
{
	p->iobuf = (char*)-1;
}
