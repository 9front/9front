#include "all.h"
#include "io.h"

static ulong
memsize(void)
{
	int nf, pgsize = 0;
	ulong userpgs = 0, userused = 0;
	char *ln, *sl;
	char *fields[2];
	Biobuf *bp;

	bp = Bopen("#c/swap", OREAD);
	if (bp != nil) {
		while ((ln = Brdline(bp, '\n')) != nil) {
			ln[Blinelen(bp)-1] = '\0';
			nf = tokenize(ln, fields, nelem(fields));
			if (nf != 2)
				continue;
			if (strcmp(fields[1], "pagesize") == 0)
				pgsize = atoi(fields[0]);
			else if (strcmp(fields[1], "user") == 0) {
				sl = strchr(fields[0], '/');
				if (sl == nil)
					continue;
				userpgs = atol(sl+1);
				userused = atol(fields[0]);
			}
		}
		Bterm(bp);
		if (pgsize > 0 && userused < userpgs)
			return (userpgs - userused)*pgsize;
	}
	return 64*MB;
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
	long m;
	int i;
	char *xiop;
	Iobuf *p, *q;
	Hiob *hp;

	wlock(&mainlock);	/* init */
	wunlock(&mainlock);

	m = memsize() / 4;
	niob = m / (sizeof(Iobuf) + RBUFSIZE + sizeof(Hiob)/HWIDTH);
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
