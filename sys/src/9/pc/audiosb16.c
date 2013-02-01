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
#include "../port/audioif.h"

typedef struct	Ring	Ring;
typedef struct	Blaster	Blaster;
typedef struct	Ctlr	Ctlr;

enum
{
	Vmaster,
	Vaudio,
	Vsynth,
	Vcd,
	Vline,
	Vmic,
	Vspeaker,
	Vtreb,
	Vbass,
	Vigain,
	Vogain,
	Vspeed,
	Vdelay,
	Nvol,

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

	void	(*startdma)(Ctlr*);
	void	(*intr)(Ctlr*);
};

struct Ctlr
{
	Rendez	vous;
	int	active;		/* boolean dma running */
	int	major;		/* SB16 major version number (sb 4) */
	int	minor;		/* SB16 minor version number */
	Ring	ring;		/* dma ring buffer */
	Blaster	blaster;

	int	lvol[Nvol];
	int	rvol[Nvol];

	/* for probe */
	Audio	*adev;
	ISAConf conf;
	Ctlr *next;
};

static Volume voltab[] = {
	[Vmaster] "master", 0x30, 0xff, Stereo, 0,
	[Vaudio] "audio", 0x32, 0xff, Stereo, 0,
	[Vsynth] "synth", 0x34, 0xff, Stereo, 0,
	[Vcd] "cd", 0x36, 0xff, Stereo, 0,
	[Vline] "line", 0x38, 0xff, Stereo, 0,
	[Vmic] "mic", 0x3a, 0xff, Mono, 0,
	[Vspeaker] "speaker", 0x3b, 0xff, Mono, 0,
	[Vtreb] "treb", 0x44, 0xff, Stereo, 0,
	[Vbass] "bass", 0x46, 0xff, Stereo, 0,
	[Vigain] "recgain", 0x3f, 0xff, Stereo, 0,
	[Vogain] "outgain", 0x41, 0xff, Stereo, 0,
	[Vspeed] "speed", 0, 0, Absolute, 0,
	[Vdelay] "delay", 0, 0, Absolute, 0,
	0,
};

static	char	Emajor[]	= "soundblaster not responding/wrong version";

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

static int
mxsetvol(Audio *adev, int x, int a[2])
{
	Blaster *blaster;
	Ctlr *ctlr = adev->ctlr;
	Volume *vol;

	vol = voltab+x;
	blaster = &ctlr->blaster;
	ilock(blaster);
	switch(vol->type){
	case Absolute:
		switch(x){
		case Vdelay:
			adev->delay = a[0];
			break;
		case Vspeed:
			adev->speed = a[0];
			break;
		}
		ctlr->lvol[x] = ctlr->rvol[x] = a[0];
		break;
	case Stereo:
		ctlr->rvol[x] = a[1];
		mxcmd(blaster, vol->reg+1, a[1]);
		/* no break */
	case Mono:
		ctlr->lvol[x] = a[0];
		mxcmd(blaster, vol->reg, a[0]);
	}
	iunlock(blaster);

	return 0;
}

static int
mxgetvol(Audio *adev, int x, int a[2])
{
	Ctlr *ctlr = adev->ctlr;

	a[0] = ctlr->lvol[x];
	a[1] = ctlr->rvol[x];

	return 0;
}

static	void
contindma(Ctlr *ctlr)
{
	Blaster *blaster;
	Ring *ring;

	blaster = &ctlr->blaster;
	ring = &ctlr->ring;
	if(buffered(ring) >= Blocksize)
		ring->ri = ring->nbuf - dmacount(ctlr->conf.dma);
	else{
		dmaend(ctlr->conf.dma);
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
	dmaend(ctlr->conf.dma);
	if(0)
		sbcmd(blaster, 0x42);	/* input sampling rate */
	else
		sbcmd(blaster, 0x41);	/* output sampling rate */
	speed = ctlr->adev->speed;
	sbcmd(blaster, speed>>8);
	sbcmd(blaster, speed);

	if(0)
		sbcmd(blaster, 0xbe);	/* A/D, autoinit */
	else
		sbcmd(blaster, 0xb6);	/* D/A, autoinit */

	sbcmd(blaster, 0x30);	/* stereo, signed 16 bit */

	count = (Blocksize>>1) - 1;
	sbcmd(blaster, count);
	sbcmd(blaster, count>>8);

	ctlr->active = 1;
	if(dmasetup(ctlr->conf.dma, ring->buf, ring->nbuf, DMAWRITE|DMALOOP) < 0){
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
	delay(1);	/* >3 υs */
	outb(blaster->reset, 0);
	delay(1);

	i = sbread(blaster);
	if(i != 0xAA) {
		print("#A%d: no response %#.2x\n", ctlrno, i);
		return 1;
	}

	if(sbcmd(blaster, 0xC6)){	/* extended mode */
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
	dmaend(ctlr->conf.dma);

	ess1688reset(blaster, ctlr->adev->ctlrno);

	/*
	 * Set the speed.
	 */
	speed = ctlr->adev->speed;
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
		ess1688w(blaster, 0xB8, 0x0E);	/* A/D, autoinit */
	else
		ess1688w(blaster, 0xB8, 0x04);	/* D/A, autoinit */
	x = ess1688r(blaster, 0xA8) & ~0x03;
	ess1688w(blaster, 0xA8, x|0x01);	/* 2 channels */
	ess1688w(blaster, 0xB9, 2);	/* demand mode, 4 bytes per request */

	if(1)
		ess1688w(blaster, 0xB6, 0);	/* for output */

	ess1688w(blaster, 0xB7, 0x71);
	ess1688w(blaster, 0xB7, 0xBC);

	x = ess1688r(blaster, 0xB1) & 0x0F;
	ess1688w(blaster, 0xB1, x|0x50);
	x = ess1688r(blaster, 0xB2) & 0x0F;
	ess1688w(blaster, 0xB2, x|0x50);

	if(1)
		sbcmd(blaster, 0xD1);	/* speaker on */

	count = -Blocksize;
	ess1688w(blaster, 0xA4, count & 0xFF);
	ess1688w(blaster, 0xA5, (count>>8) & 0xFF);
	x = ess1688r(blaster, 0xB8);
	ess1688w(blaster, 0xB8, x|0x05);

	ctlr->active = 1;
	if(dmasetup(ctlr->conf.dma, ring->buf, ring->nbuf, DMAWRITE|DMALOOP) < 0){
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
	iunlock(&ctlr->blaster);
}

static long
audiobuffered(Audio *adev)
{
	return buffered(&((Ctlr*)adev->ctlr)->ring);
}

static long
audiostatus(Audio *adev, void *a, long n, vlong)
{
	Ctlr *ctlr = adev->ctlr;
	return snprint((char*)a, n, "bufsize %6d buffered %6ld\n",
		Blocksize, buffered(&ctlr->ring));
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

static int
ratebuf(void *arg)
{
	Ctlr *ctlr = arg;
	int delay = ctlr->adev->delay*4;
	return (delay <= 0) || (buffered(&ctlr->ring) <= delay) || inactive(ctlr);
}

static long
audiowrite(Audio *adev, void *vp, long n, vlong)
{
	uchar *p, *e;
	Ctlr *ctlr;
	Ring *ring;

	p = vp;
	e = p + n;
	ctlr = adev->ctlr;
	ring = &ctlr->ring;
	while(p < e) {
		if((n = writering(ring, p, e - p)) <= 0){
			if(!ctlr->active && ring->ri == 0)
				ctlr->blaster.startdma(ctlr);
			if(!ctlr->active)
				setempty(ctlr);
			else
				sleep(&ctlr->vous, anybuf, ctlr);
		}
		p += n;
	}
	while(ratebuf(ctlr) == 0)
		sleep(&ctlr->vous, ratebuf, ctlr);
	return p - (uchar*)vp;
}

static void
audioclose(Audio *adev, int mode)
{
	Ctlr *ctlr;

	if(mode == OREAD)
		return;
	ctlr = adev->ctlr;
	sleep(&ctlr->vous, inactive, ctlr);
	setempty(ctlr);
}

static long
audiovolread(Audio *adev, void *a, long n, vlong)
{
	return genaudiovolread(adev, a, n, 0, voltab, mxgetvol, 0);
}

static long
audiovolwrite(Audio *adev, void *a, long n, vlong)
{
	Blaster *blaster;
	Ctlr *ctlr;
	int source;

	ctlr = adev->ctlr;
	blaster = &ctlr->blaster;

	n = genaudiovolwrite(adev, a, n, 0, voltab, mxsetvol, 0);

	source = 0;
	if(ctlr->lvol[Vsynth])
		source |= 1<<6;
	if(ctlr->rvol[Vsynth])
		source |= 1<<5;
	if(ctlr->lvol[Vaudio])
		source |= 1<<4;
	if(ctlr->rvol[Vaudio])
		source |= 1<<3;
	if(ctlr->lvol[Vcd])
		source |= 1<<2;
	if(ctlr->rvol[Vcd])
		source |= 1<<1;
	if(ctlr->lvol[Vmic])
		source |= 1<<0;

	ilock(blaster);
	mxcmd(blaster, 0x3c, source);	/* output switch */
	mxcmd(blaster, 0x3d, source);	/* input left switch */
	mxcmd(blaster, 0x3e, source);	/* input right switch */
	iunlock(blaster);

	return n;
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
		print("#A%d: model %#.2x %#.2x; not ESS1688 compatible\n",
			ctlrno, major, minor);
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
	static Ctlr *cards = nil;

	Ctlr *ctlr;
	Blaster *blaster;
	int i, x;

	/* make a list of audio isa cards if not already done */
	if(cards == nil){
		for(i=0; i<nelem(irq); i++){
			ctlr = mallocz(sizeof(Ctlr), 1);
			if(ctlr == nil){
				print("sb16: can't allocate memory\n");
				break;
			}
			ctlr->conf.port = 0x220 + i*0x10;
			ctlr->conf.irq = irq[i];
			ctlr->conf.dma = 0;
			if(isaconfig("audio", i, &ctlr->conf) == 0){
				free(ctlr);
				break;
			}
			ctlr->next = cards;
			cards = ctlr;
		}
	}

	/* pick a card */
	for(ctlr = cards; ctlr; ctlr = ctlr->next){
		if(ctlr->conf.type && strcmp(adev->name, ctlr->conf.type) == 0){
			ctlr->conf.type = nil;
			goto Found;
		}
	}
	return -1;

Found:
	switch(ctlr->conf.port){
	case 0x220:
	case 0x240:
	case 0x260:
	case 0x280:
		break;
	default:
		print("#A%d: bad port %#lux\n", adev->ctlrno, ctlr->conf.port);
		return -1;
	}

	if(ioalloc(ctlr->conf.port, 0x10, 0, "audio") < 0){
		print("#A%d: cannot ioalloc range %lux+0x10\n",
			adev->ctlrno, ctlr->conf.port);
		return -1;
	}
	if(ioalloc(ctlr->conf.port+0x100, 1, 0, "audio.mpu401") < 0){
		iofree(ctlr->conf.port);
		print("#A%d: cannot ioalloc range %lux+0x01\n",
			adev->ctlrno, ctlr->conf.port+0x100);
		return -1;
	}

	ctlr->adev = adev;
	adev->ctlr = ctlr;

	blaster = &ctlr->blaster;
	blaster->reset = ctlr->conf.port + 0x6;
	blaster->read = ctlr->conf.port + 0xa;
	blaster->write = ctlr->conf.port + 0xc;
	blaster->wstatus = ctlr->conf.port + 0xc;
	blaster->rstatus = ctlr->conf.port + 0xe;
	blaster->mixaddr = ctlr->conf.port + 0x4;
	blaster->mixdata = ctlr->conf.port + 0x5;
	blaster->clri8 = ctlr->conf.port + 0xe;
	blaster->clri16 = ctlr->conf.port + 0xf;
	blaster->clri401 = ctlr->conf.port + 0x100;

	blaster->startdma = sb16startdma;
	blaster->intr = sb16intr;

	outb(blaster->reset, 1);
	delay(1);			/* >3 υs */
	outb(blaster->reset, 0);
	delay(1);

	i = sbread(blaster);
	if(i != 0xaa) {
		print("#A%d: no response #%.2x\n", adev->ctlrno, i);
Errout:
		iofree(ctlr->conf.port);
		iofree(ctlr->conf.port+0x100);
		return -1;
	}

	sbcmd(blaster, 0xe1);			/* get version */
	ctlr->major = sbread(blaster);
	ctlr->minor = sbread(blaster);

	if(ctlr->major != 4) {
		if(ctlr->major != 3 || ctlr->minor != 1 ||
			ess1688(&ctlr->conf, blaster, adev->ctlrno)){
			print("#A%d: model %#.2x %#.2x; not SB 16 compatible\n",
				adev->ctlrno, ctlr->major, ctlr->minor);
			goto Errout;
		}
		ctlr->major = 4;
	}

	/*
	 * initialize the mixer
	 */
	mxcmd(blaster, 0x00, 0);			/* Reset mixer */

	for(i=0; i<Nvol; i++){
		int a[2];

		a[0] = 0;
		a[1] = 0;
		mxsetvol(adev, i, a);
	}

	/* set irq */
	for(i=0; i<nelem(irq); i++){
		if(ctlr->conf.irq == irq[i]){
			mxcmd(blaster, 0x80, 1<<i);
			break;
		}
	}
	x = mxread(blaster, 0x80);
	for(i=0; i<nelem(irq); i++){
		if(x & (1<<i)){
			ctlr->conf.irq = irq[i];
			break;
		}
	}

	for(;;){
		/* set 16bit dma */
		if(ctlr->conf.dma>=5 && ctlr->conf.dma<=7){
			x = mxread(blaster, 0x81);
			mxcmd(blaster, 0x81, (1<<ctlr->conf.dma) & 0xF0 | (x & 0x0F));
		}
		x = mxread(blaster, 0x81);
		for(i=5; i<=7; i++){
			if(x & (1<<i)){
				ctlr->conf.dma = i;
				break;
			}
		}
		if(ctlr->conf.dma>=5)
			break;
		ctlr->conf.dma = 7;
	}

	print("#A%d: %s port 0x%04lux irq %d dma %lud\n", adev->ctlrno, adev->name,
		ctlr->conf.port, ctlr->conf.irq, ctlr->conf.dma);

	ctlr->ring.nbuf = Blocks*Blocksize;
	if(dmainit(ctlr->conf.dma, ctlr->ring.nbuf))
		goto Errout;
	ctlr->ring.buf = dmabva(ctlr->conf.dma);
	print("#A%d: %s dma buffer %p-%p\n", adev->ctlrno, adev->name,
		ctlr->ring.buf, ctlr->ring.buf+ctlr->ring.nbuf);

	setempty(ctlr);

	adev->write = audiowrite;
	adev->close = audioclose;
	adev->volread = audiovolread;
	adev->volwrite = audiovolwrite;
	adev->status = audiostatus;
	adev->buffered = audiobuffered;

	intrenable(ctlr->conf.irq, audiointr, adev, BUSUNKNOWN, adev->name);

	return 0;
}

void
audiosb16link(void)
{
	addaudiocard("sb16", audioprobe);
	addaudiocard("ess1688", audioprobe);
}
