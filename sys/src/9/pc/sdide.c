#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#include "../port/sd.h"
#include <fis.h>

#define	HOWMANY(x, y)	(((x)+((y)-1))/(y))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))
#define uprint(...)	snprint(up->genbuf, sizeof up->genbuf, __VA_ARGS__);
#pragma	varargck	argpos	atadebug		3

extern SDifc sdideifc;

enum {
	DbgCONFIG	= 0x0001,	/* detected drive config info */
	DbgIDENTIFY	= 0x0002,	/* detected drive identify info */
	DbgSTATE	= 0x0004,	/* dump state on panic */
	DbgPROBE	= 0x0008,	/* trace device probing */
	DbgDEBUG	= 0x0080,	/* the current problem... */
	DbgINL		= 0x0100,	/* That Inil20+ message we hate */
	Dbg48BIT	= 0x0200,	/* 48-bit LBA */
	DbgBsy		= 0x0400,	/* interrupt but Bsy (shared IRQ) */
	DbgAtazz	= 0x0800,	/* debug raw ata io */
};
#define DEBUG		(DbgDEBUG|DbgSTATE)

enum {					/* I/O ports */
	Data		= 0,
	Error		= 1,		/* (read) */
	Features	= 1,		/* (write) */
	Count		= 2,		/* sector count<7-0>, sector count<15-8> */
	Ir		= 2,		/* interrupt reason (PACKET) */
	Sector		= 3,		/* sector number */
	Lbalo		= 3,		/* LBA<7-0>, LBA<31-24> */
	Cyllo		= 4,		/* cylinder low */
	Bytelo		= 4,		/* byte count low (PACKET) */
	Lbamid		= 4,		/* LBA<15-8>, LBA<39-32> */
	Cylhi		= 5,		/* cylinder high */
	Bytehi		= 5,		/* byte count hi (PACKET) */
	Lbahi		= 5,		/* LBA<23-16>, LBA<47-40> */
	Dh		= 6,		/* Device/Head, LBA<27-24> */
	Status		= 7,		/* (read) */
	Command		= 7,		/* (write) */

	As		= 2,		/* Alternate Status (read) */
	Dc		= 2,		/* Device Control (write) */
};

enum {					/* Error */
	Med		= 0x01,		/* Media error */
	Ili		= 0x01,		/* command set specific (PACKET) */
	Nm		= 0x02,		/* No Media */
	Eom		= 0x02,		/* command set specific (PACKET) */
	Abrt		= 0x04,		/* Aborted command */
	Mcr		= 0x08,		/* Media Change Request */
	Idnf		= 0x10,		/* no user-accessible address */
	Mc		= 0x20,		/* Media Change */
	Unc		= 0x40,		/* Uncorrectable data error */
	Wp		= 0x40,		/* Write Protect */
	Icrc		= 0x80,		/* Interface CRC error */
};

enum {					/* Features */
	Dma		= 0x01,		/* data transfer via DMA (PACKET) */
	Ovl		= 0x02,		/* command overlapped (PACKET) */
};

enum {					/* Interrupt Reason */
	Cd		= 0x01,		/* Command/Data */
	Io		= 0x02,		/* I/O direction */
	Rel		= 0x04,		/* Bus Release */
};

enum {					/* Device/Head */
	Dev0		= 0xA0,		/* Master */
	Dev1		= 0xB0,		/* Slave */
	Devs		= Dev0 | Dev1,
	Lba		= 0x40,		/* LBA mode */
};

enum {					/* Status, Alternate Status */
	Err		= 0x01,		/* Error */
	Chk		= 0x01,		/* Check error (PACKET) */
	Drq		= 0x08,		/* Data Request */
	Dsc		= 0x10,		/* Device Seek Complete */
	Serv		= 0x10,		/* Service */
	Df		= 0x20,		/* Device Fault */
	Dmrd		= 0x20,		/* DMA ready (PACKET) */
	Drdy		= 0x40,		/* Device Ready */
	Bsy		= 0x80,		/* Busy */
};

enum {					/* Command */
	Cnop		= 0x00,		/* NOP */
	Crs		= 0x20,		/* Read Sectors */
	Crs48		= 0x24,		/* Read Sectors Ext */
	Crd48		= 0x25,		/* Read w/ DMA Ext */
	Crsm48		= 0x29,		/* Read Multiple Ext */
	Cws		= 0x30,		/* Write Sectors */
	Cws48		= 0x34,		/* Write Sectors Ext */
	Cwd48		= 0x35,		/* Write w/ DMA Ext */
	Cwsm48		= 0x39,		/* Write Multiple Ext */
	Cedd		= 0x90,		/* Execute Device Diagnostics */
	Cpkt		= 0xA0,		/* Packet */
	Cidpkt		= 0xA1,		/* Identify Packet Device */
	Crsm		= 0xC4,		/* Read Multiple */
	Cwsm		= 0xC5,		/* Write Multiple */
	Csm		= 0xC6,		/* Set Multiple */
	Crd		= 0xC8,		/* Read DMA */
	Cwd		= 0xCA,		/* Write DMA */
	Cid		= 0xEC,		/* Identify Device */
};

enum {					/* Device Control */
	Nien		= 0x02,		/* (not) Interrupt Enable */
	Srst		= 0x04,		/* Software Reset */
	Hob		= 0x80,		/* High Order Bit [sic] */
};

enum {					/* PCI Configuration Registers */
	Bmiba		= 0x20,		/* Bus Master Interface Base Address */
	Idetim		= 0x40,		/* IE Timing */
	Sidetim		= 0x44,		/* Slave IE Timing */
	Udmactl		= 0x48,		/* Ultra DMA/33 Control */
	Udmatim		= 0x4A,		/* Ultra DMA/33 Timing */
};

enum {					/* Bus Master IDE I/O Ports */
	Bmicx		= 0,		/* Command */
	Bmisx		= 2,		/* Status */
	Bmidtpx		= 4,		/* Descriptor Table Pointer */
};

enum {					/* Bmicx */
	Ssbm		= 0x01,		/* Start/Stop Bus Master */
	Rwcon		= 0x08,		/* Read/Write Control */
};

enum {					/* Bmisx */
	Bmidea		= 0x01,		/* Bus Master IDE Active */
	Idedmae		= 0x02,		/* IDE DMA Error  (R/WC) */
	Ideints		= 0x04,		/* IDE Interrupt Status (R/WC) */
	Dma0cap		= 0x20,		/* Drive 0 DMA Capable */
	Dma1cap		= 0x40,		/* Drive 0 DMA Capable */
};
enum {					/* Physical Region Descriptor */
	PrdEOT		= 0x80000000,	/* End of Transfer */
};

enum {					/* offsets into the identify info. */
	Iconfig		= 0,		/* general configuration */
	Ilcyl		= 1,		/* logical cylinders */
	Ilhead		= 3,		/* logical heads */
	Ilsec		= 6,		/* logical sectors per logical track */
	Iserial		= 10,		/* serial number */
	Ifirmware	= 23,		/* firmware revision */
	Imodel		= 27,		/* model number */
	Imaxrwm		= 47,		/* max. read/write multiple sectors */
	Icapabilities	= 49,		/* capabilities */
	Istandby	= 50,		/* device specific standby timer */
	Ipiomode	= 51,		/* PIO data transfer mode number */
	Ivalid		= 53,
	Iccyl		= 54,		/* cylinders if (valid&0x01) */
	Ichead		= 55,		/* heads if (valid&0x01) */
	Icsec		= 56,		/* sectors if (valid&0x01) */
	Iccap		= 57,		/* capacity if (valid&0x01) */
	Irwm		= 59,		/* read/write multiple */
	Ilba		= 60,		/* LBA size */
	Imwdma		= 63,		/* multiword DMA mode */
	Iapiomode	= 64,		/* advanced PIO modes supported */
	Iminmwdma	= 65,		/* min. multiword DMA cycle time */
	Irecmwdma	= 66,		/* rec. multiword DMA cycle time */
	Iminpio		= 67,		/* min. PIO cycle w/o flow control */
	Iminiordy	= 68,		/* min. PIO cycle with IORDY */
	Ipcktbr		= 71,		/* time from PACKET to bus release */
	Iserbsy		= 72,		/* time from SERVICE to !Bsy */
	Iqdepth		= 75,		/* max. queue depth */
	Imajor		= 80,		/* major version number */
	Iminor		= 81,		/* minor version number */
	Icsfs		= 82,		/* command set/feature supported */
	Icsfe		= 85,		/* command set/feature enabled */
	Iudma		= 88,		/* ultra DMA mode */
	Ierase		= 89,		/* time for security erase */
	Ieerase		= 90,		/* time for enhanced security erase */
	Ipower		= 91,		/* current advanced power management */
	Ilba48		= 100,		/* 48-bit LBA size (64 bits in 100-103) */
	Irmsn		= 127,		/* removable status notification */
	Isecstat	= 128,		/* security status */
	Icfapwr		= 160,		/* CFA power mode */
	Imediaserial	= 176,		/* current media serial number */
	Icksum		= 255,		/* checksum */
};

enum {					/* bit masks for config identify info */
	Mpktsz		= 0x0003,	/* packet command size */
	Mincomplete	= 0x0004,	/* incomplete information */
	Mdrq		= 0x0060,	/* DRQ type */
	Mrmdev		= 0x0080,	/* device is removable */
	Mtype		= 0x1F00,	/* device type */
	Mproto		= 0x8000,	/* command protocol */
};

enum {					/* bit masks for capabilities identify info */
	Mdma		= 0x0100,	/* DMA supported */
	Mlba		= 0x0200,	/* LBA supported */
	Mnoiordy	= 0x0400,	/* IORDY may be disabled */
	Miordy		= 0x0800,	/* IORDY supported */
	Msoftrst	= 0x1000,	/* needs soft reset when Bsy */
	Mqueueing	= 0x4000,	/* queueing overlap supported */
	Midma		= 0x8000,	/* interleaved DMA supported */
};

enum {					/* bit masks for supported/enabled features */
	Msmart		= 0x0001,
	Msecurity	= 0x0002,
	Mrmmedia	= 0x0004,
	Mpwrmgmt	= 0x0008,
	Mpkt		= 0x0010,
	Mwcache		= 0x0020,
	Mlookahead	= 0x0040,
	Mrelirq		= 0x0080,
	Msvcirq		= 0x0100,
	Mreset		= 0x0200,
	Mprotected	= 0x0400,
	Mwbuf		= 0x1000,
	Mrbuf		= 0x2000,
	Mnop		= 0x4000,
	Mmicrocode	= 0x0001,
	Mqueued		= 0x0002,
	Mcfa		= 0x0004,
	Mapm		= 0x0008,
	Mnotify		= 0x0010,
	Mspinup		= 0x0040,
	Mmaxsec		= 0x0100,
	Mautoacoustic	= 0x0200,
	Maddr48		= 0x0400,
	Mdevconfov	= 0x0800,
	Mflush		= 0x1000,
	Mflush48	= 0x2000,
	Msmarterror	= 0x0001,
	Msmartselftest	= 0x0002,
	Mmserial	= 0x0004,
	Mmpassthru	= 0x0008,
	Mlogging	= 0x0020,
};

typedef struct Ctlr Ctlr;
typedef struct Drive Drive;

typedef struct Prd {			/* Physical Region Descriptor */
	ulong	pa;			/* Physical Base Address */
	int	count;
} Prd;

enum {
	BMspan		= 32*1024,	/* must be power of 2 <= 64*1024 */

	Nprd		= SDmaxio/BMspan+2,
};

typedef struct Ctlr {
	int	cmdport;
	int	ctlport;
	int	irq;
	int	tbdf;
	int	bmiba;			/* bus master interface base address */
	int	maxio;			/* sector count transfer maximum */
	int	span;			/* don't span this boundary with dma */
	int	maxdma;			/* don't attempt dma transfers bigger than this */

	Pcidev*	pcidev;
	void	(*ienable)(Ctlr*);
	void	(*idisable)(Ctlr*);
	SDev*	sdev;

	Drive*	drive[2];

	Prd*	prdt;			/* physical region descriptor table */
	void	(*irqack)(Ctlr*);

	QLock;				/* current command */
	Drive*	curdrive;
	int	command;		/* last command issued (debugging) */
	Rendez;
	int	done;
	uint	nrq;
	uint	nildrive;
	uint	bsy;

	Lock;				/* register access */
} Ctlr;

typedef struct Drive {
	Ctlr*	ctlr;
	SDunit	*unit;

	int	dev;
	ushort	info[256];
	Sfis;

	int	dma;			/* DMA R/W possible */
	int	dmactl;
	int	rwm;			/* read/write multiple possible */
	int	rwmctl;

	int	pkt;			/* PACKET device, length of pktcmd */
	uchar	pktcmd[16];
	int	pktdma;			/* this PACKET command using dma */

	uvlong	sectors;
	uint	secsize;
	char	serial[20+1];
	char	firmware[8+1];
	char	model[40+1];

	QLock;				/* drive access */
	int	command;		/* current command */
	int	write;
	uchar*	data;
	int	dlen;
	uchar*	limit;
	int	count;			/* sectors */
	int	block;			/* R/W bytes per block */
	int	status;
	int	error;
	int	flags;			/* internal flags */
	uint	missirq;
	uint	spurloop;
	uint	irq;
	uint	bsy;
} Drive;

enum {					/* internal flags */
	Lba48always	= 0x2,		/* ... */
	Online		= 0x4,		/* drive onlined */
};

static void
pc87415ienable(Ctlr* ctlr)
{
	Pcidev *p;
	int x;

	p = ctlr->pcidev;
	if(p == nil)
		return;

	x = pcicfgr32(p, 0x40);
	if(ctlr->cmdport == p->mem[0].bar)
		x &= ~0x00000100;
	else
		x &= ~0x00000200;
	pcicfgw32(p, 0x40, x);
}

static void
atadumpstate(Drive* drive, SDreq *r, uvlong lba, int count)
{
	Prd *prd;
	Pcidev *p;
	Ctlr *ctlr;
	int i, bmiba, ccnt;
	uvlong clba;

	if(!(DEBUG & DbgSTATE))
		return;

	ctlr = drive->ctlr;
	print("command %2.2uX\n", ctlr->command);
	print("data %8.8p limit %8.8p dlen %d status %uX error %uX\n",
		drive->data, drive->limit, drive->dlen,
		drive->status, drive->error);
	if(r->clen == -16)
		clba = fisrw(nil, r->cmd, &ccnt);
	else 
		sdfakescsirw(r, &clba, &ccnt, 0);
	print("lba %llud -> %llud, count %d -> %d (%d)\n",
		clba, lba, ccnt, count, drive->count);
	if(!(inb(ctlr->ctlport+As) & Bsy)){
		for(i = 1; i < 7; i++)
			print(" 0x%2.2uX", inb(ctlr->cmdport+i));
		print(" 0x%2.2uX\n", inb(ctlr->ctlport+As));
	}
	if(drive->command == Cwd || drive->command == Crd
	|| drive->command == (Pdma|Pin) || drive->command == (Pdma|Pout)){
		bmiba = ctlr->bmiba;
		prd = ctlr->prdt;
		print("bmicx %2.2uX bmisx %2.2uX prdt %8.8p\n",
			inb(bmiba+Bmicx), inb(bmiba+Bmisx), prd);
		while(prd){
			print("pa 0x%8.8luX count %8.8uX\n",
				prd->pa, prd->count);
			if(prd->count & PrdEOT)
				break;
			prd++;
		}
	}
	if(ctlr->pcidev && ctlr->pcidev->vid == 0x8086){
		p = ctlr->pcidev;
		print("0x40: %4.4uX 0x42: %4.4uX ",
			pcicfgr16(p, 0x40), pcicfgr16(p, 0x42));
		print("0x48: %2.2uX\n", pcicfgr8(p, 0x48));
		print("0x4A: %4.4uX\n", pcicfgr16(p, 0x4A));
	}
}

static void
atadebug(int cmdport, int ctlport, char* fmt, ...)
{
	char *p, *e, buf[PRINTSIZE];
	int i;
	va_list arg;

	if(!(DEBUG & DbgPROBE))
		return;

	p = buf;
	e = buf + sizeof buf;
	va_start(arg, fmt);
	p = vseprint(p, e, fmt, arg);
	va_end(arg);

	if(cmdport){
		if(p > buf && p[-1] == '\n')
			p--;
		p = seprint(p, e, " ataregs 0x%uX:", cmdport);
		for(i = Features; i < Command; i++)
			p = seprint(p, e, " 0x%2.2uX", inb(cmdport+i));
		if(ctlport)
			p = seprint(p, e, " 0x%2.2uX", inb(ctlport+As));
		p = seprint(p, e, "\n");
	}
	putstrn(buf, p - buf);
}

static int
ataready(int cmdport, int ctlport, int dev, int reset, int ready, int m)
{
	int as, m0;

	atadebug(cmdport, ctlport, "ataready: dev %ux:%ux reset %ux ready %ux",
		cmdport, dev, reset, ready);
	m0 = m;
	do{
		/*
		 * Wait for the controller to become not busy and
		 * possibly for a status bit to become true (usually
		 * Drdy). Must change to the appropriate device
		 * register set if necessary before testing for ready.
		 * Always run through the loop at least once so it
		 * can be used as a test for !Bsy.
		 */
		as = inb(ctlport+As);
		if(as & reset){
			/* nothing to do */
		}
		else if(dev){
			outb(cmdport+Dh, dev);
			dev = 0;
		}
		else if(ready == 0 || (as & ready)){
			atadebug(0, 0, "ataready: %d:%d %#.2ux\n", m, m0, as);
			return as;
		}
		microdelay(1);
	}while(m-- > 0);
	atadebug(0, 0, "ataready: timeout %d %#.2ux\n", m0, as);
	return -1;
}

static int
atadone(void* arg)
{
	return ((Ctlr*)arg)->done;
}

static int
atarwmmode(Drive* drive, int cmdport, int ctlport, int dev)
{
	int as, maxrwm, rwm;

	maxrwm = drive->info[Imaxrwm] & 0xFF;
	if(maxrwm == 0)
		return 0;

	/*
	 * Sometimes drives come up with the current count set
	 * to 0; if so, set a suitable value, otherwise believe
	 * the value in Irwm if the 0x100 bit is set.
	 */
	if(drive->info[Irwm] & 0x100)
		rwm = drive->info[Irwm] & 0xFF;
	else
		rwm = 0;
	if(rwm == 0)
		rwm = maxrwm;
	if(rwm > 16)
		rwm = 16;
	if(ataready(cmdport, ctlport, dev, Bsy|Drq, Drdy, 102*1000) < 0)
		return 0;
	outb(cmdport+Count, rwm);
	outb(cmdport+Command, Csm);
	microdelay(1);
	as = ataready(cmdport, ctlport, 0, Bsy, Drdy|Df|Err, 1000);
	inb(cmdport+Status);
	if(as < 0 || (as & (Df|Err)))
		return 0;

	drive->rwm = rwm;

	return rwm;
}

static int
atadmamode(SDunit *unit, Drive* drive)
{
	char buf[32], *s;
	int dma;

	/*
	 * Check if any DMA mode enabled.
	 * Assumes the BIOS has picked and enabled the best.
	 * This is completely passive at the moment, no attempt is
	 * made to ensure the hardware is correctly set up.
	 */
	dma = drive->info[Imwdma] & 0x0707;
	drive->dma = (dma>>8) & dma;
	if(drive->dma == 0 && (drive->info[Ivalid] & 0x04)){
		dma = drive->info[Iudma] & 0x7F7F;
		drive->dma = (dma>>8) & dma;
		if(drive->dma)
			drive->dma |= 'U'<<16;
	}
	if(unit != nil){
		snprint(buf, sizeof buf, "*%sdma", unit->name);
		s = getconf(buf);
		if((s && !strcmp(s, "on")) || (!s && !getconf("*nodma")))
			drive->dmactl = drive->dma;
	}
	return dma;
}

static int
ataidentify(Ctlr*, int cmdport, int ctlport, int dev, int pkt, void* info)
{
	int as, command, drdy;

	if(pkt){
		command = Cidpkt;
		drdy = 0;
	}
	else{
		command = Cid;
		drdy = Drdy;
	}
	dev &= ~Lba;
	as = ataready(cmdport, ctlport, dev, Bsy|Drq, drdy, 103*1000);
	if(as < 0)
		return -1;
	outb(cmdport+Command, command);
	microdelay(1);

	as = ataready(cmdport, ctlport, 0, Bsy, Drq|Err, 400*1000);
	if(as < 0 || (as & Err))
		return as;
	memset(info, 0, 512);
	inss(cmdport+Data, info, 256);
	inb(cmdport+Status);
	return 0;
}

static Drive*
atadrive(SDunit *unit, Drive *drive, int cmdport, int ctlport, int dev)
{
	int as, pkt, rlo, rhi;
	uchar buf[512], oserial[21];
	uvlong osectors;
	Ctlr *ctlr;

	if(DEBUG & DbgIDENTIFY)
		print("identify: port %ux dev %.2ux\n", cmdport, dev & ~Lba);

	atadebug(0, 0, "identify: port 0x%uX dev 0x%2.2uX\n", cmdport, dev);
	if(drive != nil){
		osectors = drive->sectors;
		memmove(oserial, drive->serial, sizeof drive->serial);
		ctlr = drive->ctlr;
	}else{
		osectors = 0;
		memset(oserial, 0, sizeof drive->serial);
		ctlr = nil;

		/* detect if theres a drive present */
		outb(cmdport+Dh, dev & ~Lba);
		microdelay(1);
		outb(cmdport+Cyllo, 0xAA);
		outb(cmdport+Cylhi, 0x55);
		outb(cmdport+Sector, 0xFF);
		rlo = inb(cmdport+Cyllo);
		rhi = inb(cmdport+Cylhi);
		if(rlo != 0xAA && (rlo == 0xFF || rhi != 0x55))
			return nil;
	}

	pkt = 1;
retry:
	as = ataidentify(ctlr, cmdport, ctlport, dev, pkt, buf);
	if(as < 0)
		return nil;
	if(as & Err){
		if(pkt == 0)
			return nil;
		pkt = 0;
		goto retry;
	}

	if(drive == 0){
		if((drive = malloc(sizeof(Drive))) == nil)
			return nil;
		drive->serial[0] = ' ';
		drive->dev = dev;
	}

	memmove(drive->info, buf, sizeof(drive->info));

	setfissig(drive, pkt ? 0xeb140000 : 0x0101);
	drive->sectors = idfeat(drive, drive->info);
	drive->secsize = idss(drive, drive->info);

	idmove(drive->serial, drive->info+10, 20);
	idmove(drive->firmware, drive->info+23, 8);
	idmove(drive->model, drive->info+27, 40);
	if(unit != nil){
		memset(unit->inquiry, 0, sizeof unit->inquiry);
		unit->inquiry[2] = 2;
		unit->inquiry[3] = 2;
		unit->inquiry[4] = sizeof unit->inquiry - 4;
		memmove(unit->inquiry+8, drive->model, 40);
	}

	if(pkt){
		drive->pkt = 12;
		if(drive->feat & Datapi16)
			drive->pkt = 16;
	}else{
		drive->pkt = 0;
		if(drive->feat & Dlba)
			drive->dev |= Lba;
		atarwmmode(drive, cmdport, ctlport, dev);
	}
	atadmamode(unit, drive);	

	if(osectors != 0 && memcmp(oserial, drive->serial, sizeof oserial) != 0)
		if(unit)
			unit->sectors = 0;
	drive->unit = unit;
	if(DEBUG & DbgCONFIG){
		print("dev %2.2uX port %uX config %4.4uX capabilities %4.4uX",
			dev, cmdport, drive->info[Iconfig], drive->info[Icapabilities]);
		print(" mwdma %4.4uX", drive->info[Imwdma]);
		if(drive->info[Ivalid] & 0x04)
			print(" udma %4.4uX", drive->info[Iudma]);
		print(" dma %8.8uX rwm %ud", drive->dma, drive->rwm);
		if(drive->feat&Dllba)
			print("\tLLBA sectors %llud", drive->sectors);
		print("\n");
	}

	return drive;
}

static void
atasrst(int ctlport)
{
	int dc0;

	/*
	 * Srst is a big stick and may cause problems if further
	 * commands are tried before the drives become ready again.
	 * Also, there will be problems here if overlapped commands
	 * are ever supported.
	 */
	dc0 = inb(ctlport+Dc);
	microdelay(5);
	outb(ctlport+Dc, Srst|dc0);
	microdelay(5);
	outb(ctlport+Dc, dc0);
	microdelay(2*1000);
}

static SDev*
ataprobe(int cmdport, int ctlport, int irq, int map)
{
	static int nonlegacy = 'C';
	Ctlr* ctlr;
	SDev *sdev;

	if(ioalloc(cmdport, 8, 0, "atacmd") < 0) {
		print("ataprobe: Cannot allocate %X\n", cmdport);
		return nil;
	}
	if(ioalloc(ctlport+As, 1, 0, "atactl") < 0){
		print("ataprobe: Cannot allocate %X\n", ctlport + As);
		iofree(cmdport);
		return nil;
	}

	if((ctlr = malloc(sizeof(Ctlr))) == nil)
		goto release;
	if((sdev = malloc(sizeof(SDev))) == nil){
		free(ctlr);
		goto release;
	}

	if((map & 2) && (ctlr->drive[1] = atadrive(0, 0, cmdport, ctlport, Dev1)))
		ctlr->drive[1]->ctlr = ctlr;
	if((map & 1) && (ctlr->drive[0] = atadrive(0, 0, cmdport, ctlport, Dev0)))
		ctlr->drive[0]->ctlr = ctlr;

	if(ctlr->drive[0] == nil && ctlr->drive[1] == nil){
		free(ctlr->drive[0]);
		free(ctlr->drive[1]);
		free(ctlr);
		free(sdev);
		goto release;
	}

	ctlr->cmdport = cmdport;
	ctlr->ctlport = ctlport;
	ctlr->irq = irq;
	ctlr->tbdf = BUSUNKNOWN;
	ctlr->command = Cnop;		/* debugging */

	switch(cmdport){
	default:
		sdev->idno = nonlegacy;
		break;
	case 0x1F0:
		sdev->idno = 'C';
		nonlegacy = 'E';
		break;
	case 0x170:
		sdev->idno = 'D';
		nonlegacy = 'E';
		break;
	}
	sdev->ifc = &sdideifc;
	sdev->ctlr = ctlr;
	sdev->nunit = 2;
	ctlr->sdev = sdev;

	return sdev;

release:
	iofree(cmdport);
	iofree(ctlport+As);

	return nil;
}

static void
ataclear(SDev *sdev)
{
	Ctlr* ctlr;

	ctlr = sdev->ctlr;
	iofree(ctlr->cmdport);
	iofree(ctlr->ctlport + As);

	if (ctlr->drive[0])
		free(ctlr->drive[0]);
	if (ctlr->drive[1])
		free(ctlr->drive[1]);
	if (sdev->name)
		free(sdev->name);
	if (sdev->unitflg)
		free(sdev->unitflg);
	if (sdev->unit)
		free(sdev->unit);
	free(ctlr);
	free(sdev);
}

static char *
atastat(SDev *sdev, char *p, char *e)
{
	Ctlr *ctlr;

	ctlr = sdev->ctlr;
//	return seprint(p, e, "%s ata port %X ctl %X irq %d %T\n", 
//		    sdev->name, ctlr->cmdport, ctlr->ctlport, ctlr->irq, ctlr->tbdf);
	return seprint(p, e, "%s ata port %X ctl %X irq %d\n", 
		    sdev->name, ctlr->cmdport, ctlr->ctlport, ctlr->irq);
}

static SDev*
ataprobew(DevConf *cf)
{
	char *p;
	ISAConf isa;
	
	if (cf->nports != 2)
		error(Ebadarg);

	memset(&isa, 0, sizeof isa);
	isa.port = cf->ports[0].port;
	isa.irq = cf->intnum;
	if((p=strchr(cf->type, '/')) == nil || pcmspecial(p+1, &isa) < 0)
		error("cannot find controller");

	return ataprobe(cf->ports[0].port, cf->ports[1].port, cf->intnum, 3);
}

static void atainterrupt(Ureg*, void*);

static int
iowait(Drive *drive, int ms, int interrupt)
{
	int msec, step;
	Ctlr *ctlr;

	step = 1000;
	if(drive->missirq > 10)
		step = 50;
	ctlr = drive->ctlr;
	for(msec = 0; msec < ms; msec += step){
		while(waserror())
			if(interrupt)
				return -1;
		tsleep(ctlr, atadone, ctlr, step);
		poperror();
		if(ctlr->done)
			break;
		atainterrupt(nil, ctlr);
		if(ctlr->done){
			if(drive->missirq++ < 3)
				print("ide: caught missed irq\n");
			break;
		}else
			drive->spurloop++;
	}
	return ctlr->done;
}

static void
atanop(Drive* drive, int subcommand)
{
	Ctlr* ctlr;
	int as, cmdport, ctlport, timeo;

	/*
	 * Attempt to abort a command by using NOP.
	 * In response, the drive is supposed to set Abrt
	 * in the Error register, set (Drdy|Err) in Status
	 * and clear Bsy when done. However, some drives
	 * (e.g. ATAPI Zip) just go Bsy then clear Status
	 * when done, hence the timeout loop only on Bsy
	 * and the forced setting of drive->error.
	 */
	ctlr = drive->ctlr;
	cmdport = ctlr->cmdport;
	outb(cmdport+Features, subcommand);
	outb(cmdport+Dh, drive->dev);
	ctlr->command = Cnop;		/* debugging */
	outb(cmdport+Command, Cnop);

	microdelay(1);
	ctlport = ctlr->ctlport;
	for(timeo = 0; timeo < 1000; timeo++){
		as = inb(ctlport+As);
		if(!(as & Bsy))
			break;
		microdelay(1);
	}
	drive->error |= Abrt;
}

static void
ataabort(Drive* drive, int dolock)
{
	/*
	 * If NOP is available use it otherwise
	 * must try a software reset.
	 */
	if(dolock)
		ilock(drive->ctlr);
	if(drive->feat & Dnop)
		atanop(drive, 0);
	else{
		atasrst(drive->ctlr->ctlport);
		drive->error |= Abrt;
	}
	if(dolock)
		iunlock(drive->ctlr);
}

static int
atadmasetup(Drive* drive, int len)
{
	Prd *prd;
	ulong pa;
	Ctlr *ctlr;
	int bmiba, bmisx, count, i, span;

	ctlr = drive->ctlr;
	pa = PCIWADDR(drive->data);
	if(pa & 0x03)
		return -1;
	if(ctlr->maxdma && len > ctlr->maxdma)
		return -1;

	/*
	 * Sometimes drives identify themselves as being DMA capable
	 * although they are not on a busmastering controller.
	 */
	prd = ctlr->prdt;
	if(prd == nil){
		drive->dmactl = 0;
		print("disabling dma: not on a busmastering controller\n");
		return -1;
	}

	for(i = 0; len && i < Nprd; i++){
		prd->pa = pa;
		span = ROUNDUP(pa, ctlr->span);
		if(span == pa)
			span += ctlr->span;
		count = span - pa;
		if(count >= len){
			prd->count = PrdEOT|len;
			break;
		}
		prd->count = count;
		len -= count;
		pa += count;
		prd++;
	}
	if(i == Nprd)
		return -1;

	bmiba = ctlr->bmiba;
	outl(bmiba+Bmidtpx, PCIWADDR(ctlr->prdt));
	if(drive->write)
		outb(bmiba+Bmicx, 0);
	else
		outb(bmiba+Bmicx, Rwcon);
	bmisx = inb(bmiba+Bmisx);
	outb(bmiba+Bmisx, bmisx|Ideints|Idedmae);

	return 0;
}

static void
atadmastart(Ctlr* ctlr, int write)
{
	if(write)
		outb(ctlr->bmiba+Bmicx, Ssbm);
	else
		outb(ctlr->bmiba+Bmicx, Rwcon|Ssbm);
}

static int
atadmastop(Ctlr* ctlr)
{
	int bmiba;

	bmiba = ctlr->bmiba;
	outb(bmiba+Bmicx, inb(bmiba+Bmicx) & ~Ssbm);

	return inb(bmiba+Bmisx);
}

static void
atadmainterrupt(Drive* drive, int count)
{
	Ctlr* ctlr;
	int bmiba, bmisx;

	ctlr = drive->ctlr;
	bmiba = ctlr->bmiba;
	bmisx = inb(bmiba+Bmisx);
	switch(bmisx & (Ideints|Idedmae|Bmidea)){
	case Bmidea:
		/*
		 * Data transfer still in progress, nothing to do
		 * (this should never happen).
		 */
		return;

	case Ideints:
	case Ideints|Bmidea:
		/*
		 * Normal termination, tidy up.
		 */
		drive->data += count;
		break;

	default:
		/*
		 * What's left are error conditions (memory transfer
		 * problem) and the device is not done but the PRD is
		 * exhausted. For both cases must somehow tell the
		 * drive to abort.
		 */
		ataabort(drive, 0);
		break;
	}
	atadmastop(ctlr);
	ctlr->done = 1;
}

static void
atapktinterrupt(Drive* drive)
{
	Ctlr* ctlr;
	int cmdport, len;

	ctlr = drive->ctlr;
	cmdport = ctlr->cmdport;
	switch(inb(cmdport+Ir) & (/*Rel|*/Io|Cd)){
	case Cd:
		outss(cmdport+Data, drive->pktcmd, drive->pkt/2);
		break;

	case 0:
		if(drive->pktdma)
			goto Pktdma;
		len = (inb(cmdport+Bytehi)<<8)|inb(cmdport+Bytelo);
		if(drive->data+len > drive->limit){
			atanop(drive, 0);
			break;
		}
		outss(cmdport+Data, drive->data, len/2);
		drive->data += len;
		break;

	case Io:
		if(drive->pktdma)
			goto Pktdma;
		len = (inb(cmdport+Bytehi)<<8)|inb(cmdport+Bytelo);
		if(drive->data+len > drive->limit){
			atanop(drive, 0);
			break;
		}
		inss(cmdport+Data, drive->data, len/2);
		drive->data += len;
		break;

	case Io|Cd:
		if(drive->pktdma){
	Pktdma:
			atadmainterrupt(drive, drive->dlen);
		} else
			ctlr->done = 1;
		break;
	}
}

static int
atapktio0(Drive *drive, SDreq *r)
{
	uchar *cmd;
	int as, len, cmdport, ctlport, rv;
	Ctlr *ctlr;

	rv = SDok;
	cmd = r->cmd;
	drive->command = Cpkt;
	memmove(drive->pktcmd, cmd, r->clen);
	memset(drive->pktcmd+r->clen, 0, drive->pkt-r->clen);
	drive->limit = drive->data+drive->dlen;

	ctlr = drive->ctlr;
	cmdport = ctlr->cmdport;
	ctlport = ctlr->ctlport;

	as = ataready(cmdport, ctlport, drive->dev, Bsy|Drq, 0, 107*1000);
	/* used to test as&Chk as failure too, but some CD readers use that for media change */
	if(as < 0)
		return SDnostatus;

	ilock(ctlr);
	if(drive->dlen && drive->dmactl && !atadmasetup(drive, drive->dlen))
		drive->pktdma = Dma;
	else
		drive->pktdma = 0;
	len = drive->secsize > 0 ? 16*drive->secsize : 0x8000;
	outb(cmdport+Features, drive->pktdma);
	outb(cmdport+Count, 0);
	outb(cmdport+Sector, 0);
	outb(cmdport+Bytelo, len);
	outb(cmdport+Bytehi, len>>8);
	outb(cmdport+Dh, drive->dev);
	ctlr->done = 0;
	ctlr->curdrive = drive;
	ctlr->command = Cpkt;		/* debugging */
	outb(cmdport+Command, Cpkt);

	microdelay(1);
	as = ataready(cmdport, ctlport, 0, Bsy, Drq|Chk, 400*1000);
	if(as < 0 || (as & (Bsy|Chk))){
		drive->status = as<0 ? 0 : as;
		ctlr->curdrive = nil;
		ctlr->done = 1;
		rv = SDtimeout;
	}else
		atapktinterrupt(drive);
	if(drive->pktdma)
		atadmastart(ctlr, drive->write);
	iunlock(ctlr);

	while(iowait(drive, 30*1000, 1) == 0)
		;

	ilock(ctlr);
	if(!ctlr->done){
		rv = SDcheck;
		ataabort(drive, 0);
	}
	if(drive->error){
		if(drive->pktdma)
			atadmastop(ctlr);
		drive->status |= Chk;
		ctlr->curdrive = nil;
	}
	iunlock(ctlr);

	if(rv != SDcheck && drive->status & Chk){
		rv = SDcheck;
		if(drive->pktdma){
			print("atapktio: disabling dma\n");
			drive->dmactl = 0;
		}
	}
	return rv;
}

static int
atapktio(Drive* drive, SDreq *r)
{
	int n;
	Ctlr *ctlr;

	ctlr = drive->ctlr;
	qlock(ctlr);
	n = atapktio0(drive, r);
	qunlock(ctlr);
	return n;
}

static uchar cmd48[256] = {
	[Crs]	Crs48,
	[Crd]	Crd48,
	[Crsm]	Crsm48,
	[Cws]	Cws48,
	[Cwd]	Cwd48,
	[Cwsm]	Cwsm48,
};

enum{
	Last28	= (1<<28) - 1,
};

static int
atageniostart(Drive* drive, uvlong lba)
{
	Ctlr *ctlr;
	uchar cmd;
	int as, c, cmdport, ctlport, h, len, s, use48;

	use48 = 0;
	if((drive->flags&Lba48always) || (lba+drive->count) > Last28 || drive->count > 256){
		if((drive->feat & Dllba) == 0)
			return -1;
		use48 = 1;
		c = h = s = 0;
	}else if(drive->dev & Lba){
		c = (lba>>8) & 0xFFFF;
		h = (lba>>24) & 0x0F;
		s = lba & 0xFF;
	}else{
		if (drive->s == 0 || drive->h == 0){
			print("sdide: chs address botch");
			return -1;
		}
		c = lba/(drive->s*drive->h);
		h = (lba/drive->s) % drive->h;
		s = (lba % drive->s) + 1;
	}

	ctlr = drive->ctlr;
	cmdport = ctlr->cmdport;
	ctlport = ctlr->ctlport;
	if(ataready(cmdport, ctlport, drive->dev, Bsy|Drq, Drdy, 101*1000) < 0)
		return -1;

	ilock(ctlr);
	if(drive->dmactl && !atadmasetup(drive, drive->count*drive->secsize)){
		if(drive->write)
			drive->command = Cwd;
		else
			drive->command = Crd;
	}
	else if(drive->rwmctl){
		drive->block = drive->rwm*drive->secsize;
		if(drive->write)
			drive->command = Cwsm;
		else
			drive->command = Crsm;
	}
	else{
		drive->block = drive->secsize;
		if(drive->write)
			drive->command = Cws;
		else
			drive->command = Crs;
	}
	drive->limit = drive->data + drive->count*drive->secsize;
	cmd = drive->command;
	if(use48){
		outb(cmdport+Count, drive->count>>8);
		outb(cmdport+Count, drive->count);
		outb(cmdport+Lbalo, lba>>24);
		outb(cmdport+Lbalo, lba);
		outb(cmdport+Lbamid, lba>>32);
		outb(cmdport+Lbamid, lba>>8);
		outb(cmdport+Lbahi, lba>>40);
		outb(cmdport+Lbahi, lba>>16);
		outb(cmdport+Dh, drive->dev|Lba);
		cmd = cmd48[cmd];

		if(DEBUG & Dbg48BIT)
			print("using 48-bit commands\n");
	}else{
		outb(cmdport+Count, drive->count);
		outb(cmdport+Sector, s);
		outb(cmdport+Cyllo, c);
		outb(cmdport+Cylhi, c>>8);
		outb(cmdport+Dh, drive->dev|h);
	}
	ctlr->done = 0;
	ctlr->curdrive = drive;
	ctlr->command = drive->command;	/* debugging */
	outb(cmdport+Command, cmd);

	switch(drive->command){
	case Cws:
	case Cwsm:
		microdelay(1);
		as = ataready(cmdport, ctlport, 0, Bsy, Drq|Err, 400*1000);
		if(as < 0 || (as & Err)){
			iunlock(ctlr);
			return -1;
		}
		len = drive->block;
		if(drive->data+len > drive->limit)
			len = drive->limit-drive->data;
		outss(cmdport+Data, drive->data, len/2);
		break;

	case Crd:
	case Cwd:
		atadmastart(ctlr, drive->write);
		break;
	}
	iunlock(ctlr);

	return 0;
}

static int
atagenioretry(Drive* drive, SDreq *r, uvlong lba, int count)
{
	char *s;
	int rv, count0, rw;
	uvlong lba0;

	if(drive->dmactl){
		drive->dmactl = 0;
		s = "disabling dma";
		rv = SDretry;
	}else if(drive->rwmctl){
		drive->rwmctl = 0;
		s = "disabling rwm";
		rv = SDretry;
	}else{
		s = "nondma";
		rv = sdsetsense(r, SDcheck, 4, 8, drive->error);
	}
	sdfakescsirw(r, &lba0, &count0, &rw);
	print("atagenioretry: %s %c:%llud:%d @%llud:%d\n",
		s, "rw"[rw], lba0, count0, lba, count);
	return rv;
}

static int
atagenio(Drive* drive, SDreq *r)
{
	Ctlr *ctlr;
	uvlong lba;
	int i, rw, count, maxio;

	if((i = sdfakescsi(r)) != SDnostatus)
		return i;
	if((i = sdfakescsirw(r, &lba, &count, &rw)) != SDnostatus)
		return i;
	ctlr = drive->ctlr;
	if(drive->data == nil)
		return SDok;
	if(drive->dlen < count*drive->secsize)
		count = drive->dlen/drive->secsize;
	qlock(ctlr);
	if(ctlr->maxio)
		maxio = ctlr->maxio;
	else if(drive->feat & Dllba)
		maxio = 65536;
	else
		maxio = 256;
	while(count){
		if(count > maxio)
			drive->count = maxio;
		else
			drive->count = count;
		if(atageniostart(drive, lba)){
			ilock(ctlr);
			atanop(drive, 0);
			iunlock(ctlr);
			qunlock(ctlr);
			return atagenioretry(drive, r, lba, count);
		}
		iowait(drive, 30*1000, 0);
		if(!ctlr->done){
			/*
			 * What should the above timeout be? In
			 * standby and sleep modes it could take as
			 * long as 30 seconds for a drive to respond.
			 * Very hard to get out of this cleanly.
			 */
			atadumpstate(drive, r, lba, count);
			ataabort(drive, 1);
			qunlock(ctlr);
			return atagenioretry(drive, r, lba, count);
		}

		if(drive->status & Err){
			qunlock(ctlr);
			print("atagenio: %llud:%d\n", lba, drive->count);
			return sdsetsense(r, SDcheck, 4, 8, drive->error);
		}
		count -= drive->count;
		lba += drive->count;
	}
	qunlock(ctlr);

	return SDok;
}

static int
atario(SDreq* r)
{
	uchar *p;
	int status;
	Ctlr *ctlr;
	Drive *drive;
	SDunit *unit;

	unit = r->unit;
	if((ctlr = unit->dev->ctlr) == nil || ctlr->drive[unit->subno] == nil)
		return r->status = SDtimeout;
	drive = ctlr->drive[unit->subno];
	qlock(drive);
	for(;;){
		drive->write = r->write;
		drive->data = r->data;
		drive->dlen = r->dlen;
		drive->status = 0;
		drive->error = 0;
		if(drive->pkt)
			status = atapktio(drive, r);
		else
			status = atagenio(drive, r);
		if(status != SDretry)
			break;
		if(DbgDEBUG)
			print("%s: retry: dma %8.8uX rwm %4.4uX\n",
				unit->name, drive->dmactl, drive->rwmctl);
	}
	if(status == SDok && r->rlen == 0 && (r->flags & SDvalidsense) == 0){
		sdsetsense(r, SDok, 0, 0, 0);
		if(drive->data){
			p = r->data;
			r->rlen = drive->data - p;
		}
		else
			r->rlen = 0;
	}
	qunlock(drive);
	return status;
}

/**/
static int
isdmacmd(Drive *d, SDreq *r)
{
	switch(r->ataproto & Pprotom){
	default:
		return 0;
	case Pdmq:
		error("no queued support");
	case Pdma:
		if(!(d->dmactl || d->rwmctl))
			error("dma in non dma mode\n");
		return 1;
	}
}

static int
atagenatastart(Drive* d, SDreq *r)
{
	uchar u;
	int as, cmdport, ctlport, len, pr, isdma;
	Ctlr *ctlr;

	isdma = isdmacmd(d, r);
	ctlr = d->ctlr;
	cmdport = ctlr->cmdport;
	ctlport = ctlr->ctlport;
	if(ataready(cmdport, ctlport, d->dev, Bsy|Drq, d->pkt ? 0 : Drdy, 101*1000) < 0)
		return -1;

	ilock(ctlr);
	if(isdma && atadmasetup(d, d->block)){
		iunlock(ctlr);
		return -1;
	
	}
	if(d->feat & Dllba && (r->ataproto & P28) == 0){
		outb(cmdport+Features, r->cmd[Ffeat8]);
		outb(cmdport+Features, r->cmd[Ffeat]);
		outb(cmdport+Count, r->cmd[Fsc8]);
		outb(cmdport+Count, r->cmd[Fsc]);
		outb(cmdport+Lbalo, r->cmd[Flba24]);
		outb(cmdport+Lbalo, r->cmd[Flba0]);
		outb(cmdport+Lbamid, r->cmd[Flba32]);
		outb(cmdport+Lbamid, r->cmd[Flba8]);
		outb(cmdport+Lbahi, r->cmd[Flba40]);
		outb(cmdport+Lbahi, r->cmd[Flba16]);
		u = r->cmd[Fdev] & ~0xb0;
		outb(cmdport+Dh, d->dev|u);
	}else{
		outb(cmdport+Features, r->cmd[Ffeat]);
		outb(cmdport+Count, r->cmd[Fsc]);
		outb(cmdport+Lbalo, r->cmd[Flba0]);
		outb(cmdport+Lbamid, r->cmd[Flba8]);
		outb(cmdport+Lbahi, r->cmd[Flba16]);
		u = r->cmd[Fdev] & ~0xb0;
		outb(cmdport+Dh, d->dev|u);
	}
	ctlr->done = 0;
	ctlr->curdrive = d;
	d->command = r->ataproto & (Pprotom|Pdatam);
	ctlr->command = r->cmd[Fcmd];
	outb(cmdport+Command, r->cmd[Fcmd]);

	pr = r->ataproto & Pprotom;
	if(pr == Pnd || pr == Preset)
		USED(d);
	else if(!isdma){
		microdelay(1);
		as = ataready(cmdport, ctlport, 0, Bsy, Drq|Err, 400*1000);
		if(as < 0 || (as & Err)){
			iunlock(ctlr);
			return -1;
		}
		len = d->block;
		if(r->write && len > 0)
			outss(cmdport+Data, d->data, len/2);
	}else
		atadmastart(ctlr, d->write);
	iunlock(ctlr);
	return 0;
}

static void
mkrfis(Drive *d, SDreq *r)
{
	uchar *u;
	int cmdport;
	Ctlr *ctlr;

	ctlr = d->ctlr;
	cmdport = ctlr->cmdport;
	u = r->cmd;

	ilock(ctlr);
	u[Ftype] = 0x34;
	u[Fioport] = 0;
	if((d->feat & Dllba) && (r->ataproto & P28) == 0){
		u[Frerror] = inb(cmdport+Error);
		u[Fsc8] = inb(cmdport+Count);
		u[Fsc] = inb(cmdport+Count);
		u[Flba24] = inb(cmdport+Lbalo);
		u[Flba0] = inb(cmdport+Lbalo);
		u[Flba32] = inb(cmdport+Lbamid);
		u[Flba8] = inb(cmdport+Lbamid);
		u[Flba40] = inb(cmdport+Lbahi);
		u[Flba16] = inb(cmdport+Lbahi);
		u[Fdev] = inb(cmdport+Dh);
		u[Fstatus] = inb(cmdport+Status);
	}else{
		u[Frerror] = inb(cmdport+Error);
		u[Fsc] = inb(cmdport+Count);
		u[Flba0] = inb(cmdport+Lbalo);
		u[Flba8] = inb(cmdport+Lbamid);
		u[Flba16] = inb(cmdport+Lbahi);
		u[Fdev] = inb(cmdport+Dh);
		u[Fstatus] = inb(cmdport+Status);
	}
	iunlock(ctlr);
}

static int
atarstdone(Drive *d)
{
	int as;
	Ctlr *c;

	c = d->ctlr;
	as = ataready(c->cmdport, c->ctlport, 0, Bsy|Drq, 0, 5*1000);
	c->done = as >= 0;
	return c->done;
}

static uint
cmdss(Drive *d, SDreq *r)
{
	switch(r->cmd[Fcmd]){
	case Cid:
	case Cidpkt:
		return 512;
	default:
		return d->secsize;
	}
}

/*
 * various checks.  we should be craftier and
 * avoid figuring out how big stuff is supposed to be.
 */
static uint
patasizeck(Drive *d, SDreq *r)
{
	uint count, maxio, secsize;
	Ctlr *ctlr;

	secsize = cmdss(d, r);		/* BOTCH */
	if(secsize == 0)
		error(Eio);
	count = r->dlen / secsize;
	ctlr = d->ctlr;
	if(ctlr->maxio)
		maxio = ctlr->maxio;
	else if((d->feat & Dllba) && (r->ataproto & P28) == 0)
		maxio = 65536;
	else
		maxio = 256;
	if(count > maxio){
		uprint("i/o too large, lim %d", maxio);
		error(up->genbuf);
	}
	if(r->ataproto&Ppio && count > 1)
		error("invalid # of sectors");
	return count;
}

static int
atapataio(Drive *d, SDreq *r)
{
	int rv;
	Ctlr *ctlr;

	d->count = 0;
	if(r->ataproto & Pdatam)
		d->count = patasizeck(d, r);
	d->block = r->dlen;
	d->limit = d->data + r->dlen;

	ctlr = d->ctlr;
	qlock(ctlr);
	if(waserror()){
		qunlock(ctlr);
		nexterror();
	}
	rv = atagenatastart(d, r);
	poperror();
	if(rv){
		if(DEBUG & DbgAtazz)
			print("sdide: !atageatastart\n");
		ilock(ctlr);
		atanop(d, 0);
		iunlock(ctlr);
		qunlock(ctlr);
		return sdsetsense(r, SDcheck, 4, 8, d->error);
	}

	if((r->ataproto & Pprotom) == Preset)
		atarstdone(d);
	else
		while(iowait(d, 30*1000, 1) == 0)
			;
	if(!ctlr->done){
		if(DEBUG & DbgAtazz){
			print("sdide: !done\n");
			atadumpstate(d, r, 0, d->count);
		}
		ataabort(d, 1);
		qunlock(ctlr);
		return sdsetsense(r, SDcheck, 11, 0, 6);	/* aborted; i/o process terminated */
	}
	mkrfis(d, r);
	if(d->status & Err){
		if(DEBUG & DbgAtazz)
			print("sdide: status&Err\n");
		qunlock(ctlr);
		return sdsetsense(r, SDcheck, 4, 8, d->error);
	}
	qunlock(ctlr);
	return SDok;
}

static int
ataataio0(Drive *d, SDreq *r)
{
	int i;

	if((r->ataproto & Pprotom) == Ppkt){
		if(r->clen > d->pkt)
			error(Eio);
		qlock(d->ctlr);
		i = atapktio0(d, r);
		d->block = d->data - (uchar*)r->data;
		mkrfis(d, r);
		qunlock(d->ctlr);
		return i;
	}else
		return atapataio(d, r);
}

/*
 * hack to allow udma mode to be set or unset
 * via direct ata command.  it would be better
 * to move the assumptions about dma mode out
 * of some of the helper functions.
 */
static int
isudm(SDreq *r)
{
	uchar *c;

	c = r->cmd;
	if(c[Fcmd] == 0xef && c[Ffeat] == 0x03){
		if(c[Fsc]&0x40)
			return 1;
		return -1;
	}
	return 0;
}

static int
fisreqchk(Sfis *f, SDreq *r)
{
	if((r->ataproto & Pprotom) == Ppkt)
		return SDnostatus;
	/*
	 * handle oob requests;
	 *    restrict & sanitize commands
	 */
	if(r->clen != 16)
		error(Eio);
	if(r->cmd[0] == 0xf0){
		sigtofis(f, r->cmd);
		r->status = SDok;
		return SDok;
	}
	r->cmd[0] = 0x27;
	r->cmd[1] = 0x80;
	r->cmd[7] |= 0xa0;
	return SDnostatus;
}

static int
ataataio(SDreq *r)
{
	int status, udm;
	Ctlr *c;
	Drive *d;
	SDunit *u;

	u = r->unit;
	if((c = u->dev->ctlr) == nil || (d = c->drive[u->subno]) == nil){
		r->status = SDtimeout;
		return SDtimeout;
	}
	if((status = fisreqchk(d, r)) != SDnostatus)
		return status;
	udm = isudm(r);

	qlock(d);
	if(waserror()){
		qunlock(d);
		nexterror();
	}
retry:
	d->write = r->write;
	d->data = r->data;
	d->dlen = r->dlen;
	d->status = 0;
	d->error = 0;

	switch(status = ataataio0(d, r)){
	case SDretry:
		if(DbgDEBUG)
			print("%s: retry: dma %.8ux rwm %.4ux\n",
				u->name, d->dmactl, d->rwmctl);
		goto retry;
	case SDok:
		if(udm == 1)
			d->dmactl = d->dma;
		else if(udm == -1)
			d->dmactl = 0;
		sdsetsense(r, SDok, 0, 0, 0);
		r->rlen = d->block;
		break;
	}
	poperror();
	qunlock(d);
	r->status = status;
	return status;
}
/**/

static void
ichirqack(Ctlr *ctlr)
{
	int bmiba;

	if(bmiba = ctlr->bmiba)
		outb(bmiba+Bmisx, inb(bmiba+Bmisx));
}

static void
atainterrupt(Ureg*, void* arg)
{
	Ctlr *ctlr;
	Drive *drive;
	int cmdport, len, status;

	ctlr = arg;

	ilock(ctlr);
	ctlr->nrq++;
	if(ctlr->curdrive)
		ctlr->curdrive->irq++;
	if(inb(ctlr->ctlport+As) & Bsy){
		ctlr->bsy++;
		if(ctlr->curdrive)
			ctlr->curdrive->bsy++;
		iunlock(ctlr);
		if(DEBUG & DbgBsy)
			print("IBsy+");
		return;
	}
	cmdport = ctlr->cmdport;
	status = inb(cmdport+Status);
	if((drive = ctlr->curdrive) == nil){
		ctlr->nildrive++;
		if(ctlr->irqack != nil)
			ctlr->irqack(ctlr);
		iunlock(ctlr);
		return;
	}
	if(status & Err)
		drive->error = inb(cmdport+Error);
	else switch(drive->command){
	default:
		drive->error = Abrt;
		break;

	case Crs:
	case Crsm:
	case Ppio|Pin:
		if(!(status & Drq)){
			drive->error = Abrt;
			break;
		}
		len = drive->block;
		if(drive->data+len > drive->limit)
			len = drive->limit-drive->data;
		inss(cmdport+Data, drive->data, len/2);
		drive->data += len;
		if(drive->data >= drive->limit)
			ctlr->done = 1;
		break;

	case Cws:
	case Cwsm:
	case Ppio|Pout:
		len = drive->block;
		if(drive->data+len > drive->limit)
			len = drive->limit-drive->data;
		drive->data += len;
		if(drive->data >= drive->limit){
			ctlr->done = 1;
			break;
		}
		if(!(status & Drq)){
			drive->error = Abrt;
			break;
		}
		len = drive->block;
		if(drive->data+len > drive->limit)
			len = drive->limit-drive->data;
		outss(cmdport+Data, drive->data, len/2);
		break;

	case Cpkt:
	case Ppkt|Pin:
	case Ppkt|Pout:
		atapktinterrupt(drive);
		break;

	case Crd:
	case Cwd:
	case Pdma|Pin:
	case Pdma|Pout:
		atadmainterrupt(drive, drive->count*drive->secsize);
		break;

	case Pnd:
	case Preset:
		ctlr->done = 1;
		break;
	}
	if(ctlr->irqack != nil)
		ctlr->irqack(ctlr);
	iunlock(ctlr);

	if(drive->error){
		status |= Err;
		ctlr->done = 1;
	}

	if(ctlr->done){
		ctlr->curdrive = nil;
		drive->status = status;
		wakeup(ctlr);
	}
}

typedef struct Lchan Lchan;
struct Lchan {
	int	cmdport;
	int	ctlport;
	int	irq;
	int	probed;
};
static Lchan lchan[2] = {
	0x1f0,	0x3f4,	IrqATA0,	0,
	0x170,	0x374,	IrqATA1,	0,
};

static int
badccru(Pcidev *p)
{
	switch(p->did<<16 | p->did){
	case 0x439c<<16 | 0x1002:
	case 0x438c<<16 | 0x1002:
		print("%T: allowing bad ccru %.2ux for suspected ide controller\n",
			p->tbdf, p->ccru);
		return 1;
	default:
		return 0;
	}
}

static SDev*
atapnp(void)
{
	char *s;
	int channel, map, ispc87415, maxio, pi, r, span, maxdma, tbdf;
	Ctlr *ctlr;
	Pcidev *p;
	SDev *sdev, *head, *tail;
	void (*irqack)(Ctlr*);

	head = tail = nil;
	for(p = nil; p = pcimatch(p, 0, 0); ){
		/*
		 * Look for devices with the correct class and sub-class
		 * code and known device and vendor ID; add native-mode
		 * channels to the list to be probed, save info for the
		 * compatibility mode channels.
		 * Note that the legacy devices should not be considered
		 * PCI devices by the interrupt controller.
		 * For both native and legacy, save info for busmastering
		 * if capable.
		 * Promise Ultra ATA/66 (PDC20262) appears to
		 * 1) give a sub-class of 'other mass storage controller'
		 *    instead of 'IDE controller', regardless of whether it's
		 *    the only controller or not;
		 * 2) put 0 in the programming interface byte (probably
		 *    as a consequence of 1) above).
		 * Sub-class code 0x04 is 'RAID controller', e.g. VIA VT8237.
		 */
		if(p->ccrb != 0x01)
			continue;
		if(!badccru(p))
		if(p->ccru != 0x01 && p->ccru != 0x04 && p->ccru != 0x80)
			continue;
		pi = p->ccrp;
		map = 3;
		ispc87415 = 0;
		maxdma = 0;
		maxio = 0;
		if(s = getconf("*idemaxio"))
			maxio = atoi(s);
		span = BMspan;
		irqack = nil;

		switch((p->did<<16)|p->vid){
		default:
			continue;

		case (0x0002<<16)|0x100B:	/* NS PC87415 */
			/*
			 * Disable interrupts on both channels until
			 * after they are probed for drives.
			 * This must be called before interrupts are
			 * enabled because the IRQ may be shared.
			 */
			ispc87415 = 1;
			pcicfgw32(p, 0x40, 0x00000300);
			break;
		case (0x1000<<16)|0x1042:	/* PC-Tech RZ1000 */
			/*
			 * Turn off prefetch. Overkill, but cheap.
			 */
			r = pcicfgr32(p, 0x40);
			r &= ~0x2000;
			pcicfgw32(p, 0x40, r);
			break;
		case (0x4D38<<16)|0x105A:	/* Promise PDC20262 */
		case (0x4D30<<16)|0x105A:	/* Promise PDC202xx */
		case (0x4D68<<16)|0x105A:	/* Promise PDC20268 */
		case (0x4D69<<16)|0x105A:	/* Promise Ultra/133 TX2 */
		case (0x3373<<16)|0x105A:	/* Promise 20378 RAID */
		case (0x3149<<16)|0x1106:	/* VIA VT8237 SATA/RAID */
		case (0x0415<<16)|0x1106:	/* VIA VT6415 PATA IDE */
		case (0x3112<<16)|0x1095:	/* SiL 3112 SATA/RAID */
			maxio = 15;
			span = 8*1024;
			/*FALLTHROUGH*/
		case (0x3114<<16)|0x1095:	/* SiL 3114 SATA/RAID */
		case (0x0680<<16)|0x1095:	/* SiI 0680/680A PATA133 ATAPI/RAID */
			pi = 0x85;
			break;
		case (0x0004<<16)|0x1103:	/* HighPoint HPT366 */
			pi = 0x85;
			/*
			 * Turn off fast interrupt prediction.
			 */
			if((r = pcicfgr8(p, 0x51)) & 0x80)
				pcicfgw8(p, 0x51, r & ~0x80);
			if((r = pcicfgr8(p, 0x55)) & 0x80)
				pcicfgw8(p, 0x55, r & ~0x80);
			break;
		case (0x0640<<16)|0x1095:	/* CMD 640B */
			/*
			 * Bugfix code here...
			 */
			break;
		case (0x7441<<16)|0x1022:	/* AMD 768 */
		case (0x7800<<16)|0x1022:
			/*
			 * Set:
			 *	0x41	prefetch, postwrite;
			 *	0x43	FIFO configuration 1/2 and 1/2;
			 *	0x44	status register read retry;
			 *	0x46	DMA read and end of sector flush.
			 */
			r = pcicfgr8(p, 0x41);
			pcicfgw8(p, 0x41, r|0xF0);
			r = pcicfgr8(p, 0x43);
			pcicfgw8(p, 0x43, (r & 0x90)|0x2A);
			r = pcicfgr8(p, 0x44);
			pcicfgw8(p, 0x44, r|0x08);
			r = pcicfgr8(p, 0x46);
			pcicfgw8(p, 0x46, (r & 0x0C)|0xF0);
			/*FALLTHROUGH*/
		case (0x01BC<<16)|0x10DE:	/* nVidia nForce1 */
		case (0x0065<<16)|0x10DE:	/* nVidia nForce2 */
		case (0x0085<<16)|0x10DE:	/* nVidia nForce2 MCP */
		case (0x00E3<<16)|0x10DE:	/* nVidia nForce2 250 SATA */
		case (0x00D5<<16)|0x10DE:	/* nVidia nForce3 */
		case (0x00E5<<16)|0x10DE:	/* nVidia nForce3 Pro */
		case (0x00EE<<16)|0x10DE:	/* nVidia nForce3 250 SATA */
		case (0x0035<<16)|0x10DE:	/* nVidia nForce3 MCP */
		case (0x0053<<16)|0x10DE:	/* nVidia nForce4 */
		case (0x0054<<16)|0x10DE:	/* nVidia nForce4 SATA */
		case (0x0055<<16)|0x10DE:	/* nVidia nForce4 SATA */
		case (0x0266<<16)|0x10DE:	/* nVidia nForce4 430 SATA */
		case (0x0265<<16)|0x10DE:	/* nVidia nForce 51 MCP */
		case (0x0267<<16)|0x10DE:	/* nVidia nForce 55 MCP SATA */
		case (0x037f<<16)|0x10DE:	/* nVidia nForce 55 MCP SATA */
		case (0x03ec<<16)|0x10DE:	/* nVidia nForce 61 MCP SATA */
		case (0x03f6<<16)|0x10DE:	/* nVidia nForce 61 MCP PATA */
		case (0x0448<<16)|0x10DE:	/* nVidia nForce 65 MCP SATA */
		case (0x0560<<16)|0x10DE:	/* nVidia nForce 69 MCP SATA */
			/*
			 * Ditto, although it may have a different base
			 * address for the registers (0x50?).
			 */
			/*FALLTHROUGH*/
		case (0x209A<<16)|0x1022:	/* AMD CS5536 */
		case (0x7401<<16)|0x1022:	/* AMD 755 Cobra */
		case (0x7409<<16)|0x1022:	/* AMD 756 Viper */
		case (0x7410<<16)|0x1022:	/* AMD 766 Viper Plus */
		case (0x7469<<16)|0x1022:	/* AMD 3111 */
		case (0x4376<<16)|0x1002:	/* SB4xx pata */
		case (0x4379<<16)|0x1002:	/* SB4xx sata */
		case (0x437a<<16)|0x1002:	/* SB4xx sata ctlr #2 */
		case (0x437c<<16)|0x1002:	/* Rx6xx pata */
		case (0x438c<<16)|0x1002:	/* ATI SB600 PATA */
		case (0x439c<<16)|0x1002:	/* SB7xx pata */
			break;

		case (0x6101<<16)|0x11ab:	/* Marvell PATA */
		case (0x6121<<16)|0x11ab:	/* Marvell PATA */
		case (0x6123<<16)|0x11ab:	/* Marvell PATA */
		case (0x6145<<16)|0x11ab:	/* Marvell PATA */
		case (0x1b4b<<16)|0x91a0:	/* Marvell PATA */
		case (0x1b4b<<16)|0x91a4:	/* Marvell PATA */
			break;

		case (0x0211<<16)|0x1166:	/* ServerWorks IB6566 */
			{
				Pcidev *sb;

				sb = pcimatch(nil, 0x1166, 0x0200);
				if(sb == nil)
					break;
				r = pcicfgr32(sb, 0x64);
				r &= ~0x2000;
				pcicfgw32(sb, 0x64, r);
			}
			span = 32*1024;
			break;
		case (0x5229<<16)|0x10B9:	/* ALi M1543 */
		case (0x5288<<16)|0x10B9:	/* ALi M5288 SATA */
			/*FALLTHROUGH*/
		case (0x5513<<16)|0x1039:	/* SiS 962 */
		case (0x0646<<16)|0x1095:	/* CMD 646 */
		case (0x0571<<16)|0x1106:	/* VIA 82C686 */
		case (0x9001<<16)|0x1106:	/* VIA chipset in VIA PV530 */
		case (0x0502<<16)|0x100b:	/* National Semiconductor SC1100/SCx200 */
			break;
		case (0x2360<<16)|0x197b:	/* jmicron jmb360 */
		case (0x2361<<16)|0x197b:	/* jmicron jmb361 */
		case (0x2363<<16)|0x197b:	/* jmicron jmb363 */
		case (0x2365<<16)|0x197b:	/* jmicron jmb365 */
		case (0x2366<<16)|0x197b:	/* jmicron jmb366 */
		case (0x2368<<16)|0x197b:	/* jmicron jmb368 */
			break;
		case (0x7010<<16)|0x8086:	/* 82371SB (PIIX3) */
		case (0x1230<<16)|0x8086:	/* 82371FB (PIIX) */
		case (0x7111<<16)|0x8086:	/* 82371[AE]B (PIIX4[E]) */
			maxdma = 0x20000;
			break;
		case (0x1c00<<16)|0x8086:	/* SERIES6 SATA */
		case (0x1c01<<16)|0x8086:	/* SERIES6 SATA */
		case (0x1c08<<16)|0x8086:	/* SERIES6 SATA */
		case (0x1c09<<16)|0x8086:	/* SERIES6 SATA */
		case (0x2411<<16)|0x8086:	/* 82801AA (ICH) */
		case (0x2421<<16)|0x8086:	/* 82801AB (ICH0) */
		case (0x244A<<16)|0x8086:	/* 82801BA (ICH2, Mobile) */
		case (0x244B<<16)|0x8086:	/* 82801BA (ICH2, High-End) */
		case (0x248A<<16)|0x8086:	/* 82801CA (ICH3, Mobile) */
		case (0x248B<<16)|0x8086:	/* 82801CA (ICH3, High-End) */
		case (0x24CA<<16)|0x8086:	/* 82801DBM (ICH4, Mobile) */
		case (0x24CB<<16)|0x8086:	/* 82801DB (ICH4, High-End) */
		case (0x24D1<<16)|0x8086:	/* 82801er (ich5) */
		case (0x24DB<<16)|0x8086:	/* 82801EB (ICH5) */
		case (0x25A2<<16)|0x8086:	/* 6300ESB pata */
		case (0x25A3<<16)|0x8086:	/* 6300ESB (E7210) */
		case (0x266F<<16)|0x8086:	/* 82801FB (ICH6) */
		case (0x2651<<16)|0x8086:	/* 82801FB (ICH6) */
		case (0x2653<<16)|0x8086:	/* 82801FBM (ICH6, Mobile) */
		case (0x269e<<16)|0x8086:	/* 63xxESB (intel 5000) */
		case (0x27DF<<16)|0x8086:	/* 82801G PATA (ICH7) */
		case (0x27C0<<16)|0x8086:	/* 82801GB SATA (ICH7) */
		case (0x27C4<<16)|0x8086:	/* 82801GBM SATA (ICH7) */
		case (0x27C5<<16)|0x8086:	/* 82801GBM SATA AHCI (ICH7) */
		case (0x2850<<16)|0x8086:	/* 82801HBM/HEM PATA */
		case (0x2820<<16)|0x8086:	/* 82801HB/HR/HH/HO SATA IDE */
		case (0x2825<<16)|0x8086:	/* 82801IIH Intel Q35 IDE */
		case (0x2828<<16)|0x8086:	/* 82801HBM SATA (ICH8-M) */
		case (0x2829<<16)|0x8086:	/* 82801HBM SATA AHCI (ICH8-M) */
		case (0x2920<<16)|0x8086:	/* 82801(IB)/IR/IH/IO SATA (ICH9) port 0-3 */
		case (0x2921<<16)|0x8086:	/* 82801(IB)/IR/IH/IO SATA (ICH9) port 0-1 */
		case (0x2926<<16)|0x8086:	/* 82801(IB)/IR/IH/IO SATA (ICH9) port 4-5 */
		case (0x2928<<16)|0x8086:	/* 82801(IB)/IR/IH/IO SATA (ICH9m) port 0-1 */
		case (0x2929<<16)|0x8086:	/* 82801(IB)/IR/IH/IO SATA (ICH9m) port 0-1, 4-5 */
		case (0x292d<<16)|0x8086:	/* 82801(IB)/IR/IH/IO SATA (ICH9m) port 4-5*/
		case (0x3a20<<16)|0x8086:	/* 82801ji (ich10) */
		case (0x3a26<<16)|0x8086:	/* 82801ji (ich10) */
		case (0x3b20<<16)|0x8086:	/* 34x0 (pch) port 0-3 */
		case (0x3b21<<16)|0x8086:	/* 34x0 (pch) port 4-5 */
		case (0x3b28<<16)|0x8086:	/* 34x0pm (pch) port 0-1, 4-5 */
		case (0x3b2e<<16)|0x8086:	/* 34x0pm (pch) port 0-3 */
			map = 0;
			if(pcicfgr16(p, 0x40) & 0x8000)
				map |= 1;
			if(pcicfgr16(p, 0x42) & 0x8000)
				map |= 2;
			irqack = ichirqack;
			break;
		case (0x811a<<16)|0x8086:	/* Intel SCH (Poulsbo) */
			map = 1;
			irqack = ichirqack;
			break;
		}
		for(channel = 0; channel < 2; channel++){
			if((map & 1<<channel) == 0)
				continue;
			if(pi & 1<<2*channel){
				sdev = ataprobe(p->mem[0+2*channel].bar & ~0x01,
						p->mem[1+2*channel].bar & ~0x01,
						p->intl, 3);
				tbdf = p->tbdf;
			}
			else if(lchan[channel].probed == 0){
				sdev = ataprobe(lchan[channel].cmdport,
					lchan[channel].ctlport, lchan[channel].irq, 3);
				lchan[channel].probed = 1;
				tbdf = BUSUNKNOWN;
			}
			else
				continue;
			if(sdev == nil)
				continue;
			ctlr = sdev->ctlr;
			if(ispc87415) {
				ctlr->ienable = pc87415ienable;
				print("pc87415disable: not yet implemented\n");
			}
			ctlr->tbdf = tbdf;
			ctlr->pcidev = p;
			ctlr->maxio = maxio;
			ctlr->maxdma = maxdma;
			ctlr->span = span;
			ctlr->irqack = irqack;
			if((pi & 0x80) && (p->mem[4].bar & 0x01))
				ctlr->bmiba = (p->mem[4].bar & ~0x01) + channel*8;
			if(head != nil)
				tail->next = sdev;
			else
				head = sdev;
			tail = sdev;
		}
	}

	if(lchan[0].probed + lchan[1].probed == 0)
		for(channel = 0; channel < 2; channel++){
			sdev = nil;
			if(lchan[channel].probed == 0){
	//			print("sdide: blind probe %.3ux\n", lchan[channel].cmdport);
				sdev = ataprobe(lchan[channel].cmdport,
					lchan[channel].ctlport, lchan[channel].irq, 3);
				lchan[channel].probed = 1;
			}
			if(sdev == nil)
				continue;
			if(head != nil)
				tail->next = sdev;
			else
				head = sdev;
			tail = sdev;
		}

if(0){
	int port;
	ISAConf isa;

	/*
	 * Hack for PCMCIA drives.
	 * This will be tidied once we figure out how the whole
	 * removeable device thing is going to work.
	 */
	memset(&isa, 0, sizeof(isa));
	isa.port = 0x180;		/* change this for your machine */
	isa.irq = 11;			/* change this for your machine */

	port = isa.port+0x0C;
	channel = pcmspecial("MK2001MPL", &isa);
	if(channel == -1)
		channel = pcmspecial("SunDisk", &isa);
	if(channel == -1){
		isa.irq = 10;
		channel = pcmspecial("CF", &isa);
	}
	if(channel == -1){
		isa.irq = 10;
		channel = pcmspecial("OLYMPUS", &isa);
	}
	if(channel == -1){
		port = isa.port+0x204;
		channel = pcmspecial("ATA/ATAPI", &isa);
	}
	if(channel >= 0 && (sdev = ataprobe(isa.port, port, isa.irq, 3)) != nil){
		if(head != nil)
			tail->next = sdev;
		else
			head = sdev;
	}
}
	return head;
}

static void
atadmaclr(Ctlr *ctlr)
{
	int bmiba, bmisx;

	if(ctlr->curdrive)
		ataabort(ctlr->curdrive, 1);
	bmiba = ctlr->bmiba;
	if(bmiba == 0)
		return;
	atadmastop(ctlr);
 	outl(bmiba+Bmidtpx, 0);
	bmisx = inb(bmiba+Bmisx) & ~Bmidea;
	outb(bmiba+Bmisx, bmisx|Ideints|Idedmae);
//	pciintst(ctlr->pcidev);
}

static int
ataenable(SDev* sdev)
{
	Ctlr *ctlr;
	char name[32];

	ctlr = sdev->ctlr;
	if(ctlr->bmiba){
		atadmaclr(ctlr);
		if(ctlr->pcidev != nil)
			pcisetbme(ctlr->pcidev);
		/* Intel SCH requires 8 byte alignment, though datasheet says 4 m( */
		ctlr->prdt = mallocalign(Nprd*sizeof(Prd), 8, 0, 64*1024);
	}
	snprint(name, sizeof(name), "%s (%s)", sdev->name, sdev->ifc->name);
	intrenable(ctlr->irq, atainterrupt, ctlr, ctlr->tbdf, name);
	outb(ctlr->ctlport+Dc, 0);
	if(ctlr->ienable)
		ctlr->ienable(ctlr);
	return 1;
}

static int
atadisable(SDev *sdev)
{
	Ctlr *ctlr;
	char name[32];

	ctlr = sdev->ctlr;
	outb(ctlr->ctlport+Dc, Nien);		/* disable interrupts */
	if (ctlr->idisable)
		ctlr->idisable(ctlr);
	snprint(name, sizeof(name), "%s (%s)", sdev->name, sdev->ifc->name);
	intrdisable(ctlr->irq, atainterrupt, ctlr, ctlr->tbdf, name);
	if(ctlr->bmiba) {
//		atadmaclr(ctlr);
		if (ctlr->pcidev)
			pciclrbme(ctlr->pcidev);
		free(ctlr->prdt);
		ctlr->prdt = nil;
	}
	return 0;
}

static int
ataonline(SDunit *unit)
{
	Drive *drive;
	Ctlr *ctlr;
	int ret;

	if((ctlr = unit->dev->ctlr) == nil || ctlr->drive[unit->subno] == nil)
		return 0;
	ret = 1;
	drive = ctlr->drive[unit->subno];
	if((drive->flags & Online) == 0){
		drive->flags |= Online;
		atadrive(unit, drive, ctlr->cmdport, ctlr->ctlport, drive->dev);
		ret = 2;
	}
	if(drive->feat & Datapi){
		ulong dma;

		dma = drive->dmactl;
		drive->dmactl = 0;
		ret = scsionline(unit);
		drive->dmactl = dma;
	} else {
		unit->sectors = drive->sectors;
		unit->secsize = drive->secsize;
	}
	return ret;
}

static int
atarctl(SDunit* unit, char* p, int l)
{
	Ctlr *ctlr;
	Drive *drive;
	char *e, *op;

	if((ctlr = unit->dev->ctlr) == nil || ctlr->drive[unit->subno] == nil)
		return 0;
	drive = ctlr->drive[unit->subno];

	e = p+l;
	op = p;
	qlock(drive);
	p = seprint(p, e, "config %4.4uX capabilities %4.4uX", drive->info[Iconfig], drive->info[Icapabilities]);
	if(drive->dma)
		p = seprint(p, e, " dma %8.8uX dmactl %8.8uX", drive->dma, drive->dmactl);
	if(drive->rwm)
		p = seprint(p, e, " rwm %ud rwmctl %ud", drive->rwm, drive->rwmctl);
	if(drive->feat & Dllba)
		p = seprint(p, e, " lba48always %s", (drive->flags&Lba48always) ? "on" : "off");
	p = seprint(p, e, "\n");
	p = seprint(p, e, "model	%s\n", drive->model);
	p = seprint(p, e, "serial	%s\n", drive->serial);
	p = seprint(p, e, "firm	%s\n", drive->firmware);
	p = seprint(p, e, "feat	");
	p = pflag(p, e, drive);
	if(drive->sectors){
		p = seprint(p, e, "geometry %llud %d", drive->sectors, drive->secsize);
		if(drive->pkt == 0 && (drive->feat & Dlba) == 0)
			p = seprint(p, e, " %d %d %d", drive->c, drive->h, drive->s);
		p = seprint(p, e, "\n");
		p = seprint(p, e, "alignment %d %d\n",
			drive->secsize<<drive->physshift, drive->physalign);
	}
	p = seprint(p, e, "missirq	%ud\n", drive->missirq);
	p = seprint(p, e, "sloop	%ud\n", drive->spurloop);
	p = seprint(p, e, "irq	%ud %ud\n", ctlr->nrq, drive->irq);
	p = seprint(p, e, "bsy	%ud %ud\n", ctlr->bsy, drive->bsy);
	p = seprint(p, e, "nildrive	%ud\n", ctlr->nildrive);
	qunlock(drive);

	return p - op;
}

static int
atawctl(SDunit* unit, Cmdbuf* cb)
{
	Ctlr *ctlr;
	Drive *drive;

	if((ctlr = unit->dev->ctlr) == nil || ctlr->drive[unit->subno] == nil)
		return 0;
	drive = ctlr->drive[unit->subno];

	qlock(drive);
	if(waserror()){
		qunlock(drive);
		nexterror();
	}

	/*
	 * Dma and rwm control is passive at the moment,
	 * i.e. it is assumed that the hardware is set up
	 * correctly already either by the BIOS or when
	 * the drive was initially identified.
	 */
	if(strcmp(cb->f[0], "dma") == 0){
		if(cb->nf != 2 || drive->dma == 0)
			error(Ebadctl);
		if(strcmp(cb->f[1], "on") == 0)
			drive->dmactl = drive->dma;
		else if(strcmp(cb->f[1], "off") == 0)
			drive->dmactl = 0;
		else
			error(Ebadctl);
	}
	else if(strcmp(cb->f[0], "rwm") == 0){
		if(cb->nf != 2 || drive->rwm == 0)
			error(Ebadctl);
		if(strcmp(cb->f[1], "on") == 0)
			drive->rwmctl = drive->rwm;
		else if(strcmp(cb->f[1], "off") == 0)
			drive->rwmctl = 0;
		else
			error(Ebadctl);
	}
	else if(strcmp(cb->f[0], "lba48always") == 0){
		if(cb->nf != 2 || !(drive->feat & Dllba))
			error(Ebadctl);
		if(strcmp(cb->f[1], "on") == 0)
			drive->flags |= Lba48always;
		else if(strcmp(cb->f[1], "off") == 0)
			drive->flags &= ~Lba48always;
		else
			error(Ebadctl);
	}
	else if(strcmp(cb->f[0], "identify") == 0){
		atadrive(unit, drive, ctlr->cmdport, ctlr->ctlport, drive->dev);
	}
	else
		error(Ebadctl);
	qunlock(drive);
	poperror();

	return 0;
}

SDifc sdideifc = {
	"ide",				/* name */

	atapnp,				/* pnp */
	nil,				/* legacy */
	ataenable,			/* enable */
	atadisable,			/* disable */

	scsiverify,			/* verify */
	ataonline,			/* online */
	atario,				/* rio */
	atarctl,			/* rctl */
	atawctl,			/* wctl */

	scsibio,			/* bio */
	ataprobew,			/* probe */
	ataclear,			/* clear */
	atastat,			/* rtopctl */
	nil,				/* wtopctl */
	ataataio,
};
