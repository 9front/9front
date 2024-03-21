#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/error.h"
#include "../port/audioif.h"

typedef struct Ring Ring;
typedef struct Hwdesc Hwdesc;
typedef struct Ctlr Ctlr;

enum {
	Ioc	=	1<<31,
	Bup	=	1<<30,
};

struct Hwdesc {
	ulong addr;
	ulong size;
};

enum {
	Ndesc = 32,
	Bufsize = 32768,	/* bytes, must be divisible by ndesc */
	Blocksize = Bufsize/Ndesc,
	Maxbusywait = 500000, /* microseconds, roughly */
	BytesPerSample = 4,
};

struct Ring
{
	Rendez r;

	uchar	*buf;
	ulong	nbuf;

	ulong	ri;
	ulong	wi;
};

struct Ctlr {
	/* keep these first, they want to be 8-aligned */
	Hwdesc indesc[Ndesc];
	Hwdesc outdesc[Ndesc];
	Hwdesc micdesc[Ndesc];

	Lock;

	ulong port;
	ulong mixport;
	uchar *mmreg;
	uchar *mmmix;
	int ismmio;

	Ring inring, micring, outring;

	int sis7012;

	/* for probe */
	Audio *adev;
	Pcidev *pcidev;
	Ctlr *next;
};

enum {
	In = 0x00,
	Out = 0x10,
	Mic = 0x20,
		Bar = 0x00,	/* Base address register, 8-byte aligned */
		/* a 32-bit read at 0x04 can be used to get civ:lvi:sr in one step */
		Civ = 0x04,	/* current index value (desc being processed) */
		Lvi = 0x05,	/* Last valid index (index of first unused entry!) */
		Sr = 0x06,	/* status register */
			Fifoe = 1<<4,	/* fifo error (r/wc) */
			Bcis = 1<<3,	/* buffer completion interrupt status (r/wc) */
			Lvbci = 1<<2,	/* last valid buffer completion(in)/fetched(out) interrupt (r/wc) */
			Celv = 1<<1,	/* current equals last valid (ro) */
			Dch = 1<<0,	/* dma controller halted (ro) */
		Picb = 0x08,	/* position in current buffer */
		Piv = 0x0a,	/* prefetched index value */
		Cr = 0x0b,	/* control register */
			Ioce = 1<<4,	/* interrupt on buffer completion (if bit set in hwdesc.size) (rw) */
			Feie = 1<<3,	/* fifo error interrupt enable (rw) */
			Lvbie = 1<<2,	/* last valid buffer interrupt enable (rw) */
			RR = 1<<1,	/* reset busmaster related regs, excl. ioce,feie,lvbie (rw) */
			Rpbm = 1<<0,	/* run/pause busmaster. 0 stops, 1 starts (rw) */
	Cnt = 0x2c,	/* global control */
		Ena16bit = 0x0<<22,
		Ena20bit = 0x1<<22,
		Ena2chan = 0x0<<20,
		Ena4chan = 0x1<<20,
		Enam6chan = 0x2<<20,
		EnaRESER = 0x3<<20,
		Sr2ie = 1<<6,	/* sdin2 interrupt enable (rw) */
		Srie = 1<<5,	/* sdin1 interrupt enable (rw) */
		Prie = 1<<4,	/* sdin0 interrupt enable (rw) */
		Aclso = 1<<3,	/* ac link shut-off (rw) */
		Acwr = 1<<2,	/* ac 97 warm reset (rw) */
		Accr = 1<<1,	/* ac 97 cold reset (rw) */
		GPIie = 1<<0,	/* GPI interrupt enable (rw) */
	Sta = 0x30,			/* global status */
		Cap6chan = 1<<21,
		Cap4chan = 1<<20,
		Md3 = 1<<17,	/* modem powerdown semaphore */
		Ad3 = 1<<16,	/* audio powerdown semaphore */
		Rcs = 1<<15,	/* read completion status (r/wc) */
		S2ri = 1<<29,	/* sdin2 resume interrupt (r/wc) */
		Sri = 1<<11,	/* sdin1 resume interrupt (r/wc) */
		Pri = 1<<10,	/* sdin0 resume interrupt (r/wc) */
		S2cr = 1<<28,	/* sdin2 codec ready (ro) */
		Scr = 1<<9,	/* sdin1 codec ready (ro) */
		Pcr = 1<<8,	/* sdin0 codec ready (ro) */
		Mint = 1<<7,	/* microphone in inetrrupt (ro) */
		Point = 1<<6,	/* pcm out interrupt (ro) */
		Piint = 1<<5,	/* pcm in interrupt (ro) */
		Moint = 1<<2,	/* modem out interrupt (ro) */
		Miint = 1<<1,	/* modem in interrupt (ro) */
		Gsci = 1<<0,	/* GPI status change interrupt */
	Cas = 0x34,	/* codec access semaphore */
		Casp = 1<<0,	/* set to 1 on read if zero, cleared by hardware */
};

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

	m = (r->nbuf - BytesPerSample) - buffered(r);
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

static uchar
csr8r(Ctlr *c, int r){
	if(c->ismmio)
		return *(uchar*)(c->mmreg+r);		
	return inb(c->port+r);
}

static ushort
csr16r(Ctlr *c, int r){
	if(c->ismmio)
		return *(ushort*)(c->mmreg+r);		
	return ins(c->port+r);
}

static ulong
csr32r(Ctlr *c, int r){
	if(c->ismmio)
		return *(ulong*)(c->mmreg+r);		
	return inl(c->port+r);
}

static void
csr8w(Ctlr *c, int r, uchar v){
	if(c->ismmio)
		*(uchar*)(c->mmreg+r) = v;
	else
		outb(c->port+r, (int)v);
}

static void
csr16w(Ctlr *c, int r, ushort v){
	if(c->ismmio)
		*(ushort*)(c->mmreg+r) = v;
	else
		outs(c->port+r, v);
}

static void
csr32w(Ctlr *c, int r, ulong v){
	if(c->ismmio)
		*(ulong*)(c->mmreg+r) = v;
	else
		outl(c->port+r, v);
}

/* audioac97mix */
extern void ac97mixreset(Audio *,
	void (*wr)(Audio*,int,ushort), 
	ushort (*rr)(Audio*,int));

static void
ac97waitcodec(Audio *adev)
{
	Ctlr *ctlr;
	int i;
	ctlr = adev->ctlr;
	for(i = 0; i <= Maxbusywait/10; i++){
		if((csr8r(ctlr, Cas) & Casp) == 0)
			return;
		microdelay(10);
	}
	print("#A%d: ac97 exhausted waiting codec access\n", adev->ctlrno);
}

static void
ac97mixw(Audio *adev, int port, ushort val)
{
	Ctlr *ctlr;
	ac97waitcodec(adev);
	ctlr = adev->ctlr;
	if(ctlr->ismmio)
		*(ushort*)(ctlr->mmmix+port) = val;
	else
		outs(ctlr->mixport+port, val);
}

static ushort
ac97mixr(Audio *adev, int port)
{
	Ctlr *ctlr;
	ac97waitcodec(adev);
	ctlr = adev->ctlr;
	if(ctlr->ismmio)
		return *(ushort*)(ctlr->mmmix+port);
	return ins(ctlr->mixport+port);
}

static void
ac97interrupt(Ureg *, void *arg)
{
	Audio *adev;
	Ctlr *ctlr;
	ulong stat;

	adev = arg;
	ctlr = adev->ctlr;
	if(ctlr == nil || ctlr->adev != adev)
		return;

	stat = csr32r(ctlr, Sta);
	stat &= S2ri | Sri | Pri | Mint | Point | Piint | Moint | Miint | Gsci;
	if(stat & (Point|Piint|Mint)){
		ilock(ctlr);
		if(stat & Point){
			ctlr->outring.ri = csr8r(ctlr, Out + Civ) * Blocksize;
			wakeup(&ctlr->outring.r);

			if(ctlr->sis7012)
				csr16w(ctlr, Out + Picb, csr16r(ctlr, Out + Picb) & ~Dch);
			else
				csr16w(ctlr, Out + Sr, csr16r(ctlr, Out + Sr) & ~Dch);
			stat &= ~Point;
		}
		if(stat & Piint){
			ctlr->inring.wi = csr8r(ctlr, In + Civ) * Blocksize;
			wakeup(&ctlr->inring.r);

			if(ctlr->sis7012)
				csr16w(ctlr, In + Picb, csr16r(ctlr, In + Picb) & ~Dch);
			else
				csr16w(ctlr, In + Sr, csr16r(ctlr, In + Sr) & ~Dch);
			stat &= ~Piint;
		}
		if(stat & Mint){
			ctlr->micring.wi = csr8r(ctlr, Mic + Civ) * Blocksize;
			wakeup(&ctlr->micring.r);

			if(ctlr->sis7012)
				csr16w(ctlr, Mic + Picb, csr16r(ctlr, Mic + Picb) & ~Dch);
			else
				csr16w(ctlr, Mic + Sr, csr16r(ctlr, Mic + Sr) & ~Dch);
			stat &= ~Mint;
		}
		iunlock(ctlr);
	}
	if(stat) /* have seen 0x400, which is sdin0 resume */
		iprint("#A%d: ac97 unhandled interrupt(s): stat 0x%lux\n",
			adev->ctlrno, stat);
}

static long
ac97buffered(Audio *adev)
{
	Ctlr *ctlr = adev->ctlr;
	return buffered(&ctlr->outring);
}

static long
ac97status(Audio *adev, void *a, long n, vlong)
{
	Ctlr *ctlr = adev->ctlr;
	return snprint((char*)a, n, "bufsize %6d buffered %6ld\n",
		Blocksize, buffered(&ctlr->outring));
}

static int
inavail(void *arg)
{
	Ring *r = arg;
	return buffered(r) > 0;
}

static int
outavail(void *arg)
{
	Ring *r = arg;
	return available(r) > 0;
}

static int
outrate(void *arg)
{
	Ctlr *ctlr = arg;
	int delay = ctlr->adev->delay*BytesPerSample;
	return (delay <= 0) || (buffered(&ctlr->outring) <= delay);
}

static long
ac97read(Audio *adev, void *vp, long n, vlong)
{
	uchar *p, *e;
	Ctlr *ctlr;
	Ring *ring;
	ulong oi, ni;

	p = vp;
	e = p + n;
	ctlr = adev->ctlr;
	ring = &ctlr->inring;
	while(p < e) {
		oi = ring->ri / Blocksize;
		if((n = readring(ring, p, e - p)) <= 0){
			csr8w(ctlr, In + Lvi, (oi - 1) % Ndesc);
			csr8w(ctlr, In + Cr, Ioce | Rpbm);
			sleep(&ring->r, inavail, ring);
			continue;
		}
		ni = ring->ri / Blocksize;
		while(oi != ni){
			csr8w(ctlr, In + Lvi, (oi - 1) % Ndesc);
			csr8w(ctlr, In + Cr, Ioce | Rpbm);
			oi = (oi + 1) % Ndesc;
		}
		p += n;
	}
	return p - (uchar*)vp;
}

static long
ac97write(Audio *adev, void *vp, long n, vlong)
{
	uchar *p, *e;
	Ctlr *ctlr;
	Ring *ring;
	ulong oi, ni;

	p = vp;
	e = p + n;
	ctlr = adev->ctlr;
	ring = &ctlr->outring;
	while(p < e) {
		oi = ring->wi / Blocksize;
		if((n = writering(ring, p, e - p)) <= 0){
			sleep(&ring->r, outavail, ring);
			continue;
		}
		ni = ring->wi / Blocksize;
		while(oi != ni){
			csr8w(ctlr, Out+Lvi, oi);
			csr8w(ctlr, Out+Cr, Ioce | Rpbm);
			oi = (oi + 1) % Ndesc;
		}
		p += n;
	}
	while(outrate(ctlr) == 0)
		sleep(&ring->r, outrate, ctlr);
	return p - (uchar*)vp;
}

static void
ac97close(Audio *adev, int mode)
{
	Ctlr *ctlr;
	Ring *ring;

	if(mode == OREAD)
		return;

	ctlr = adev->ctlr;
	ring = &ctlr->outring;
	while(ring->wi % Blocksize)
		if(writering(ring, (uchar*)"", 1) <= 0)
			break;
}

static Pcidev*
ac97match(Pcidev *p)
{
	/* not all of the matched devices have been tested */
	while(p = pcimatch(p, 0, 0))
		switch((p->vid<<16)|p->did){
		case (0x1022<<16)|0x7445:
		case (0x1022<<16)|0x746d:
		case (0x1039<<16)|0x7012:
		case (0x10de<<16)|0x006a:
		case (0x10de<<16)|0x00da:
		case (0x10de<<16)|0x00ea:
		case (0x10de<<16)|0x01b1:
		case (0x8086<<16)|0x2415:
		case (0x8086<<16)|0x2425:
		case (0x8086<<16)|0x2445:
		case (0x8086<<16)|0x2485:
		case (0x8086<<16)|0x24c5:
		case (0x8086<<16)|0x24d5:
		case (0x8086<<16)|0x25a6:
		case (0x8086<<16)|0x266e:
		case (0x8086<<16)|0x27de:
		case (0x8086<<16)|0x7195:
			return p;
		}
	return nil;
}

static void
sethwp(Ctlr *ctlr, long off, void *ptr)
{
	csr8w(ctlr, off+Cr, RR);
	csr32w(ctlr, off+Bar, PCIWADDR(ptr));
	csr8w(ctlr, off+Lvi, 0);
}

static int
ac97reset1(Audio *adev, Ctlr *ctlr)
{
	int i, irq, tbdf;
	ulong ctl, stat = 0;
	Pcidev *p;

	p = ctlr->pcidev;

	/* ICH4 through ICH7 may use memory-type base address registers */
	if(p->vid == 0x8086 &&
	  (p->did == 0x24c5 || p->did == 0x24d5 || p->did == 0x266e || p->did == 0x27de) &&
	  (p->mem[2].bar != 0 && p->mem[3].bar != 0) &&
	  ((p->mem[2].bar & 1) == 0 && (p->mem[3].bar & 1) == 0)){
		ctlr->mmmix = vmap(p->mem[2].bar & ~0xF, p->mem[2].size);
		if(ctlr->mmmix == nil){
			print("ac97: vmap failed for mmmix %llux\n", p->mem[2].bar & ~0xF);
			return -1;
		}
		ctlr->mmreg = vmap(p->mem[3].bar & ~0xF, p->mem[3].size);
		if(ctlr->mmreg == nil){
			print("ac97: vmap failed for mmreg %llux\n", p->mem[3].bar & ~0xF);
			vunmap(ctlr->mmmix, p->mem[2].size);
			return -1;
		}
		ctlr->ismmio = 1;
	}else{
		if((p->mem[0].bar & 1) == 0 || (p->mem[1].bar & 1) == 0){
			print("ac97: not i/o regions 0x%04llux 0x%04llux\n", p->mem[0].bar, p->mem[1].bar);
			return -1;
		}

		if(p->vid == 0x1039 && p->did == 0x7012){
			ctlr->sis7012 = 1;	/* i/o bars swapped? */
		}

		ctlr->port = p->mem[1].bar & ~3;
		if(ioalloc(ctlr->port, p->mem[1].size, 0, "ac97") < 0){
			print("ac97: ioalloc failed for port 0x%04lux\n", ctlr->port);
			return -1;
		}
		ctlr->mixport = p->mem[0].bar & ~3;
		if(ioalloc(ctlr->mixport, p->mem[0].size, 0, "ac97mix") < 0){
			print("ac97: ioalloc failed for mixport 0x%04lux\n", ctlr->mixport);
			iofree(ctlr->port);
			return -1;
		}
	}

	irq = p->intl;
	tbdf = p->tbdf;

	adev->ctlr = ctlr;

	print("#A%d: ac97 port 0x%04lux mixport 0x%04lux irq %d\n",
		adev->ctlrno, ctlr->port, ctlr->mixport, irq);

	pcisetbme(p);
	pcisetioe(p);

	ctlr->micring.buf = xspanalloc(Bufsize, 8, 0);
	ctlr->micring.nbuf = Bufsize;
	ctlr->micring.ri = 0;
	ctlr->micring.wi = 0;

	ctlr->inring.buf = xspanalloc(Bufsize, 8, 0);
	ctlr->inring.nbuf = Bufsize;
	ctlr->inring.ri = 0;
	ctlr->inring.wi = 0;

	ctlr->outring.buf = xspanalloc(Bufsize, 8, 0);
	ctlr->outring.nbuf = Bufsize;
	ctlr->outring.ri = 0;
	ctlr->outring.wi = 0;

	for(i = 0; i < Ndesc; i++){
		int size, off = i * Blocksize;
		
		if(ctlr->sis7012)
			size = Blocksize;
		else
			size = Blocksize / 2;
		ctlr->micdesc[i].addr = PCIWADDR(ctlr->micring.buf + off);
		ctlr->micdesc[i].size = Ioc | size;
		ctlr->indesc[i].addr = PCIWADDR(ctlr->inring.buf + off);
		ctlr->indesc[i].size = Ioc | size;
		ctlr->outdesc[i].addr = PCIWADDR(ctlr->outring.buf + off);
		ctlr->outdesc[i].size = Ioc | size;
	}

	ctl = csr32r(ctlr, Cnt);
	ctl &= ~(EnaRESER | Aclso);

	if((ctl & Accr) == 0){
		print("#A%d: ac97 cold reset\n", adev->ctlrno);
		ctl |= Accr;
	}else{
		print("#A%d: ac97 warm reset\n", adev->ctlrno);
		ctl |= Acwr;
	}

	csr32w(ctlr, Cnt, ctl);
	for(i = 0; i < Maxbusywait; i++){
		if((csr32r(ctlr, Cnt) & Acwr) == 0)
			break;
		microdelay(1);
	}
	if(i == Maxbusywait)
		print("#A%d: ac97 gave up waiting Acwr to go down\n", adev->ctlrno);

	for(i = 0; i < Maxbusywait; i++){
		if((stat = csr32r(ctlr, Sta)) & (Pcr | Scr | S2cr))
			break;
		microdelay(1);
	}
	if(i == Maxbusywait)
		print("#A%d: ac97 gave up waiting codecs become ready\n", adev->ctlrno);

	print("#A%d: ac97 codecs ready:%s%s%s\n", adev->ctlrno,
		(stat & Pcr) ? " sdin0" : "",
		(stat & Scr) ? " sdin1" : "",
		(stat & S2cr) ? " sdin2" : "");

	print("#A%d: ac97 codecs resumed:%s%s%s\n", adev->ctlrno,
		(stat & Pri) ? " sdin0" : "",
		(stat & Sri) ? " sdin1" : "",
		(stat & S2ri) ? " sdin2" : "");

	sethwp(ctlr, In, ctlr->indesc);
	sethwp(ctlr, Out, ctlr->outdesc);
	sethwp(ctlr, Mic, ctlr->micdesc);

	csr8w(ctlr, In+Cr, Ioce);	/*  | Lvbie | Feie */
	csr8w(ctlr, Out+Cr, Ioce);	/*  | Lvbie | Feie */
	csr8w(ctlr, Mic+Cr, Ioce);	/*  | Lvbie | Feie */

	ac97mixreset(adev, ac97mixw, ac97mixr);

	adev->read = ac97read;
	adev->write = ac97write;
	adev->close = ac97close;
	adev->buffered = ac97buffered;
	adev->status = ac97status;

	intrenable(irq, ac97interrupt, adev, tbdf, adev->name);

	return 0;
}

static int
ac97reset(Audio *adev)
{
	static Ctlr *cards = nil;
	Ctlr *ctlr;
	Pcidev *p;

	/* make a list of all ac97 cards if not already done */
	if(cards == nil){
		p = nil;
		while((p = ac97match(p)) != nil){
			ctlr = mallocz(sizeof(Ctlr), 1);
			if(ctlr == nil){
				print("ac97: can't allocate memory\n");
				break;
			}
			ctlr->pcidev = p;
			ctlr->next = cards;
			cards = ctlr;
		}
	}

	/* pick a card from the list */
	for(ctlr = cards; ctlr; ctlr = ctlr->next){
		if(ctlr->adev == nil && ctlr->pcidev != nil){
			ctlr->adev = adev;
			pcienable(ctlr->pcidev);
			if(ac97reset1(adev, ctlr) == 0)
				return 0;
			pcidisable(ctlr->pcidev);
			ctlr->pcidev = nil;
			ctlr->adev = nil;
		}
	}
	return -1;
}

void
audioac97link(void)
{
	addaudiocard("ac97", ac97reset);
}
