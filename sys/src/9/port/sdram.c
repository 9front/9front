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

typedef struct Ramdisk Ramdisk;
struct Ramdisk {
	Segment	*seg;
	Segio	sio;

	ulong	nb;
	ulong	ss;
	ulong	off;

	char	buf[16];

	Physseg;
};

static Ramdisk rds[4];

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
ramdiskinit0(Ramdisk *rd, uvlong base, uvlong size, ulong ss)
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
		print("%s: invalid parameters\n", rd->name);
		return;
	}

	base = ramdiskalloc(base, size/BY2PG);
	if(base == 0){
		print("%s: allocation failed\n", rd->name);
		return;
	}
	rd->nb = nb;
	rd->ss = ss;
	rd->off = off;

	rd->pa = base;
	rd->size = size;
	rd->attr = SG_PHYSICAL | SG_CACHED | SG_NOEXEC;
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
	Ramdisk *rd;
	uvlong a[3];
	char *p;
	int subno, n;

	for(subno=0; subno<nelem(rds); subno++){
		rd = &rds[subno];
		if(rd->nb != 0)
			continue;

		snprint(rd->name = rd->buf, sizeof(rd->buf), "ramdisk%d", subno);
		if((p = getconf(rd->name)) == nil)
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
			ramdiskinit0(rd, 0, a[0], 0);
			break;
		case 2:	/* ramdiskX=size ss */
			ramdiskinit0(rd, 0, a[0], (ulong)a[1]);
			break;
		case 3:	/* ramdiskX=base size ss */
			ramdiskinit0(rd, a[0], a[1], (ulong)a[2]);
			break;
		}
	}
}

static SDev*
rampnp(void)
{
	SDev *sdev;

	sdev = malloc(sizeof(SDev));
	if(sdev == nil)
		return nil;

	sdev->idno = 'Z';
	sdev->ifc = &sdramifc;
	sdev->nunit = nelem(rds);

	return sdev;
}

static int
ramverify(SDunit *unit)
{
	Ramdisk *rd = &rds[unit->subno];

	if(rd->nb == 0)
		return 0;

	unit->inquiry[0] = 0;
	unit->inquiry[1] = 0;
	unit->inquiry[4] = sizeof unit->inquiry - 4;
	strcpy((char*)unit->inquiry+8, rd->name);

	return 1;
}

static int
ramonline(SDunit *unit)
{
	Ramdisk *rd = &rds[unit->subno];

	if(unit->sectors != 0)
		return 1;

	rd->seg = newseg(rd->attr, UTZERO, rd->size/BY2PG);
	if(rd->seg == nil)
		return 0;
	rd->seg->pseg = rd;
	unit->sectors = rd->nb;
	unit->secsize = rd->ss;

	return 2;
}

static char*
ramrctl(SDunit *unit, char *p, char *e)
{
	Ramdisk *rd = &rds[unit->subno];

	return seprint(p, e, "geometry %llud %ld\nalignment %lud %lud\n",
		unit->sectors, unit->secsize,
		(ulong)BY2PG, rd->off / unit->secsize);
}

static long
rambio(SDunit *unit, int, int write, void *data, long nb, uvlong bno)
{
	Ramdisk *rd = &rds[unit->subno];
	long secsize = unit->secsize;

	return segio(&rd->sio, rd->seg, data, nb*secsize, bno*secsize + rd->off, !write);
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
	.verify	= ramverify,
	.online	= ramonline,
	.rctl	= ramrctl,
	.bio	= rambio,
	.rio	= ramrio,
};
