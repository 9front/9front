/*
 *  ramdisk driver
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "../port/sd.h"

typedef struct Ctlr Ctlr;
struct Ctlr {
	SDev	*dev;
	Segment	*seg;
	Segio	sio;

	ulong	nb;
	ulong	ss;
	ulong	off;

	char	buf[16];

	Physseg;
};

static Ctlr ctlrs[4];

extern SDifc sdramifc;

static uvlong
ramdiskalloc(uvlong base, ulong pages)
{
	uvlong limit, mbase, mlimit;
	int i, j;

	if(pages == 0)
		return 0;

	if(base == 0){
		/* allocate pages from the end of memoy banks */
		for(i=nelem(conf.mem)-1; i>=0; i--)
			if(conf.mem[i].npage >= pages){
				conf.mem[i].npage -= pages;
				return (uvlong)conf.mem[i].base + (uvlong)conf.mem[i].npage*BY2PG;
			}
		return 0;
	}

	/* exclude pages from memory banks */
	limit = base + (uvlong)pages*BY2PG;
	for(i=0; i<nelem(conf.mem); i++){
		mbase = conf.mem[i].base;
		mlimit = mbase + (uvlong)conf.mem[i].npage*BY2PG;
		if(base >= mlimit || limit <= mbase)
			continue;
		if(base >= mbase)
			conf.mem[i].npage = (base - mbase) / BY2PG;
		if(limit < mlimit){
			for(j=0; j<nelem(conf.mem); j++){
				if(conf.mem[j].npage == 0){
					conf.mem[j].base = limit;
					conf.mem[j].npage = (mlimit - limit) / BY2PG;
					break;
				}
			}
		}
	}

	return base;
}

static void
ramdiskinit0(Ctlr *ctlr, uvlong base, uvlong size, ulong ss)
{
	ulong nb, off;

	if(ss == 0)
		ss = 512;

	nb = size / ss;
	off = base & (BY2PG-1);
	size = (uvlong)nb*ss;
	size += off;
	size += BY2PG-1;
	size &= ~(BY2PG-1);
	base &= ~(BY2PG-1);

	if(size == 0 || size != (uintptr)size){
		print("%s: invalid parameters\n", ctlr->name);
		return;
	}

	base = ramdiskalloc(base, size/BY2PG);
	if(base == 0){
		print("%s: allocation failed\n", ctlr->name);
		return;
	}
	ctlr->nb = nb;
	ctlr->ss = ss;
	ctlr->off = off;
	ctlr->pa = base;
	ctlr->size = size;
	print("%s: %llux+%lud %llud %lud (%lud sectors)\n",
		ctlr->name, (uvlong)ctlr->pa, ctlr->off, (uvlong)ctlr->size, ctlr->ss, ctlr->nb);
}

static vlong
getsizenum(char **p)
{
	vlong v = strtoll(*p, p, 0);
	switch(**p){
	case 'T': case 't': v <<= 10;
	case 'G': case 'g': v <<= 10;
	case 'M': case 'm': v <<= 10;
	case 'K': case 'k': v <<= 10;
		(*p)++;
	}
	return v;
}

void
ramdiskinit(void)
{
	Ctlr *ctlr;
	uvlong a[3];
	char *p;
	int ctlrno, n;

	for(ctlrno=0; ctlrno<nelem(ctlrs); ctlrno++){
		ctlr = &ctlrs[ctlrno];
		if(ctlr->nb != 0)
			continue;

		snprint(ctlr->name = ctlr->buf, sizeof(ctlr->buf), "ramdisk%d", ctlrno);
		if((p = getconf(ctlr->name)) == nil)
			continue;

		for(n = 0; n < nelem(a); n++){
			while(*p == ' ' || *p == '\t')
				p++;
			if(*p == 0)
				break;
			a[n] = getsizenum(&p);
			switch(*p){
			case '-': case '+':
				a[n] += getsizenum(&p);
				break;
			}
		}
		switch(n){
		case 1:	/* ramdiskX=size */
			ramdiskinit0(ctlr, 0, a[0], 0);
			break;
		case 2:	/* ramdiskX=size ss */
			ramdiskinit0(ctlr, 0, a[0], (ulong)a[1]);
			break;
		case 3:	/* ramdiskX=base size ss */
			ramdiskinit0(ctlr, a[0], a[1], (ulong)a[2]);
			break;
		}
	}
}

static SDev*
rampnp(void)
{
	SDev *sdev;
	Ctlr *ctlr;

	for(ctlr = ctlrs; ctlr < &ctlrs[nelem(ctlrs)]; ctlr++){
		if(ctlr->nb == 0 || ctlr->dev != nil)
			continue;
		sdev = malloc(sizeof(SDev));
		if(sdev == nil)
			break;
		sdev->idno = 'Z';
		sdev->ifc = &sdramifc;
		sdev->nunit = 1;
		sdev->ctlr = ctlr;
		ctlr->dev = sdev;
		return sdev;
	}
	return nil;
}

static int
ramenable(SDev* dev)
{
	Ctlr *ctlr = dev->ctlr;

	ctlr->attr = SG_CACHED;
	ctlr->seg = newseg(SG_PHYSICAL, UTZERO, ctlr->size/BY2PG);
	if(ctlr->seg == nil)
		return 0;
	ctlr->seg->pseg = ctlr;
	return 1;
}

static int
ramverify(SDunit*)
{
	return 1;
}

static int
ramonline(SDunit *unit)
{
	Ctlr *ctlr = unit->dev->ctlr;
	unit->sectors = ctlr->nb;
	unit->secsize = ctlr->ss;
	return 1;
}

static int
ramrctl(SDunit *unit, char *p, int l)
{
	return snprint(p, l, "geometry %llud %ld\n",
		unit->sectors, unit->secsize);
}

static long
rambio(SDunit *unit, int, int write, void *data, long nb, uvlong bno)
{
	Ctlr *ctlr = unit->dev->ctlr;
	long secsize = unit->secsize;
	return segio(&ctlr->sio, ctlr->seg, data, nb*secsize, bno*secsize + ctlr->off, !write);
}

static int
ramrio(SDreq *r)
{
	int i, rw, count;
	uvlong lba;

	if((i = sdfakescsi(r)) != SDnostatus)
		return r->status = i;
	if((i = sdfakescsirw(r, &lba, &count, &rw)) != SDnostatus)
		return i;
	r->rlen = rambio(r->unit, r->lun, rw == SDwrite, r->data, count, lba);
	return r->status = SDok;
}

SDifc sdramifc = {
	.name	= "ram",
	.pnp	= rampnp,
	.enable	= ramenable,
	.verify	= ramverify,
	.online	= ramonline,
	.rctl	= ramrctl,
	.bio	= rambio,
	.rio	= ramrio,
};
