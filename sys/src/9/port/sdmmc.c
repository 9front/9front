/*
 * mmc / sd memory card
 *
 * Copyright © 2012 Richard Miller <r.miller@acm.org>
 *
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "../port/sd.h"

#define CSD(end, start)	rbits(csd, start, (end)-(start)+1)

typedef struct Ctlr Ctlr;

enum {
	Inittimeout	= 15,
	Multiblock	= 1,

	/* Commands */
	GO_IDLE_STATE	= 0,
	ALL_SEND_CID	= 2,
	SEND_RELATIVE_ADDR= 3,
	SWITCH_FUNC	= 6,
	SELECT_CARD	= 7,
	SD_SEND_IF_COND	= 8,
	SEND_CSD	= 9,
	STOP_TRANSMISSION= 12,
	SEND_STATUS	= 13,
	SET_BLOCKLEN	= 16,
	READ_SINGLE_BLOCK= 17,
	READ_MULTIPLE_BLOCK= 18,
	WRITE_BLOCK	= 24,
	WRITE_MULTIPLE_BLOCK= 25,
	APP_CMD		= 55,	/* prefix for following app-specific commands */
	SET_BUS_WIDTH	= 6,
	SD_SEND_OP_COND	= 41,

	/* Command arguments */
	/* SD_SEND_IF_COND */
	Voltage		= 1<<8,
	Checkpattern	= 0x42,

	/* SELECT_CARD */
	Rcashift	= 16,

	/* SD_SEND_OP_COND */
	Hcs	= 1<<30,	/* host supports SDHC & SDXC */
	Ccs	= 1<<30,	/* card is SDHC or SDXC */
	V3_3	= 3<<20,	/* 3.2-3.4 volts */

	/* SET_BUS_WIDTH */
	Width1	= 0<<0,
	Width4	= 2<<0,

	/* SWITCH_FUNC */
	Dfltspeed	= 0<<0,
	Hispeed		= 1<<0,
	Checkfunc	= 0x00FFFFF0,
	Setfunc		= 0x80FFFFF0,
	Funcbytes	= 64,

	/* OCR (operating conditions register) */
	Powerup	= 1<<31,
};

struct Ctlr {
	SDev	*dev;
	SDio	*io;

	/* SD card registers */
	u16int	rca;
	u32int	ocr;
	u32int	cid[4];
	u32int	csd[4];

	int	retry;
};

extern SDifc sdmmcifc;

static SDio *sdio[8];
static int nsdio, isdio;

void
addmmcio(SDio *io)
{
	assert(io != nil);
	assert(isdio == 0);
	if(nsdio >= nelem(sdio)){
		print("addmmcio: out of slots for %s\n", io->name);
		return;
	}
	sdio[nsdio++] = io;
}

static SDev*
init1(void)
{
	SDev *sdev;
	Ctlr *ctlr;
	SDio *io;
	int more;

	if(isdio >= nsdio)
		return nil;
	if((sdev = malloc(sizeof(SDev))) == nil)
		return nil;
	if((ctlr = malloc(sizeof(Ctlr))) == nil){
		free(sdev);
		return nil;
	}
	if((io = malloc(sizeof(SDio))) == nil){
		free(ctlr);
		free(sdev);
		return nil;
	}
Next:
	memmove(io, sdio[isdio++], sizeof(SDio));
	if(io->init != nil){
		more = (*io->init)(io);
		if(more < 0){
			if(isdio < nsdio)
				goto Next;

			free(io);
			free(ctlr);
			free(sdev);
			return nil;
		}
		if(more > 0)
			isdio--;	/* try again */
	}
	sdev->idno = 'M';
	sdev->ifc = &sdmmcifc;
	sdev->nunit = 1;
	sdev->ctlr = ctlr;
	ctlr->dev = sdev;
	ctlr->io = io;
	return sdev;
}

static SDev*
mmcpnp(void)
{
	SDev *list = nil, **link = &list;

	while((*link = init1()) != nil)
		link = &(*link)->next;

	return list;
}

static uint
rbits(u32int *p, uint start, uint len)
{
	uint w, off, v;

	w   = start / 32;
	off = start % 32;
	if(off == 0)
		v = p[w];
	else
		v = p[w] >> off | p[w+1] << (32-off);
	if(len < 32)
		return v & ((1<<len) - 1);
	else
		return v;
}

static void
identify(SDunit *unit, u32int *csd)
{
	uint csize, mult;

	unit->secsize = 1 << CSD(83, 80);
	switch(CSD(127, 126)){
	case 0:				/* CSD version 1 */
		csize = CSD(73, 62);
		mult = CSD(49, 47);
		unit->sectors = (csize+1) * (1<<(mult+2));
		break;
	case 1:				/* CSD version 2 */
		csize = CSD(69, 48);
		unit->sectors = (csize+1) * 0x80000LL / unit->secsize;
		break;
	}
	if(unit->secsize == 1024){
		unit->sectors <<= 1;
		unit->secsize = 512;
	}
}

static int
mmcverify(SDunit *unit)
{
	Ctlr *ctlr = unit->dev->ctlr;
	SDio *io = ctlr->io;
	int n;

	n = (*io->inquiry)(io, (char*)&unit->inquiry[8], sizeof(unit->inquiry)-8);
	if(n < 0)
		return 0;
	unit->inquiry[0] = 0x00;	/* direct access (disk) */
	unit->inquiry[1] = 0x80;	/* removable */
	unit->inquiry[4] = sizeof(unit->inquiry)-4;
	return 1;
}

static int
mmcenable(SDev* dev)
{
	Ctlr *ctlr = dev->ctlr;
	SDio *io = ctlr->io;
	(*io->enable)(io);
	return 1;
}

static void
mmcswitchfunc(SDio *io, int arg)
{
	u32int r[4];
	uchar *buf;
	int n;

	n = Funcbytes;
	buf = sdmalloc(n);
	if(waserror()){
		print("%s: mmcswitchfunc error\n", io->name);
		sdfree(buf);
		nexterror();
	}
	(*io->iosetup)(io, 0, buf, n, 1);
	(*io->cmd)(io, SWITCH_FUNC, arg, r);
	(*io->io)(io, 0, buf, n);
	sdfree(buf);
	poperror();
}

static int
cardinit(Ctlr *ctlr)
{
	SDio *io = ctlr->io;
	u32int r[4];
	int hcs, i;

	(*io->cmd)(io, GO_IDLE_STATE, 0, r);
	hcs = 0;
	if(!waserror()){
		(*io->cmd)(io, SD_SEND_IF_COND, Voltage|Checkpattern, r);
		if(r[0] == (Voltage|Checkpattern))	/* SD 2.0 or above */
			hcs = Hcs;
		poperror();
	}
	for(i = 0; i < Inittimeout; i++){
		tsleep(&up->sleep, return0, nil, 100);
		(*io->cmd)(io, APP_CMD, 0, r);
		(*io->cmd)(io, SD_SEND_OP_COND, hcs|V3_3, r);
		if(r[0] & Powerup)
			break;
	}
	if(i == Inittimeout)
		return 2;
	ctlr->ocr = r[0];
	(*io->cmd)(io, ALL_SEND_CID, 0, r);
	memmove(ctlr->cid, r, sizeof ctlr->cid);
	(*io->cmd)(io, SEND_RELATIVE_ADDR, 0, r);
	ctlr->rca = r[0]>>16;
	(*io->cmd)(io, SEND_CSD, ctlr->rca<<Rcashift, r);
	memmove(ctlr->csd, r, sizeof ctlr->csd);
	return 1;
}

static void
retryproc(void *arg)
{
	Ctlr *ctlr = arg;
	int i = 0;

	while(waserror())
		;
	if(i++ < ctlr->retry)
		cardinit(ctlr);
	USED(i);
	ctlr->retry = 0;
	pexit("", 1);
}

static int
mmconline(SDunit *unit)
{
	Ctlr *ctlr = unit->dev->ctlr;
	SDio *io = ctlr->io;
	u32int r[4];

	assert(unit->subno == 0);
	if(ctlr->retry)
		return 0;
	if(waserror()){
		unit->sectors = 0;
		if(ctlr->retry++ == 0)
			kproc(unit->name, retryproc, ctlr);
		return 0;
	}
	if(unit->sectors != 0){
		(*io->cmd)(io, SEND_STATUS, ctlr->rca<<Rcashift, r);
		poperror();
		return 1;
	}
	if(cardinit(ctlr) != 1){
		poperror();
		return 2;
	}
	identify(unit, ctlr->csd);
	(*io->cmd)(io, SELECT_CARD, ctlr->rca<<Rcashift, r);
	(*io->cmd)(io, SET_BLOCKLEN, unit->secsize, r);
	(*io->cmd)(io, APP_CMD, ctlr->rca<<Rcashift, r);
	(*io->cmd)(io, SET_BUS_WIDTH, Width4, r);
	if(io->highspeed){
		if(!waserror()){
			mmcswitchfunc(io, Hispeed|Setfunc);
			poperror();
		}
	}
	poperror();
	return 1;
}

static int
mmcrctl(SDunit *unit, char *p, int l)
{
	Ctlr *ctlr = unit->dev->ctlr;
	int i, n;

	assert(unit->subno == 0);
	if(unit->sectors == 0){
		mmconline(unit);
		if(unit->sectors == 0)
			return 0;
	}
	n = snprint(p, l, "rca %4.4ux ocr %8.8ux\ncid ", ctlr->rca, ctlr->ocr);
	for(i = nelem(ctlr->cid)-1; i >= 0; i--)
		n += snprint(p+n, l-n, "%8.8ux", ctlr->cid[i]);
	n += snprint(p+n, l-n, " csd ");
	for(i = nelem(ctlr->csd)-1; i >= 0; i--)
		n += snprint(p+n, l-n, "%8.8ux", ctlr->csd[i]);
	n += snprint(p+n, l-n, "\ngeometry %llud %ld\n",
		unit->sectors, unit->secsize);
	return n;
}

static long
mmcbio(SDunit *unit, int lun, int write, void *data, long nb, uvlong bno)
{
	Ctlr *ctlr = unit->dev->ctlr;
	SDio *io = ctlr->io;
	int len, tries;
	u32int r[4];
	uchar *buf;
	ulong b;

	USED(lun);
	assert(unit->subno == 0);
	if(unit->sectors == 0)
		error(Echange);
	buf = data;
	len = unit->secsize;
	if(Multiblock && (!write || !io->nomultiwrite)){
		b = bno;
		tries = 0;
		while(waserror())
			if(++tries == 3)
				nexterror();
		(*io->iosetup)(io, write, buf, len, nb);
		if(waserror()){
			(*io->cmd)(io, STOP_TRANSMISSION, 0, r);
			nexterror();
		}
		(*io->cmd)(io, write? WRITE_MULTIPLE_BLOCK: READ_MULTIPLE_BLOCK,
			ctlr->ocr & Ccs? b: b * len, r);
		(*io->io)(io, write, buf, nb * len);
		poperror();
		(*io->cmd)(io, STOP_TRANSMISSION, 0, r);
		poperror();
		b += nb;
	}else{
		for(b = bno; b < bno + nb; b++){
			(*io->iosetup)(io, write, buf, len, 1);
			(*io->cmd)(io, write? WRITE_BLOCK : READ_SINGLE_BLOCK,
				ctlr->ocr & Ccs? b: b * len, r);
			(*io->io)(io, write, buf, len);
			buf += len;
		}
	}
	return (b - bno) * len;
}

static int
mmcrio(SDreq *r)
{
	int i, rw, count;
	uvlong lba;

	if((i = sdfakescsi(r)) != SDnostatus)
		return r->status = i;
	if((i = sdfakescsirw(r, &lba, &count, &rw)) != SDnostatus)
		return i;
	r->rlen = mmcbio(r->unit, r->lun, rw == SDwrite, r->data, count, lba);
	return r->status = SDok;
}

SDifc sdmmcifc = {
	.name	= "mmc",
	.pnp	= mmcpnp,
	.enable	= mmcenable,
	.verify	= mmcverify,
	.online	= mmconline,
	.rctl	= mmcrctl,
	.bio	= mmcbio,
	.rio	= mmcrio,
};
