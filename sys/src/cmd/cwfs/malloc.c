#include "all.h"
#include "io.h"

static ulong
memsize(void)
{
	ulong pgsize, pgmax, userpgs, userused;
	char *s, *f[2];
	int n, mpcnt;
	Biobuf *bp;

	mpcnt = 25;
	pgsize = userpgs = userused = 0;
	if(bp = Bopen("#c/swap", OREAD)) {
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
		pgmax = (1024*1024*1024)/pgsize;	/* 1GB max */
		if(userpgs > pgmax)
			userpgs = pgmax;
		return userpgs*pgsize;
	}
	return 16*MB;
}


long	niob;
long	nhiob;
Hiob	*hiob;

/*
 * Called to allocate permanent data structures
 * Alignment is in number of bytes. It pertains both to the start and
 * end of the allocated memory.
 */
void*
ialloc(ulong n, int align)
{
	void *p = mallocalign(n, align, 0, 0);

	if (p == nil)
		panic("ialloc: out of memory");
	setmalloctag(p, getcallerpc(&n));
	memset(p, 0, n);
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
		print("\t%ld buffers; %ld hashes\n", niob, nhiob);
	hiob = ialloc(nhiob * sizeof(Hiob), 0);
	hp = hiob;
	for(i=0; i<nhiob; i++) {
		lock(hp);
		unlock(hp);
		hp++;
	}
	p = ialloc(niob * sizeof(Iobuf), 0);
	xiop = ialloc(niob * RBUFSIZE, 0);
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
