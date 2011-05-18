/*
 *	SB 16 driver
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

typedef struct	Ring	Ring;
typedef struct	Blaster	Blaster;
typedef struct	Ctlr	Ctlr;

enum
{
	Fmono		= 1,
	Fin		= 2,
	Fout		= 4,

	Vaudio		= 0,
	Vsynth,
	Vcd,
	Vline,
	Vmic,
	Vspeaker,
	Vtreb,
	Vbass,
	Vspeed,
	Nvol,

	Speed		= 44100,
	Ncmd		= 50,		/* max volume command words */

	Blocksize	= 4096,
	Blocks		= 65536/Blocksize,
};

struct Ring
{
	uchar	*buf;
	ulong	nbuf;

	ulong	ri;
	ulong	wi;
};

struct Blaster
{
	Lock;
	int	reset;		/* io ports to the sound blaster */
	int	read;
	int	write;
	int	wstatus;
	int	rstatus;
	int	mixaddr;
	int	mixdata;
	int	clri8;
	int	clri16;
	int	clri401;
	int	dma;

	void	(*startdma)(Ctlr*);
	void	(*intr)(Ctlr*);
};

struct Ctlr
{
	QLock;
	Rendez	vous;
	int	active;		/* boolean dma running */
	int	rivol[Nvol];	/* right/left input/output volumes */
	int	livol[Nvol];
	int	rovol[Nvol];
	int	lovol[Nvol];
	int	major;		/* SB16 major version number (sb 4) */
	int	minor;		/* SB16 minor version number */
	ulong	totcount;	/* how many bytes processed since open */
	vlong	tottime;	/* time at which totcount bytes were processed */
	Ring	ring;		/* dma ring buffer */
	Blaster	blaster;
	Audio	*adev;
};

static struct
{
	char*	name;
	int	flag;
	int	ilval;		/* initial values */
	int	irval;
} volumes[] = {
	[Vaudio]		"audio",	Fout, 		50,	50,
	[Vsynth]		"synth",	Fin|Fout,	0,	0,
	[Vcd]		"cd",		Fin|Fout,	0,	0,
	[Vline]		"line",	Fin|Fout,	0,	0,
	[Vmic]		"mic",	Fin|Fout|Fmono,	0,	0,
	[Vspeaker]	"speaker",	Fout|Fmono,	0,	0,

	[Vtreb]		"treb",		Fout, 		50,	50,
	[Vbass]		"bass",		Fout, 		50,	50,

	[Vspeed]	"speed",	Fin|Fout|Fmono,	Speed,	Speed,
	0
};

static	char	Emajor[]	= "soundblaster not responding/wrong version";
static	char	Emode[]		= "illegal open mode";
static	char	Evolume[]	= "illegal volume specifier";


static long
buffered(Ring *r)
{
	ulong ri, wi;

	ri = r->ri;
	wi = r->wi;
	if(wi >= ri)
		return wi - ri;
	else
		return r->nbuf - (ri - wi);
}

static long
available(Ring *r)
{
	long m;

	m = (r->nbuf - 1) - buffered(r);
	if(m < 0)
		m = 0;
	return m;
}

static long
readring(Ring *r, uchar *p, long n)
{
	long n0, m;

	n0 = n;
	while(n > 0){
		if((m = buffered(r)) <= 0)
			break;
		if(m > n)
			m = n;
		if(p){
			if(r->ri + m > r->nbuf)
				m = r->nbuf - r->ri;
			memmove(p, r->buf + r->ri, m);
			p += m;
		}
		r->ri = (r->ri + m) % r->nbuf;
		n -= m;
	}
	return n0 - n;
}

static long
writering(Ring *r, uchar *p, long n)
{
	long n0, m;

	n0 = n;
	while(n > 0){
		if((m = available(r)) <= 0)
			break;
		if(m > n)
			m = n;
		if(p){
			if(r->wi + m > r->nbuf)
				m = r->nbuf - r->wi;
			memmove(r->buf + r->wi, p, m);
			p += m;
		}
		r->wi = (r->wi + m) % r->nbuf;
		n -= m;
	}
	return n0 - n;
}

static	int
sbcmd(Blaster *blaster, int val)
{
	int i, s;

	for(i=1<<16; i!=0; i--) {
		s = inb(blaster->wstatus);
		if((s & 0x80) == 0) {
			outb(blaster->write, val);
			return 0;
		}
	}
	return 1;
}

static	int
sbread(Blaster *blaster)
{
	int i, s;

	for(i=1<<16; i!=0; i--) {
		s = inb(blaster->rstatus);
		if((s & 0x80) != 0) {
			return inb(blaster->read);
		}
	}
	return -1;
}

static int
ess1688w(Blaster *blaster, int reg, int val)
{
	if(sbcmd(blaster, reg) || sbcmd(blaster, val))
		return 1;
	return 0;
}

static int
ess1688r(Blaster *blaster, int reg)
{
	if(sbcmd(blaster, 0xC0) || sbcmd(blaster, reg))
		return -1;
	return sbread(blaster);
}

static	int
mxcmd(Blaster *blaster, int addr, int val)
{
	outb(blaster->mixaddr, addr);
	outb(blaster->mixdata, val);
	return 1;
}

static	int
mxread(Blaster *blaster, int addr)
{
	int s;

	outb(blaster->mixaddr, addr);
	s = inb(blaster->mixdata);
	return s;
}

static	void
mxcmds(Blaster *blaster, int s, int v)
{

	if(v > 100)
		v = 100;
	if(v < 0)
		v = 0;
	mxcmd(blaster, s, (v*255)/100);
}

static	void
mxcmdt(Blaster *blaster, int s, int v)
{

	if(v > 100)
		v = 100;
	if(v <= 0)
		mxcmd(blaster, s, 0);
	else
		mxcmd(blaster, s, 255-100+v);
}

static	void
mxcmdu(Blaster *blaster, int s, int v)
{

	if(v > 100)
		v = 100;
	if(v <= 0)
		v = 0;
	mxcmd(blaster, s, 128-50+v);
}

static	void
mxvolume(Ctlr *ctlr)
{
	Blaster *blaster;
	int *left, *right;
	int source;

	if(0){
		left = ctlr->livol;
		right = ctlr->rivol;
	}else{
		left = ctlr->lovol;
		right = ctlr->rovol;
	}

	blaster = &ctlr->blaster;

	ilock(blaster);

	mxcmd(blaster, 0x30, 255);		/* left master */
	mxcmd(blaster, 0x31, 255);		/* right master */
	mxcmd(blaster, 0x3f, 0);		/* left igain */
	mxcmd(blaster, 0x40, 0);		/* right igain */
	mxcmd(blaster, 0x41, 0);		/* left ogain */
	mxcmd(blaster, 0x42, 0);		/* right ogain */

	mxcmds(blaster, 0x32, left[Vaudio]);
	mxcmds(blaster, 0x33, right[Vaudio]);

	mxcmds(blaster, 0x34, left[Vsynth]);
	mxcmds(blaster, 0x35, right[Vsynth]);

	mxcmds(blaster, 0x36, left[Vcd]);
	mxcmds(blaster, 0x37, right[Vcd]);

	mxcmds(blaster, 0x38, left[Vline]);
	mxcmds(blaster, 0x39, right[Vline]);

	mxcmds(blaster, 0x3a, left[Vmic]);
	mxcmds(blaster, 0x3b, left[Vspeaker]);

	mxcmdu(blaster, 0x44, left[Vtreb]);
	mxcmdu(blaster, 0x45, right[Vtreb]);

	mxcmdu(blaster, 0x46, left[Vbass]);
	mxcmdu(blaster, 0x47, right[Vbass]);

	source = 0;
	if(left[Vsynth])
		source |= 1<<6;
	if(right[Vsynth])
		source |= 1<<5;
	if(left[Vaudio])
		source |= 1<<4;
	if(right[Vaudio])
		source |= 1<<3;
	if(left[Vcd])
		source |= 1<<2;
	if(right[Vcd])
		source |= 1<<1;
	if(left[Vmic])
		source |= 1<<0;
	if(0)
		mxcmd(blaster, 0x3c, 0);		/* output switch */
	else
		mxcmd(blaster, 0x3c, source);
	mxcmd(blaster, 0x3d, source);		/* input left switch */
	mxcmd(blaster, 0x3e, source);		/* input right switch */
	iunlock(blaster);
}

static	void
contindma(Ctlr *ctlr)
{
	Blaster *blaster;
	Ring *ring;

	blaster = &ctlr->blaster;
	ring = &ctlr->ring;
	if(buffered(ring) >= Blocksize){
		ring->ri = ring->nbuf - dmacount(blaster->dma);

		ctlr->totcount += Blocksize;
		ctlr->tottime = todget(nil);
	}else{
		dmaend(blaster->dma);
		sbcmd(blaster, 0xd9);	/* exit at end of count */
		sbcmd(blaster, 0xd5);	/* pause */
		ctlr->active = 0;
	}
	wakeup(&ctlr->vous);
}

/*
 * cause sb to get an interrupt per buffer.
 * start first dma
 */
static	void
sb16startdma(Ctlr *ctlr)
{
	Blaster *blaster;
	Ring *ring;
	long count;
	int speed;

	blaster = &ctlr->blaster;
	ring = &ctlr->ring;
	ilock(blaster);
	dmaend(blaster->dma);
	if(0) {
		sbcmd(blaster, 0x42);			/* input sampling rate */
		speed = ctlr->livol[Vspeed];
	} else {
		sbcmd(blaster, 0x41);			/* output sampling rate */
		speed = ctlr->lovol[Vspeed];
	}
	sbcmd(blaster, speed>>8);
	sbcmd(blaster, speed);

	if(0)
		sbcmd(blaster, 0xbe);			/* A/D, autoinit */
	else
		sbcmd(blaster, 0xb6);			/* D/A, autoinit */

	sbcmd(blaster, 0x30);				/* stereo, signed 16 bit */

	count = (Blocksize>>1) - 1;
	sbcmd(blaster, count);
	sbcmd(blaster, count>>8);

	ctlr->active = 1;
	if(dmasetup(blaster->dma, ring->buf, ring->nbuf, DMAWRITE|DMALOOP) < 0){
		ctlr->active = 0;
		print("#A%d: dmasetup fail\n", ctlr->adev->ctlrno);
	}
	iunlock(blaster);
}

static int
ess1688reset(Blaster *blaster, int ctlrno)
{
	int i;

	outb(blaster->reset, 3);
	delay(1);			/* >3 υs */
	outb(blaster->reset, 0);
	delay(1);

	i = sbread(blaster);
	if(i != 0xAA) {
		print("#A%d: no response %#.2x\n", ctlrno, i);
		return 1;
	}

	if(sbcmd(blaster, 0xC6)){		/* extended mode */
		print("#A%d: barf 3\n", ctlrno);
		return 1;
	}

	return 0;
}

static	void
ess1688startdma(Ctlr *ctlr)
{
	Blaster *blaster;
	Ring *ring;
	ulong count;
	int speed, x;

	blaster = &ctlr->blaster;
	ring = &ctlr->ring;

	ilock(blaster);
	dmaend(blaster->dma);

	ess1688reset(blaster, ctlr->adev->ctlrno);

	/*
	 * Set the speed.
	 */
	if(0)
		speed = ctlr->livol[Vspeed];
	else
		speed = ctlr->lovol[Vspeed];
	if(speed < 4000)
		speed = 4000;
	else if(speed > 48000)
		speed = 48000;

	if(speed > 22000)
		  x = 0x80|(256-(795500+speed/2)/speed);
	else
		  x = 128-(397700+speed/2)/speed;
	ess1688w(blaster, 0xA1, x & 0xFF);

	speed = (speed * 9) / 20;
	x = 256 - 7160000 / (speed * 82);
	ess1688w(blaster, 0xA2, x & 0xFF);

	if(0)
		ess1688w(blaster, 0xB8, 0x0E);		/* A/D, autoinit */
	else
		ess1688w(blaster, 0xB8, 0x04);		/* D/A, autoinit */
	x = ess1688r(blaster, 0xA8) & ~0x03;
	ess1688w(blaster, 0xA8, x|0x01);			/* 2 channels */
	ess1688w(blaster, 0xB9, 2);			/* demand mode, 4 bytes per request */

	if(1)
		ess1688w(blaster, 0xB6, 0);		/* for output */

	ess1688w(blaster, 0xB7, 0x71);
	ess1688w(blaster, 0xB7, 0xBC);

	x = ess1688r(blaster, 0xB1) & 0x0F;
	ess1688w(blaster, 0xB1, x|0x50);
	x = ess1688r(blaster, 0xB2) & 0x0F;
	ess1688w(blaster, 0xB2, x|0x50);

	if(1)
		sbcmd(blaster, 0xD1);			/* speaker on */

	count = -Blocksize;
	ess1688w(blaster, 0xA4, count & 0xFF);
	ess1688w(blaster, 0xA5, (count>>8) & 0xFF);
	x = ess1688r(blaster, 0xB8);
	ess1688w(blaster, 0xB8, x|0x05);

	ctlr->active = 1;
	if(dmasetup(blaster->dma, ring->buf, ring->nbuf, DMAWRITE|DMALOOP) < 0){
		ctlr->active = 0;
		print("#A%d: dmasetup fail\n", ctlr->adev->ctlrno);
	}
	iunlock(blaster);
}

static void
sb16intr(Ctlr *ctlr)
{
	Blaster *blaster;
	int stat;

	blaster = &ctlr->blaster;
	ilock(blaster);
	stat = mxread(blaster, 0x82);		/* get irq status */
	if(stat & 3){
		contindma(ctlr);
		if(stat & 2)
			inb(blaster->clri16);
		else if(stat & 1)
			inb(blaster->clri8);
	} else if(stat & 4)
		inb(blaster->clri401);
	iunlock(blaster);
}

static void
ess1688intr(Ctlr *ctlr)
{
	Blaster *blaster;

	blaster = &ctlr->blaster;
	ilock(blaster);
	contindma(ctlr);
	inb(blaster->clri8);
	iunlock(blaster);
}

static void
audiointr(Ureg *, void *arg)
{
	Audio *adev;
	Ctlr *ctlr;

	adev = arg;
	ctlr = adev->ctlr;
	if(!ctlr->active){
		iprint("#A%d: unexpected %s interrupt\n",
			ctlr->adev->ctlrno, ctlr->adev->name);
		return;
	}
	ctlr->blaster.intr(ctlr);
}

static void
setempty(Ctlr *ctlr)
{
	ilock(&ctlr->blaster);
	ctlr->ring.ri = 0;
	ctlr->ring.wi = 0;
	ctlr->totcount = 0;
	ctlr->tottime = 0LL;
	iunlock(&ctlr->blaster);
}

static void
resetlevel(Ctlr *ctlr)
{
	int i;

	for(i=0; volumes[i].name; i++) {
		ctlr->lovol[i] = volumes[i].ilval;
		ctlr->rovol[i] = volumes[i].irval;
		ctlr->livol[i] = volumes[i].ilval;
		ctlr->rivol[i] = volumes[i].irval;
	}
}

static long
audiobuffered(Audio *adev)
{
	return buffered(&((Ctlr*)adev->ctlr)->ring);
}

static long
audiostatus(Audio *adev, void *a, long n, vlong off)
{
	char buf[300];
	Ctlr *ctlr;

	ctlr = adev->ctlr;
	snprint(buf, sizeof(buf), 
		"buffered %.4lx/%.4lx  offset %10lud time %19lld\n",
		buffered(&ctlr->ring), available(&ctlr->ring),
		ctlr->totcount, ctlr->tottime);
	return readstr(off, a, n, buf);
}

static int
inactive(void *arg)
{
	Ctlr *ctlr = arg;

	return !ctlr->active;
}

static int
anybuf(void *arg)
{
	Ctlr *ctlr = arg;

	return available(&ctlr->ring) || inactive(ctlr);
}

static long
audiowrite(Audio *adev, void *vp, long n, vlong)
{
	uchar *p, *e;
	Ctlr *ctlr;
	Ring *ring;
	long m;

	p = vp;
	e = p + n;
	ctlr = adev->ctlr;
	qlock(ctlr);
	if(waserror()){
		qunlock(ctlr);
		nexterror();
	}
	ring = &ctlr->ring;
	while(p < e) {
		if((m = writering(ring, p, e - p)) <= 0){
			if(!ctlr->active && ring->ri == 0)
				ctlr->blaster.startdma(ctlr);
			if(!ctlr->active){
				setempty(ctlr);
				continue;
			}
			sleep(&ctlr->vous, anybuf, ctlr);
			continue;
		}
		p += m;
	}
	poperror();
	qunlock(ctlr);

	return p - (uchar*)vp;
}

static void
audioclose(Audio *adev)
{
	Ctlr *ctlr;

	ctlr = adev->ctlr;
	qlock(ctlr);
	if(waserror()){
		qunlock(ctlr);
		nexterror();
	}
	sleep(&ctlr->vous, inactive, ctlr);
	setempty(ctlr);
	poperror();
	qunlock(ctlr);
}

static int
ess1688(ISAConf* sbconf, Blaster *blaster, int ctlrno)
{
	int i, major, minor;

	/*
	 * Try for ESS1688.
	 */
	sbcmd(blaster, 0xE7);			/* get version */
	major = sbread(blaster);
	minor = sbread(blaster);
	if(major != 0x68 || minor != 0x8B){
		print("#A%d: model %#.2x %#.2x; not ESS1688 compatible\n", ctlrno, major, minor);
		return -1;
	}

	ess1688reset(blaster, ctlrno);

	switch(sbconf->irq){
	case 2:
	case 9:
		i = 0x50|(0<<2);
		break;
	case 5:
		i = 0x50|(1<<2);
		break;
	case 7:
		i = 0x50|(2<<2);
		break;
	case 10:
		i = 0x50|(3<<2);
		break;
	default:
		print("#A%d: bad ESS1688 irq %d\n", ctlrno, sbconf->irq);
		return 1;
	}
	ess1688w(blaster, 0xB1, i);

	switch(sbconf->dma){
	case 0:
		i = 0x50|(1<<2);
		break;
	case 1:
		i = 0xF0|(2<<2);
		break;
	case 3:
		i = 0x50|(3<<2);
		break;
	default:
		print("#A%d: bad ESS1688 dma %lud\n", ctlrno, sbconf->dma);
		return 1;
	}
	ess1688w(blaster, 0xB2, i);

	ess1688reset(blaster, ctlrno);

	blaster->startdma = ess1688startdma;
	blaster->intr = ess1688intr;

	return 0;
}

static int
audioprobe(Audio *adev)
{
	static int irq[] = {9,5,7,10};

	Ctlr *ctlr;
	Blaster *blaster;
	ISAConf sbconf;
	int i, x;

	sbconf.port = 0x220;
	sbconf.irq = 5;
	sbconf.dma = 0;
	if(isaconfig("audio", adev->ctlrno, &sbconf) == 0)
		return -1;

	switch(sbconf.port){
	case 0x220:
	case 0x240:
	case 0x260:
	case 0x280:
		break;
	default:
		print("#A%d: bad port %#lux\n", adev->ctlrno, sbconf.port);
		return -1;
	}

	if(ioalloc(sbconf.port, 0x10, 0, "audio") < 0){
		print("#A%d: cannot ioalloc range %lux+0x10\n", adev->ctlrno, sbconf.port);
		return -1;
	}
	if(ioalloc(sbconf.port+0x100, 1, 0, "audio.mpu401") < 0){
		iofree(sbconf.port);
		print("#A%d: cannot ioalloc range %lux+0x01\n", adev->ctlrno, sbconf.port+0x100);
		return -1;
	}

	ctlr = malloc(sizeof(Ctlr));
	ctlr->adev = adev;
	adev->ctlr = ctlr;

	blaster = &ctlr->blaster;
	blaster->reset = sbconf.port + 0x6;
	blaster->read = sbconf.port + 0xa;
	blaster->write = sbconf.port + 0xc;
	blaster->wstatus = sbconf.port + 0xc;
	blaster->rstatus = sbconf.port + 0xe;
	blaster->mixaddr = sbconf.port + 0x4;
	blaster->mixdata = sbconf.port + 0x5;
	blaster->clri8 = sbconf.port + 0xe;
	blaster->clri16 = sbconf.port + 0xf;
	blaster->clri401 = sbconf.port + 0x100;
	blaster->dma = sbconf.dma;

	blaster->startdma = sb16startdma;
	blaster->intr = sb16intr;

	resetlevel(ctlr);

	outb(blaster->reset, 1);
	delay(1);			/* >3 υs */
	outb(blaster->reset, 0);
	delay(1);

	i = sbread(blaster);
	if(i != 0xaa) {
		print("#A%d: no response #%.2x\n", adev->ctlrno, i);
		iofree(sbconf.port);
		iofree(sbconf.port+0x100);
		free(ctlr);
		return -1;
	}

	sbcmd(blaster, 0xe1);			/* get version */
	ctlr->major = sbread(blaster);
	ctlr->minor = sbread(blaster);

	if(ctlr->major != 4) {
		if(ctlr->major != 3 || ctlr->minor != 1 || ess1688(&sbconf, blaster, adev->ctlrno)){
			print("#A%d: model %#.2x %#.2x; not SB 16 compatible\n",
				adev->ctlrno, ctlr->major, ctlr->minor);
			iofree(sbconf.port);
			iofree(sbconf.port+0x100);
			return -1;
		}
		ctlr->major = 4;
	}

	/*
	 * initialize the mixer
	 */
	mxcmd(blaster, 0x00, 0);			/* Reset mixer */
	mxvolume(ctlr);

	/* set irq */
	for(i=0; i<nelem(irq); i++){
		if(sbconf.irq == irq[i]){
			mxcmd(blaster, 0x80, 1<<i);
			break;
		}
	}
	x = mxread(blaster, 0x80);
	for(i=0; i<nelem(irq); i++){
		if(x & (1<<i)){
			sbconf.irq = irq[i];
			break;
		}
	}

	for(;;){
		/* set 16bit dma */
		if(blaster->dma>=5 && blaster->dma<=7){
			x = mxread(blaster, 0x81);
			mxcmd(blaster, 0x81, (1<<blaster->dma) & 0xF0 | (x & 0x0F));
		}
		x = mxread(blaster, 0x81);
		for(i=5; i<=7; i++){
			if(x & (1<<i)){
				blaster->dma = i;
				break;
			}
		}
		if(blaster->dma<5){
			blaster->dma = 7;
			continue;
		}
		break;
	}

	print("#A%d: %s port 0x%04lux irq %d dma %d\n", adev->ctlrno, sbconf.type,
		sbconf.port, sbconf.irq, blaster->dma);

	ctlr->ring.nbuf = Blocks*Blocksize;
	if(dmainit(blaster->dma, ctlr->ring.nbuf)){
		free(ctlr);
		return -1;
	}
	ctlr->ring.buf = dmabva(blaster->dma);

	intrenable(sbconf.irq, audiointr, adev, BUSUNKNOWN, sbconf.type);

	setempty(ctlr);
	mxvolume(ctlr);

	adev->write = audiowrite;
	adev->close = audioclose;
	adev->status = audiostatus;
	adev->buffered = audiobuffered;

	return 0;
}

void
audiosb16link(void)
{
	addaudiocard("sb16", audioprobe);
	addaudiocard("ess1688", audioprobe);
}
