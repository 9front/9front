#include <u.h>
#include "dat.h"
#include "fns.h"
#include "mem.h"

enum {
	Sectsz = 0x200,
	Dirsz = 0x20,
	Maxpath = 64,
	Fat12 = 1,
	Fat16 = 2,
	Fat32 = 4,
};

typedef struct File File;
typedef struct Dir Dir;
typedef struct Pbs Pbs;
typedef struct Pbs32 Pbs32;
typedef struct Fat Fat;

struct Fat
{
	ulong ver;
	ulong clustsize;
	ulong eofmark;
	ulong partlba;
	ulong fatlba;
	ulong dirstart; /* LBA for FAT16, cluster for FAT32 */
	ulong dirents;
	ulong datalba;
};

struct File
{
	Fat *fat;
	ulong lba;
	ulong clust;
	ulong lbaoff;
	ulong len;
	uchar *rp;
	uchar *ep;
	uchar buf[Sectsz];
};

struct Dir
{
	char name[11];
	uchar attr;
	uchar reserved;
	uchar ctime;
	uchar ctime[2];
	uchar cdate[2];
	uchar adate[2];
	uchar starthi[2];
	uchar mtime[2];
	uchar mdate[2];
	uchar startlo[2];
	uchar len[4];
};

struct Pbs
{
	uchar magic[3];
	uchar version[8];
	uchar sectsize[2];
	uchar clustsize;
	uchar nreserv[2];
	uchar nfats;
	uchar rootsize[2];
	uchar volsize[2];
	uchar mediadesc;
	uchar fatsize[2];
	uchar trksize[2];
	uchar nheads[2];
	uchar nhidden[4];
	uchar bigvolsize[4];
	uchar driveno;
	uchar reserved0;
	uchar bootsig;
	uchar volid[4];
	uchar label[11];
	uchar type[8];
};

struct Pbs32
{
	uchar common[36];
	uchar fatsize[4];
	uchar flags[2];
	uchar ver[2];
	uchar rootclust[4];
	uchar fsinfo[2];
	uchar bootbak[2];
	uchar reserved0[12];
	uchar driveno;
	uchar reserved1;
	uchar bootsig;
	uchar volid[4];
	uchar label[11];
	uchar type[8];
};

enum {
	Initfreq	= 400000,	/* initialisation frequency for MMC */
	SDfreq		= 25000000,	/* standard SD frequency */
	DTO		= 14,		/* data timeout exponent (guesswork) */
};

enum {
	/* Controller registers */
	Sysaddr			= 0x00>>2,
	Blksizecnt		= 0x04>>2,
	Arg1			= 0x08>>2,
	Cmdtm			= 0x0c>>2,
	Resp0			= 0x10>>2,
	Resp1			= 0x14>>2,
	Resp2			= 0x18>>2,
	Resp3			= 0x1c>>2,
	Data			= 0x20>>2,
	Status			= 0x24>>2,
	Control0		= 0x28>>2,
	Control1		= 0x2c>>2,
	Interrupt		= 0x30>>2,
	Irptmask		= 0x34>>2,
	Irpten			= 0x38>>2,
	Capabilites		= 0x40>>2,
	Forceirpt		= 0x50>>2,
	Boottimeout		= 0x60>>2,
	Dbgsel			= 0x64>>2,
	Spiintspt		= 0xf0>>2,
	Slotisrver		= 0xfc>>2,

	/* Control0 */
	Dwidth4			= 1<<1,
	Dwidth1			= 0<<1,

	/* Control1 */
	Srstdata		= 1<<26,	/* reset data circuit */
	Srstcmd			= 1<<25,	/* reset command circuit */
	Srsthc			= 1<<24,	/* reset complete host controller */
	Datatoshift		= 16,		/* data timeout unit exponent */
	Datatomask		= 0xF0000,
	Clkfreq8shift		= 8,		/* SD clock base divider LSBs */
	Clkfreq8mask		= 0xFF00,
	Clkfreqms2shift		= 6,		/* SD clock base divider MSBs */
	Clkfreqms2mask		= 0xC0,
	Clkgendiv		= 0<<5,		/* SD clock divided */
	Clkgenprog		= 1<<5,		/* SD clock programmable */
	Clken			= 1<<2,		/* SD clock enable */
	Clkstable		= 1<<1,	
	Clkintlen		= 1<<0,		/* enable internal EMMC clocks */

	/* Cmdtm */
	Indexshift		= 24,
	Suspend			= 1<<22,
	Resume			= 2<<22,
	Abort			= 3<<22,
	Isdata			= 1<<21,
	Ixchken			= 1<<20,
	Crcchken		= 1<<19,
	Respmask		= 3<<16,
	Respnone		= 0<<16,
	Resp136			= 1<<16,
	Resp48			= 2<<16,
	Resp48busy		= 3<<16,
	Multiblock		= 1<<5,
	Host2card		= 0<<4,
	Card2host		= 1<<4,
	Autocmd12		= 1<<2,
	Autocmd23		= 2<<2,
	Blkcnten		= 1<<1,
	Dmaen			= 1<<0,

	/* Interrupt */
	Acmderr		= 1<<24,
	Denderr		= 1<<22,
	Dcrcerr		= 1<<21,
	Dtoerr		= 1<<20,
	Cbaderr		= 1<<19,
	Cenderr		= 1<<18,
	Ccrcerr		= 1<<17,
	Ctoerr		= 1<<16,
	Err		= 1<<15,
	Cardintr	= 1<<8,
	Cardinsert	= 1<<6,
	Readrdy		= 1<<5,
	Writerdy	= 1<<4,
	Dmaintr		= 1<<3,
	Datadone	= 1<<1,
	Cmddone		= 1<<0,

	/* Status */
	Present		= 1<<18,
	Bufread		= 1<<11,
	Bufwrite	= 1<<10,
	Readtrans	= 1<<9,
	Writetrans	= 1<<8,
	Datactive	= 1<<2,
	Datinhibit	= 1<<1,
	Cmdinhibit	= 1<<0,

	Inittimeout	= 15,
//	Multiblock	= 1,

	/* Commands */
	GO_IDLE_STATE	= 0,
	ALL_SEND_CID	= 2,
	SEND_RELATIVE_ADDR= 3,
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

	/* OCR (operating conditions register) */
	Powerup	= 1<<31,
};

static int cmdinfo[64] = {
[0]  Ixchken,
[2]  Resp136,
[3]  Resp48 | Ixchken | Crcchken,
[6]  Resp48 | Ixchken | Crcchken,
[7]  Resp48busy | Ixchken | Crcchken,
[8]  Resp48 | Ixchken | Crcchken,
[9]  Resp136,
[12] Resp48busy | Ixchken | Crcchken,
[13] Resp48 | Ixchken | Crcchken,
[16] Resp48,
[17] Resp48 | Isdata | Card2host | Ixchken | Crcchken | Dmaen,
[18] Resp48 | Isdata | Card2host | Multiblock | Blkcnten | Ixchken | Crcchken | Dmaen,
[24] Resp48 | Isdata | Host2card | Ixchken | Crcchken | Dmaen,
[25] Resp48 | Isdata | Host2card | Multiblock | Blkcnten | Ixchken | Crcchken | Dmaen,
[41] Resp48,
[55] Resp48 | Ixchken | Crcchken,
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	u32int	*regs;
	ulong	extclk;

	/* SD card registers */
	u16int	rca;
	u32int	ocr;
	u32int	cid[4];
	u32int	csd[4];
};
static Ctlr ctlr = {
	.regs	= (u32int*)0xE0101000,
	.extclk	= 100000000,
};


static ushort
GETSHORT(void *v)
{
	uchar *p = v;
	return p[0] | p[1]<<8;
}
static ulong
GETLONG(void *v)
{
	uchar *p = v;
	return p[0] | p[1]<<8 | p[2]<<16 | p[3]<<24;
}

static int
memcmp(void *src, void *dst, int n)
{
	uchar *d = dst;
	uchar *s = src;
	int r = 0;

	while(n-- > 0){
		r = *d++ - *s++;
		if(r != 0)
			break;
	}

	return r;
}

static uint
clkdiv(uint d)
{
	uint v;

	v = (d << Clkfreq8shift) & Clkfreq8mask;
	v |= ((d >> 8) << Clkfreqms2shift) & Clkfreqms2mask;
	return v;
}

static int
mmcwait(int mask)
{
	int i, t;

	t = 0;
	while(((i=ctlr.regs[Interrupt])&mask) == 0)
		if(t++ > 10000000)
			break;

	return i;
}

static int
mmccmd(u32int cmd, u32int arg, u32int *resp)
{
	u32int *r;
	u32int c;
	int i;

	c = (cmd << Indexshift) | cmdinfo[cmd];

	r = ctlr.regs;
	if(r[Status] & Cmdinhibit){
		print("mmc: need to reset Cmdinhibit intr %x stat %x\n",
			r[Interrupt], r[Status]);
		r[Control1] |= Srstcmd;
		while(r[Control1] & Srstcmd)
			;
		while(r[Status] & Cmdinhibit)
			;
	}
	if((c & Isdata || (c & Respmask) == Resp48busy) && r[Status] & Datinhibit){
		print("mmc: need to reset Datinhibit intr %x stat %x\n",
			r[Interrupt], r[Status]);
		r[Control1] |= Srstdata;
		while(r[Control1] & Srstdata)
			;
		while(r[Status] & Datinhibit)
			;
	}
	r[Arg1] = arg;
	if((i = r[Interrupt]) != 0){
		if(i != Cardinsert)
			print("mmc: before command, intr was %x\n", i);
		r[Interrupt] = i;
	}
	r[Cmdtm] = c;

	i = mmcwait(Cmddone|Err);
	if((i&(Cmddone|Err)) != Cmddone){
		if((i&~Err) != Ctoerr)
			print("mmc: CMD%d error intr %x stat %x\n", cmd, i, r[Status]);
		r[Interrupt] = i;
		if(r[Status]&Cmdinhibit){
			r[Control1] |= Srstcmd;
			while(r[Control1]&Srstcmd)
				;
		}
		return -1;
	}
	r[Interrupt] = i & ~(Datadone|Readrdy|Writerdy);
	switch(c & Respmask){
	case Resp136:
		resp[0] = r[Resp0]<<8;
		resp[1] = r[Resp0]>>24 | r[Resp1]<<8;
		resp[2] = r[Resp1]>>24 | r[Resp2]<<8;
		resp[3] = r[Resp2]>>24 | r[Resp3]<<8;
		break;
	case Resp48:
	case Resp48busy:
		resp[0] = r[Resp0];
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}
	if((c & Respmask) == Resp48busy){
		r[Irpten] = Datadone|Err;
		i = mmcwait(Cmddone|Err);
		if(i)
			r[Interrupt] = i;
		r[Irpten] = 0;
		if((i & Datadone) == 0)
			print("mmc: no Datadone after CMD%d\n", cmd);
		if(i & Err)
			print("mmc: CMD%d error interrupt %x\n", cmd, i);
	}

	/*
	 * Once card is selected, use faster clock
	 */
	if(cmd == SELECT_CARD){
		sleep(10);
		r[Control1] = clkdiv(ctlr.extclk / SDfreq - 1) |
			DTO << Datatoshift | Clkgendiv | Clken | Clkintlen;
		for(i = 0; i < 1000; i++){
			sleep(1);
			if(r[Control1] & Clkstable)
				break;
		}
		sleep(10);
	}

	/*
	 * If card bus width changes, change host bus width
	 */
	if(cmd == SET_BUS_WIDTH)
		switch(arg){
		case 0:
			r[Control0] &= ~Dwidth4;
			break;
		case 2:
			r[Control0] |= Dwidth4;
			break;
		}
	return 0;
}

static int
mmconline(void)
{
	u32int r[4];
	int hcs, i;

	mmccmd(GO_IDLE_STATE, 0, r);

	hcs = 0;
	if(mmccmd(SD_SEND_IF_COND, Voltage|Checkpattern, r) == 0){
		if(r[0] == (Voltage|Checkpattern))	/* SD 2.0 or above */
			hcs = Hcs;
	}
	for(i = 0; i < Inittimeout; i++){
		sleep(100);
		mmccmd(APP_CMD, 0, r);
		mmccmd(SD_SEND_OP_COND, hcs|V3_3, r);
		if(r[0] & Powerup)
			break;
	}
	if(i == Inittimeout){
		print("mmc: card won't power up\n");
		return -1;
	}
	ctlr.ocr = r[0];
	mmccmd(ALL_SEND_CID, 0, r);
	memcpy(ctlr.cid, r, sizeof ctlr.cid);
	mmccmd(SEND_RELATIVE_ADDR, 0, r);
	ctlr.rca = r[0]>>16;
	mmccmd(SEND_CSD, ctlr.rca<<Rcashift, r);
	memcpy(ctlr.csd, r, sizeof ctlr.csd);
	mmccmd(SELECT_CARD, ctlr.rca<<Rcashift, r);
	mmccmd(SET_BLOCKLEN, Sectsz, r);
	mmccmd(APP_CMD, ctlr.rca<<Rcashift, r);
	mmccmd(SET_BUS_WIDTH, Width4, r);
	return 0;
}

static int
mmcinit(void)
{
	u32int *r;
	int i;

	r = ctlr.regs;
	r[Control1] = Srsthc;
	for(i = 0; i < 100; i++){
		sleep(10);
		if((r[Control1] & Srsthc) == 0)
			break;
	}
	if(i == 100){
		print("mmc: reset timeout!\n");
		return -1;
	}
	r[Control1] = clkdiv(ctlr.extclk / Initfreq - 1) | DTO << Datatoshift |
		Clkgendiv | Clken | Clkintlen;
	for(i = 0; i < 1000; i++){
		sleep(1);
		if(r[Control1] & Clkstable)
			break;
	}
	if(i == 1000){
		print("mmc: SD clock won't initialise!\n");
		return -1;
	}
	r[Irptmask] = ~(Dtoerr|Cardintr|Dmaintr);
	return mmconline();
}

static int
mmcread(ulong bno, uchar buf[Sectsz])
{
	u32int *r, rr[4];
	int i, t;

	r = ctlr.regs;
	for(t=0; t<3; t++){
		r[Sysaddr] = (u32int)buf;
		r[Blksizecnt] = 7<<12 | 1<<16 | Sectsz;
		r[Irpten] = Datadone|Err;
		mmccmd(READ_SINGLE_BLOCK, ctlr.ocr & Ccs? bno : bno*Sectsz, rr);
		i = mmcwait(Datadone|Err);
		if(i)
			r[Interrupt] = i;
		r[Irpten] = 0;
		if((i & Err) != 0)
			print("mmcread: error intr %x stat %x\n", i, r[Status]);
		else if((i & Datadone) == 0)
			print("mmcread: timeout intr %x stat %x\n", i, r[Status]);
		else
			return 0;
	}
	return -1;
}

static int
dirname(Dir *d, char buf[Maxpath])
{
	char c, *x;

	if(d->attr == 0x0F || *d->name <= 0)
		return -1;
	memcpy(buf, d->name, 8);
	x = buf+8;
	while(x > buf && x[-1] == ' ')
		x--;
	if(d->name[8] != ' '){
		*x++ = '.';
		memcpy(x, d->name+8, 3);
		x += 3;
	}
	while(x > buf && x[-1] == ' ')
		x--;
	*x = 0;
	x = buf;
	while(c = *x){
		if(c >= 'A' && c <= 'Z'){
			c -= 'A';
			c += 'a';
		}
		*x++ = c;
	}
	return x - buf;
}

static ulong
dirclust(Dir *d)
{
	return GETSHORT(d->starthi)<<16 | GETSHORT(d->startlo);
}

static void
fileinit(File *fp, Fat *fat, ulong lba)
{
	fp->fat = fat;
	fp->lba = lba;
	fp->len = 0;
	fp->lbaoff = 0;
	fp->clust = ~0U;
	fp->rp = fp->ep = fp->buf + Sectsz;
}

static ulong
readnext(File *fp, ulong clust)
{
	Fat *fat = fp->fat;
	uchar tmp[2], *p;
	ulong idx, lba;

	if(fat->ver == Fat12)
		idx = (3*clust)/2;
	else
		idx = clust*fat->ver;
	lba = fat->fatlba + (idx / Sectsz);
	if(mmcread(lba, fp->buf))
		memset(fp->buf, 0xff, Sectsz);
	p = &fp->buf[idx % Sectsz];
	if(p == &fp->buf[Sectsz-1]){
		tmp[0] = *p;
		if(mmcread(++lba, fp->buf))
			memset(fp->buf, 0xff, Sectsz);
		tmp[1] = fp->buf[0];
		p = tmp;
	}
	if(fat->ver == Fat32)
		return GETLONG(p) & 0xfffffff;
	idx = GETSHORT(p);
	if(fat->ver == Fat12){
		if(clust & 1)
			idx >>= 4;
		idx &= 0xfff;
	}
	return idx;
}

static int
fileread(File *fp, void *data, int len)
{
	Fat *fat = fp->fat;

	if(fp->len > 0 && fp->rp >= fp->ep){
		if(fp->clust != ~0U){
			if(fp->lbaoff % fat->clustsize == 0){
				if(fp->clust < 2 || fp->clust >= fat->eofmark)
					return -1;
				fp->lbaoff = (fp->clust - 2) * fat->clustsize;
				fp->clust = readnext(fp, fp->clust);
				fp->lba = fp->lbaoff + fat->datalba;
			}
			fp->lbaoff++;
		}
		if(mmcread(fp->lba++, fp->rp = fp->buf))
			return -1;
	}
	if(fp->len < len)
		len = fp->len;
	if(len > (fp->ep - fp->rp))
		len = fp->ep - fp->rp;
	memcpy(data, fp->rp, len);
	fp->rp += len;
	fp->len -= len;
	return len;
}

static int
fatwalk(File *fp, Fat *fat, char *path)
{
	char name[Maxpath], *end;
	int i, j;
	Dir d;

	if(fat->ver == Fat32){
		fileinit(fp, fat, 0);
		fp->clust = fat->dirstart;
		fp->len = ~0U;
	}else{
		fileinit(fp, fat, fat->dirstart);
		fp->len = fat->dirents * Dirsz;
	}
	for(;;){
		if(fileread(fp, &d, Dirsz) != Dirsz)
			break;
		if((i = dirname(&d, name)) <= 0)
			continue;
		while(*path == '/')
			path++;
		for(end = path; *end != '\0'; end++)
			if(*end == '/')
				break;
		j = end - path;
		if(i == j && memcmp(name, path, j) == 0){
			fileinit(fp, fat, 0);
			fp->clust = dirclust(&d);
			fp->len = GETLONG(d.len);
			if(*end == 0)
				return 0;
			else if(d.attr & 0x10){
				fp->len = fat->clustsize * Sectsz;
				path = end;
				continue;
			}
			break;
		}
	}
	return -1;
}

static int
conffat(Fat *fat, void *buf)
{
	Pbs *p = buf;
	uint fatsize, volsize, datasize, reserved;
	uint ver, dirsize, dirents, clusters;

	if(GETSHORT(p->sectsize) != Sectsz)
		return -1;
	if(memcmp(p->type, "FAT", 3) && memcmp(((Pbs32*)buf)->type, "FAT", 3))
		return -1;
	
	/* load values from fat */
	ver = 0;
	fatsize = GETSHORT(p->fatsize);
	if(fatsize == 0){
		fatsize = GETLONG(((Pbs32*)buf)->fatsize);
		ver = Fat32;
	}
	volsize = GETSHORT(p->volsize);
	if(volsize == 0)
		volsize = GETLONG(p->bigvolsize);
	reserved = GETSHORT(p->nreserv);
	dirents = GETSHORT(p->rootsize);
	dirsize = (dirents * Dirsz + Sectsz - 1) / Sectsz;
	datasize = volsize - (reserved + fatsize * p->nfats + dirsize);
	clusters = datasize / p->clustsize;
	if(ver != Fat32)
		if(clusters < 0xff7)
			ver = Fat12;
		else
			ver = Fat16;
	
	/* fill FAT descriptor */
	fat->ver = ver;
	fat->dirents = dirents;
	fat->clustsize = p->clustsize;
	fat->fatlba = fat->partlba + reserved;
	fat->dirstart  = fat->fatlba + fatsize * p->nfats;
	if(ver == Fat32){
		fat->datalba = fat->dirstart;
		fat->dirstart  = GETLONG(((Pbs32*)buf)->rootclust);
		fat->eofmark = 0xffffff7;
	}else{
		fat->datalba = fat->dirstart + dirsize;
		if(ver == Fat16)
			fat->eofmark = 0xfff7;
		else
			fat->eofmark = 0xff7;
	}
	return 0;
}

static int
findfat(Fat *fat, ulong xbase, ulong lba)
{
	struct {
		uchar status;
		uchar bchs[3];
		uchar typ;
		uchar echs[3];
		uchar lba[4];
		uchar len[4];
	} p[4];
	uchar buf[Sectsz];
	int i;

	if(xbase == 0)
		xbase = lba;
	if(mmcread(lba, buf))
		return -1;
	if(buf[0x1fe] != 0x55 || buf[0x1ff] != 0xAA)
		return -1;
	memcpy(p, &buf[0x1be], sizeof(p));
	for(i=0; i<4; i++){
		switch(p[i].typ){
		case 0x05:
		case 0x0f:
		case 0x85:
			/* extended partitions */
			if(!findfat(fat, xbase, xbase + GETLONG(p[i].lba)))
				return 0;
			/* no break */
		case 0x00:
			continue;
		default:
			fat->partlba = lba + GETLONG(p[i].lba);
			if(mmcread(fat->partlba, buf))
				continue;
			if(!conffat(fat, buf))
				return 0;
		}
	}
	return -1;
}

static int
load(Fat *fat, char *path, void *data)
{
	uchar *p;
	File fi;
	int n;

	print("%s", path);
	if(fatwalk(&fi, fat, path)){
		print(": not found\n", path);
		return -1;
	}
	print("...");
	p = data;
	while((n = fileread(&fi, p, Sectsz)) > 0)
		p += n;
	print("\n");
	return p - (uchar*)data;
}

int
mmcboot(void)
{
	char file[Maxpath], *p;
	Fat fat;

	if(mmcinit() < 0)
		return 0;
	if(findfat(&fat, 0, 0)){
		print("no fat\n");
		return 0;
	}
	memcpy(file, "9zynq", 6);
	memset(p = (char*)CONF, 0, CONFSIZE);
	p += load(&fat, "plan9.ini", p);
	p -= 9; /* "bootfile=" */
	while(--p >= (char*)CONF){
		while(p > (char*)CONF && p[-1] != '\n')
			p--;
		if(memcmp("bootfile=", p, 9) == 0){
			p += 9;
			memcpy(file, p, sizeof(file)-1);
			for(p=file; p < &file[sizeof(file)-1]; p++)
				if(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
					break;
			*p = '\0';
			break;
		}
	}
	return load(&fat, file, (void*)TZERO) > 0;
}
