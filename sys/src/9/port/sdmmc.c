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

/* Commands */
SDiocmd GO_IDLE_STATE		= { 0, 0, 0, 0, "GO_IDLE_STATE" };
SDiocmd SEND_OP_COND		= { 1, 3, 0, 0, "SEND_OP_COND" };
SDiocmd ALL_SEND_CID		= { 2, 2, 0, 0, "ALL_SEND_CID" };
SDiocmd SET_RELATIVE_ADDR	= { 3, 1, 0, 0, "SET_RELATIVE_ADDR" };
SDiocmd SEND_RELATIVE_ADDR	= { 3, 6, 0, 0, "SEND_RELATIVE_ADDR" };
SDiocmd SWITCH			= { 6, 1, 1, 0, "SWITCH" };
SDiocmd SWITCH_FUNC		= { 6, 1, 0, 1, "SWITCH_FUNC" };
SDiocmd SELECT_CARD		= { 7, 1, 1, 0, "SELECT_CARD" };
SDiocmd SEND_EXT_CSD		= { 8, 1, 0, 1, "SEND_EXT_CSD" };
SDiocmd SD_SEND_IF_COND		= { 8, 1, 0, 0, "SD_SEND_IF_COND" };
SDiocmd SEND_CSD		= { 9, 2, 0, 0, "SEND_CSD" };
SDiocmd STOP_TRANSMISSION	= {12, 1, 1, 0, "STOP_TRANSMISSION" };
SDiocmd SEND_STATUS		= {13, 1, 0, 0, "SEND_STATUS" };
SDiocmd SET_BLOCKLEN		= {16, 1, 0, 0, "SET_BLOCKLEN" };
SDiocmd READ_SINGLE_BLOCK	= {17, 1, 0, 1, "READ_SINGLE_BLOCK" };
SDiocmd READ_MULTIPLE_BLOCK	= {18, 1, 0, 3, "READ_MULTIPLE_BLOCK" };
SDiocmd WRITE_SINGLE_BLOCK	= {24, 1, 0, 2, "WRITE_SINGLE_BLOCK" };
SDiocmd WRITE_MULTIPLE_BLOCK	= {25, 1, 0, 4, "WRITE_MULTIPLE_BLOCK" };

/* prefix for following app-specific commands */
SDiocmd APP_CMD			= {55, 1, 0, 0, "APP_CMD" };
SDiocmd  SD_SET_BUS_WIDTH	= { 6, 1, 0, 0, "SD_SET_BUS_WIDTH" };
SDiocmd  SD_SEND_OP_COND	= {41, 3, 0, 0, "SD_SEND_OP_COND" };

/* Command arguments */
enum {
	/* SD_SEND_IF_COND */
	Voltage		= 1<<8,	/* Host supplied voltage range 2.7-3.6 volts*/
	Checkpattern	= 0x42,

	/* SELECT_CARD */
	Rcashift	= 16,

	/* SD_SET_BUS_WIDTH */
	Width1	= 0<<0,
	Width4	= 2<<0,

	/* SWITCH_FUNC */
	Dfltspeed	= 0<<0,
	Hispeed		= 1<<0,
	Checkfunc	= 0x00FFFFF0,
	Setfunc		= 0x80FFFFF0,
	Funcbytes	= 64,

	/* SWITCH */
	MMCSetSDTiming	= 0x3B90000,
	MMCSetHSTiming	= 0x3B90100,
	MMCSetBusWidth1	= 0x3B70000,
	MMCSetBusWidth4	= 0x3B70100,
	MMCSetBusWidth8	= 0x3B70200,

	/* OCR (operating conditions register) */
	Powerup	= 1<<31,
	Hcs	= 1<<30,	/* Host capacity support */
	Ccs	= 1<<30,	/* Card capacity status */
	V3_3	= 3<<20,	/* 3.2-3.4 volts */
};

enum {
	Multiblock	= 1,
	Inittimeout	= 15,

	Initfreq	= 400000,	/* initialisation frequency for MMC */
	SDfreq		= 25000000,	/* standard SD frequency */
	SDfreqhs	= 50000000,	/* highspeed frequency */
};

typedef struct Card Card;
struct Card {
	QLock;

	SDev	*dev;
	SDio	*io;

	int	ismmc;
	int	specver;

	int	buswidth;
	int	busspeed;

	/* SD card registers */
	u32int	rca;
	u32int	ocr;
	u32int	cid[4];
	u32int	csd[4];

	u8int	ext_csd[512];

	uvlong	sectors[3];
	uint	secsize;

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
	Card *card;
	SDio *io;
	int more;

	if(isdio >= nsdio)
		return nil;
	if((sdev = malloc(sizeof(SDev))) == nil)
		return nil;
	if((card = malloc(sizeof(Card))) == nil){
		free(sdev);
		return nil;
	}
	if((io = malloc(sizeof(SDio))) == nil){
		free(card);
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
			free(card);
			free(sdev);
			return nil;
		}
		if(more > 0)
			isdio--;	/* try again */
	}
	card->dev = sdev;
	card->io = io;
	sdev->idno = 'M';
	sdev->ifc = &sdmmcifc;
	sdev->nunit = nelem(card->sectors);
	sdev->ctlr = card;
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

static SDio*
freecard(Card *card)
{
	SDio *io = card->io;

	/* wait for retryproc() */
	do {
		qlock(card);
		qunlock(card);
	} while(card->retry);

	free(card);

	return io;
}

SDio*
annexsdio(char *spec)
{
	return freecard(sdannexctlr(spec, &sdmmcifc));
}

static void
mmcclear(SDev *sdev)
{
	free(freecard(sdev->ctlr));
}

static void
readextcsd(Card *card)
{
	SDio *io = card->io;
	u32int r[4];
	uchar *buf;

	buf = sdmalloc(512);
	if(waserror()){
		sdfree(buf);
		nexterror();
	}
	(*io->iosetup)(io, 0, buf, 512, 1);
	(*io->cmd)(io, &SEND_EXT_CSD, 0, r);
	(*io->io)(io, 0, buf, 512);
	memmove(card->ext_csd, buf, sizeof card->ext_csd);
	sdfree(buf);
	poperror();
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

static uvlong
rbytes(uchar *p, uint start, uint len)
{
	uvlong v = 0;
	uint i;

	p += start;
	for(i = 0; i < len; i++)
		v |= (uvlong)p[i] << 8*i;
	return v;
}

static void
identify(Card *card)
{
	uint csize, mult;
	uvlong capacity;

#define CSD(end, start)	rbits(card->csd, start, (end)-(start)+1)
	mult = CSD(49, 47);
	csize = CSD(73, 62);
	card->secsize = 1 << CSD(83, 80);
	card->sectors[0] = (csize+1) * (1<<(mult+2));
	card->sectors[1] = 0;
	card->sectors[2] = 0;

	card->specver = 0;

	if(card->ismmc){
		switch(CSD(125, 122)){
		case 0:
		default:
			card->specver = 120;	/* 1.2 */
			break;
		case 1:
			card->specver = 140;	/* 1.4 */
			break;
		case 2:
			card->specver = 122;	/* 1.22 */
			break;
		case 3:
			card->specver = 300;	/* 3.0 */
			break;
		case 4:
			card->specver = 400;	/* 4.0 */
			break;
		}
		switch(CSD(127, 126)){
		case 2:				/* MMC CSD Version 1.2 */
		case 3:				/* MMC Version coded in EXT_CSD */
			if(card->specver < 400)
				break;
			readextcsd(card);
#define EXT_CSD(end, start) rbytes(card->ext_csd, start, (end)-(start)+1)
			switch((uchar)EXT_CSD(192, 192)){
			case 8:
				card->specver = 510;	/* 5.1 */
				break;
			case 7:
				card->specver = 500;	/* 5.0 */
				break;
			case 6:
				card->specver = 450;	/* 4.5/4.51 */
				break;
			case 5:
				card->specver = 441;	/* 4.41 */
				break;
			case 3:
				card->specver = 430;	/* 4.3 */
				break;
			case 2:
				card->specver = 420;	/* 4.2 */
				break;
			case 1:
				card->specver = 410;	/* 4.1 */
				break;
			case 0:
				card->specver = 400;	/* 4.0 */
				break;
			}
		}
		if(card->specver >= 420) {
			capacity = EXT_CSD(215, 212) * 512ULL;
			if(capacity > 0x80000000ULL)
				card->sectors[0] = capacity / card->secsize;

			capacity = EXT_CSD(226, 226) * 0x20000ULL;
			card->sectors[1] = capacity / card->secsize;
			card->sectors[2] = capacity / card->secsize;
		}
	} else {
		switch(CSD(127, 126)){
		case 0:				/* SD Version 1.0 */
			card->specver = 100;
			break;
		case 1:				/* SD Version 2.0 */
			card->specver = 200;
			csize = CSD(69, 48);
			capacity = (csize+1) * 0x80000ULL;
			card->sectors[0] = capacity / card->secsize;
			break;
		}
	}

	if(card->secsize == 1024){
		card->secsize = 512;
		card->sectors[0] <<= 1;
		card->sectors[1] <<= 1;
		card->sectors[2] <<= 1;
	}
}

static int
mmcverify(SDunit *unit)
{
	Card *card = unit->dev->ctlr;
	SDio *io = card->io;
	int n;

	eqlock(card);
	if(waserror()){
		qunlock(card);
		nexterror();
	}
	n = (*io->inquiry)(io, (char*)&unit->inquiry[8], sizeof(unit->inquiry)-8);
	qunlock(card);
	poperror();
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
	Card *card = dev->ctlr;
	SDio *io = card->io;
	(*io->enable)(io);
	return 1;
}

static void
mmcswitch(Card *card, u32int arg)
{
	SDio *io = card->io;
	u32int r[4];
	int i;

	(*io->cmd)(io, &SWITCH, arg, r);

	for(i=0; i<10;i++){
		tsleep(&up->sleep, return0, nil, 100);

		(*io->cmd)(io, &SEND_STATUS, card->rca<<Rcashift, r);
		if(r[0] & (1<<7))
			error(Eio);
		if(r[0] & (1<<8))
			return;
	}
	error(Eio);
}

static void
sdswitchfunc(Card *card, int arg)
{
	SDio *io = card->io;
	u32int r[4];
	uchar *buf;
	int n;

	n = Funcbytes;
	buf = sdmalloc(n);
	if(waserror()){
		sdfree(buf);
		nexterror();
	}
	(*io->iosetup)(io, 0, buf, n, 1);
	(*io->cmd)(io, &SWITCH_FUNC, arg, r);
	if((arg & 0xFFFFFFF0) == Setfunc){
		card->busspeed = (arg & Hispeed) != 0? SDfreqhs: SDfreq;
		tsleep(&up->sleep, return0, nil, 10);
		(*io->bus)(io, 0, card->busspeed);
		tsleep(&up->sleep, return0, nil, 10);
	}
	(*io->io)(io, 0, buf, n);
	sdfree(buf);
	poperror();
}

static int
cardinit(Card *card)
{
	SDio *io = card->io;
	u32int r[4], ocr;
	int i;

	card->secsize = 0;
	card->sectors[0] = 0;
	card->sectors[1] = 0;
	card->sectors[2] = 0;

	card->buswidth = 1;
	card->busspeed = Initfreq;
	(*io->bus)(io, card->buswidth, card->busspeed);
	tsleep(&up->sleep, return0, nil, 10);

	(*io->cmd)(io, &GO_IDLE_STATE, 0, r);

	/* card type unknown */
	card->ismmc = -1;

	if(!waserror()){	/* try SD card first */
		ocr = V3_3;
		if(!waserror()){
			(*io->cmd)(io, &SD_SEND_IF_COND, Voltage|Checkpattern, r);
			if(r[0] == (Voltage|Checkpattern)){	/* SD 2.0 or above */
				ocr |= Hcs;
				card->ismmc = 0;		/* this is SD card */
			}
			poperror();
		}
		for(i = 0; i < Inittimeout; i++){
			tsleep(&up->sleep, return0, nil, 100);
			(*io->cmd)(io, &APP_CMD, 0, r);
			(*io->cmd)(io, &SD_SEND_OP_COND, ocr, r);
			if(r[0] & Powerup)
				break;
		}
		card->ismmc = 0;	/* this is SD card */
		poperror();
		if(i == Inittimeout)
			return 2;
	} else if(card->ismmc) {	/* try MMC if not ruled out */
		(*io->cmd)(io, &GO_IDLE_STATE, 0, r);

		ocr = Hcs|V3_3;
		for(i = 0; i < Inittimeout; i++){
			tsleep(&up->sleep, return0, nil, 100);
			(*io->cmd)(io, &SEND_OP_COND, ocr, r);
			if(r[0] & Powerup)
				break;
		}
		card->ismmc = 1;	/* this is MMC */
		if(i == Inittimeout)
			return 2;
	}

	card->ocr = r[0];
	(*io->cmd)(io, &ALL_SEND_CID, 0, r);
	memmove(card->cid, r, sizeof card->cid);

	if(card->ismmc){
		card->rca = 0;
		(*io->cmd)(io, &SET_RELATIVE_ADDR, card->rca<<16, r);
	} else {
		(*io->cmd)(io, &SEND_RELATIVE_ADDR, 0, r);
		card->rca = r[0]>>16;
	}

	(*io->cmd)(io, &SEND_CSD, card->rca<<Rcashift, r);
	memmove(card->csd, r, sizeof card->csd);

	return 1;
}

static void
retryproc(void *arg)
{
	Card *card = arg;
	int i = 0;

	qlock(card);
	while(waserror())
		;
	if(i++ < card->retry)
		cardinit(card);
	USED(i);
	card->retry = 0;
	qunlock(card);

	pexit("", 1);
}

static int
mmconline(SDunit *unit)
{
	Card *card = unit->dev->ctlr;
	SDio *io = card->io;
	u32int r[4];

	if(card->retry)
		return 0;

	eqlock(card);
	if(waserror()){
		unit->sectors = 0;
		if(card->retry++ == 0)
			kproc(unit->name, retryproc, card);
		qunlock(card);
		return 0;
	}
	if(card->secsize != 0 && card->sectors[0] != 0){
		unit->secsize = card->secsize;
		unit->sectors = card->sectors[unit->subno];
		if(unit->sectors == 0){
			qunlock(card);
			poperror();
			return 0;
		}
		(*io->cmd)(io, &SEND_STATUS, card->rca<<Rcashift, r);
		qunlock(card);
		poperror();
		return 1;
	}
	if(cardinit(card) != 1){
		qunlock(card);
		poperror();
		return 2;
	}
	(*io->cmd)(io, &SELECT_CARD, card->rca<<Rcashift, r);
	tsleep(&up->sleep, return0, nil, 10);

	(*io->bus)(io, 0, card->busspeed = SDfreq);
	tsleep(&up->sleep, return0, nil, 10);

	identify(card);
	unit->secsize = card->secsize;
	unit->sectors = card->sectors[unit->subno];

	(*io->cmd)(io, &SET_BLOCKLEN, card->secsize, r);

	if(card->ismmc && card->specver >= 400){
		if(!waserror()){
			mmcswitch(card, MMCSetHSTiming);
			(*io->bus)(io, 0, card->busspeed = SDfreqhs);
			tsleep(&up->sleep, return0, nil, 10);
			readextcsd(card);
			poperror();
		} else {
			mmcswitch(card, MMCSetSDTiming);
			(*io->bus)(io, 0, card->busspeed = SDfreq);
			tsleep(&up->sleep, return0, nil, 10);
			readextcsd(card);
		}
		if(!waserror()){
			mmcswitch(card, MMCSetBusWidth8);
			(*io->bus)(io, card->buswidth = 8, 0);
			readextcsd(card);
			poperror();
		} else if(!waserror()){
			mmcswitch(card, MMCSetBusWidth4);
			(*io->bus)(io, card->buswidth = 4, 0);
			readextcsd(card);
			poperror();
		} else {
			mmcswitch(card, MMCSetBusWidth1);
			(*io->bus)(io, card->buswidth = 1, 0);
			readextcsd(card);
		}
	} else if(!card->ismmc) {
		(*io->cmd)(io, &APP_CMD, card->rca<<Rcashift, r);
		(*io->cmd)(io, &SD_SET_BUS_WIDTH, Width4, r);
		(*io->bus)(io, card->buswidth = 4, 0);
		if(!waserror()){
			sdswitchfunc(card, Hispeed|Setfunc);
			poperror();
		}
	}
	qunlock(card);
	poperror();
	return 1;
}

static char*
mmcrctl(SDunit *unit, char *p, char *e)
{
	Card *card = unit->dev->ctlr;
	int i;

	if(card->sectors[0] == 0)
		mmconline(unit);
	if(unit->sectors == 0)
		return p;

	p = seprint(p, e, "version %s %d.%2.2d\n", card->ismmc? "MMC": "SD",
		card->specver/100, card->specver%100);

	p = seprint(p, e, "rca %4.4ux ocr %8.8ux", card->rca, card->ocr);

	p = seprint(p, e, "\ncid ");
	for(i = nelem(card->cid)-1; i >= 0; i--)
		p = seprint(p, e, "%8.8ux", card->cid[i]);

	p = seprint(p, e, "\ncsd ");
	for(i = nelem(card->csd)-1; i >= 0; i--)
		p = seprint(p, e, "%8.8ux", card->csd[i]);

	if(card->ismmc)
		p = seprint(p, e, "\nboot %s",
			((card->ext_csd[179]>>3)&7) == (unit->subno==0? 7: unit->subno)?
			"enabled": "disabled" );

	p = seprint(p, e, "\ngeometry %llud %ld\n",
		unit->sectors, unit->secsize);
	return p;
}

static long
mmcbio(SDunit *unit, int lun, int write, void *data, long nb, uvlong bno)
{
	Card *card = unit->dev->ctlr;
	SDio *io = card->io;
	int len, tries;
	u32int r[4];
	uchar *buf;
	ulong b;

	USED(lun);
	if(unit->sectors == 0)
		error(Echange);

	eqlock(card);
	if(waserror()){
		if(io->led != nil)
			(*io->led)(io, 0);
		qunlock(card);
		nexterror();
	}
	if(io->led != nil)
		(*io->led)(io, 1);

	if(card->ismmc && unit->subno != (card->ext_csd[179]&7)){
		b = (card->ext_csd[179] & ~7) | unit->subno;
		mmcswitch(card, 3<<24 | 179<<16 | b<<8);
		card->ext_csd[179] = b;
	}

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
			(*io->cmd)(io, &STOP_TRANSMISSION, 0, r);
			nexterror();
		}
		(*io->cmd)(io, write? &WRITE_MULTIPLE_BLOCK: &READ_MULTIPLE_BLOCK,
			card->ocr & Ccs? b: b * len, r);
		(*io->io)(io, write, buf, nb * len);
		poperror();
		(*io->cmd)(io, &STOP_TRANSMISSION, 0, r);
		poperror();
		b += nb;
	}else{
		for(b = bno; b < bno + nb; b++){
			(*io->iosetup)(io, write, buf, len, 1);
			(*io->cmd)(io, write? &WRITE_SINGLE_BLOCK: &READ_SINGLE_BLOCK,
				card->ocr & Ccs? b: b * len, r);
			(*io->io)(io, write, buf, len);
			buf += len;
		}
	}
	if(io->led != nil)
		(*io->led)(io, 0);
	qunlock(card);
	poperror();

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
	.clear	= mmcclear,
};
