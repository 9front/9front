#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

typedef struct Hwdesc Hwdesc;
typedef struct Ctlr Ctlr;
static uint sis7012 = 0;

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
	Nts = 33,
	Bufsize = 32768,	/* bytes, must be divisible by ndesc */
	Maxbusywait = 500000, /* microseconds, roughly */
	BytesPerSample = 4,
};

struct Ctlr {
	/* keep these first, they want to be 8-aligned */
	Hwdesc indesc[Ndesc];
	Hwdesc outdesc[Ndesc];
	Hwdesc micdesc[Ndesc];

	Lock;
	Rendez outr;

	ulong port;
	ulong mixport;

	char *out;
	char *in;
	char *mic;

	char *outp;
	char *inp;
	char *micp;

	/* shared variables, ilock to access */
	int outavail;
	int inavail;
	int micavail;

	/* interrupt handler alone */
	int outciv;
	int inciv;
	int micciv;

	int tsouti;
	uvlong tsoutp;
	ulong tsout[Nts];
	int tsoutb[Nts];

	ulong civstat[Ndesc];
	ulong lvistat[Ndesc];

	int targetrate;
	int hardrate;

	int attachok;

	/* for probe */
	Pcidev *pcidev;
	Ctlr *next;
};

#define iorl(c, r)	(inl((c)->port+(r)))
#define iowl(c, r, l)	(outl((c)->port+(r), (ulong)(l)))

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

#define csr8r(c, r)	(inb((c)->port+(r)))
#define csr16r(c, r)	(ins((c)->port+(r)))
#define csr32r(c, r)	(inl((c)->port+(r)))
#define csr8w(c, r, b)	(outb((c)->port+(r), (int)(b)))
#define csr16w(c, r, w)	(outs((c)->port+(r), (ushort)(w)))
#define csr32w(c, r, w)	(outl((c)->port+(r), (ulong)(w)))

/* audioac97mix */
extern int ac97hardrate(Audio *, int);
extern void ac97mixreset(Audio *, void (*wr)(Audio*,int,ushort), 
	ushort (*rr)(Audio*,int));

static void
ac97waitcodec(Audio *adev)
{
	Ctlr *ctlr;
	int i;
	ctlr = adev->ctlr;
	for(i = 0; i < Maxbusywait/10; i++){
		if((csr8r(ctlr, Cas) & Casp) == 0)
			break;
		microdelay(10);
	}
	if(i == Maxbusywait)
		print("#A%d: ac97 exhausted waiting codec access\n", adev->ctlrno);
}

static void
ac97mixw(Audio *adev, int port, ushort val)
{
	Ctlr *ctlr;
	ac97waitcodec(adev);
	ctlr = adev->ctlr;
	outs(ctlr->mixport+port, val);
}

static ushort
ac97mixr(Audio *adev, int port)
{
	Ctlr *ctlr;
	ac97waitcodec(adev);
	ctlr = adev->ctlr;
	return ins(ctlr->mixport+port);
}

static int
outavail(void *arg)
{
	Ctlr *ctlr;
	ctlr = arg;
	return ctlr->outavail > 0;
}

static void
ac97interrupt(Ureg *, void *arg)
{
	Audio *adev;
	Ctlr *ctlr;
	int civ, n, i;
	ulong stat;
	uvlong now;

	adev = arg;
	ctlr = adev->ctlr;
	stat = csr32r(ctlr, Sta);

	stat &= S2ri | Sri | Pri | Mint | Point | Piint | Moint | Miint | Gsci;

	ilock(ctlr);
	if(stat & Point){
		if(sis7012)
			csr16w(ctlr, Out + Picb, csr16r(ctlr, Out + Picb) & ~Dch);
		else
			csr16w(ctlr, Out + Sr, csr16r(ctlr, Out + Sr) & ~Dch);
		
		civ = csr8r(ctlr, Out + Civ);
		n = 0;
		while(ctlr->outciv != civ){
			ctlr->civstat[ctlr->outciv++]++;
			if(ctlr->outciv == Ndesc)
				ctlr->outciv = 0;
			n += Bufsize/Ndesc;
		}

		now = fastticks(0);
		i = ctlr->tsouti;
		ctlr->tsoutb[i] = n;
		ctlr->tsout[i] = now - ctlr->tsoutp;
		ctlr->tsouti = (i + 1) % Nts;
		ctlr->tsoutp = now;
		ctlr->outavail += n;
		
		if(ctlr->outavail > Bufsize/2)
			wakeup(&ctlr->outr);
		stat &= ~Point;	
	}
	iunlock(ctlr);
	if(stat) /* have seen 0x400, which is sdin0 resume */
		print("#A%d: ac97 unhandled interrupt(s): stat 0x%lux\n", adev->ctlrno, stat);
}

static int
off2lvi(char *base, char *p)
{
	int lvi;
	lvi = p - base;
	return lvi / (Bufsize/Ndesc);
}

static long
ac97medianoutrate(Audio *adev)
{
	ulong ts[Nts], t;
	uvlong hz;
	int i, j;
	Ctlr *ctlr;
	ctlr = adev->ctlr;
	fastticks(&hz);
	for(i = 0; i < Nts; i++)
		if(ctlr->tsout[i] > 0)
			ts[i] = (ctlr->tsoutb[i] * hz) / ctlr->tsout[i];
		else
			ts[i] = 0;
	for(i = 1; i < Nts; i++){
		t = ts[i];
		j = i;
		while(j > 0 && ts[j-1] > t){
			ts[j] = ts[j-1];
			j--;
		}
		ts[j] = t;
	}
	return ts[Nts/2] / BytesPerSample;
}

static void
ac97volume(Audio *adev, char *msg)
{
	adev->volwrite(adev, msg, strlen(msg), 0);
}

static void
ac97attach(Audio *adev)
{
	Ctlr *ctlr;
	ctlr = adev->ctlr;
	if(!ctlr->attachok){
		ac97hardrate(adev, ctlr->hardrate);
		ac97volume(adev, "audio 75");
		ac97volume(adev, "head 100");
		ac97volume(adev, "master 100");
		ctlr->attachok = 1;
	}
}

static long
ac97status(Audio *adev, void *a, long n, vlong off)
{
	Ctlr *ctlr;
	char *buf;
	long i, l;
	ctlr = adev->ctlr;
	l = 0;
	buf = malloc(READSTR);
	l += snprint(buf + l, READSTR - l, "rate %d\n", ctlr->targetrate);
	l += snprint(buf + l, READSTR - l, "median rate %lud\n", ac97medianoutrate(adev));
	l += snprint(buf + l, READSTR - l, "hard rate %d\n", ac97hardrate(adev, -1));

	l += snprint(buf + l, READSTR - l, "civ stats");
	for(i = 0; i < Ndesc; i++)
		l += snprint(buf + l, READSTR - l, " %lud", ctlr->civstat[i]);
	l += snprint(buf + l, READSTR - l, "\n");

	l += snprint(buf + l, READSTR - l, "lvi stats");
	for(i = 0; i < Ndesc; i++)
		l += snprint(buf + l, READSTR - l, " %lud", ctlr->lvistat[i]);
	snprint(buf + l, READSTR - l, "\n");

	n = readstr(off, a, n, buf);
	free(buf);
	return n;
}

static long
ac97buffered(Audio *adev)
{
	Ctlr *ctlr;
	ctlr = adev->ctlr;
	return Bufsize - Bufsize/Ndesc - ctlr->outavail;
}

static long
ac97ctl(Audio *adev, void *a, long n, vlong)
{
	Ctlr *ctlr;
	char *tok[2], *p;
	int ntok;
	long t;

	ctlr = adev->ctlr;
	if(n > READSTR)
		n = READSTR - 1;
	p = malloc(READSTR);

	if(waserror()){
		free(p);
		nexterror();
	}
	memmove(p, a, n);
	p[n] = 0;
	ntok = tokenize(p, tok, nelem(tok));
	if(ntok > 1 && !strcmp(tok[0], "rate")){
		t = strtol(tok[1], 0, 10);
		if(t < 8000 || t > 48000)
			error("rate must be between 8000 and 48000");
		ctlr->targetrate = t;
		ctlr->hardrate = t;
		ac97hardrate(adev, ctlr->hardrate);
		poperror();
		free(p);
		return n;
	}
	error("invalid ctl");
	return n; /* shut up, you compiler you */
}

static void
ac97kick(Ctlr *ctlr, long reg)
{
	csr8w(ctlr, reg+Cr, Ioce | Rpbm);
}

static long
ac97write(Audio *adev, void *a, long nwr, vlong)
{
	Ctlr *ctlr;
	char *p, *sp, *ep;
	int len, lvi, olvi;
	int t;
	long n;

	ctlr = adev->ctlr;
	ilock(ctlr);
	p = ctlr->outp;
	sp = a;
	ep = ctlr->out + Bufsize;
	olvi = off2lvi(ctlr->out, p);
	n = nwr;
	while(n > 0){
		len = ep - p;
		if(ctlr->outavail < len)
			len = ctlr->outavail;
		if(n < len)
			len = n;
		ctlr->outavail -= len;
		iunlock(ctlr);
		memmove(p, sp, len);
		ilock(ctlr);
		p += len;
		sp += len;
		n -= len;
		if(p == ep)
			p = ctlr->out;
		lvi = off2lvi(ctlr->out, p);
		if(olvi != lvi){
			t = olvi;
			while(t != lvi){
				t = (t + 1) % Ndesc;
				ctlr->lvistat[t]++;
				csr8w(ctlr, Out+Lvi, t);
				ac97kick(ctlr, Out);
			}
			olvi = lvi;
		}
		if(ctlr->outavail == 0){
			ctlr->outp = p;
			iunlock(ctlr);
			sleep(&ctlr->outr, outavail, ctlr);
			ilock(ctlr);
		}
	}
	ctlr->outp = p;
	iunlock(ctlr);
	return nwr;
}

static Pcidev*
ac97match(Pcidev *p)
{
	/* not all of the matched devices have been tested */
	while(p = pcimatch(p, 0, 0))
		switch(p->vid){
		default:
			break;
		case 0x1039:
			switch(p->did){
			default:
				break;
			case 0x7012:
				sis7012 = 1;
				return p;
			}
		case 0x1022:
			switch(p->did){
			default:
				break;
			case 0x746d:
			case 0x7445:
				return p;
			}
		case 0x10de:
			switch(p->did){
			default:
				break;
			case 0x01b1:
			case 0x006a:
			case 0x00da:
			case 0x00ea:
				return p;
			}
		case 0x8086:
			switch(p->did){
			default:
				break;
			case 0x2415:
			case 0x2425:
			case 0x2445:
			case 0x2485:
			case 0x24c5:
			case 0x24d5:
			case 0x25a6:
			case 0x266e:
			case 0x7195:
				return p;
			}
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
ac97reset(Audio *adev)
{
	static Ctlr *cards = nil;
	Pcidev *p;
	int i, irq, tbdf;
	Ctlr *ctlr;
	ulong ctl, stat = 0;

	/* make a list of all ac97 cards if not already done */
	if(cards == nil){
		p = nil;
		while(p = ac97match(p)){
			ctlr = xspanalloc(sizeof(Ctlr), 8, 0);
			memset(ctlr, 0, sizeof(Ctlr));
			ctlr->pcidev = p;
			ctlr->next = cards;
			cards = ctlr;
		}
	}

	/* pick a card from the list */
	for(ctlr = cards; ctlr; ctlr = ctlr->next){
		if(p = ctlr->pcidev){
			ctlr->pcidev = nil;
			goto Found;
		}
	}
	return -1;

Found:
	adev->ctlr = ctlr;
	ctlr->targetrate = 44100;
	ctlr->hardrate = 44100;

	if(p->mem[0].size == 64){
		ctlr->port = p->mem[0].bar & ~3;
		ctlr->mixport = p->mem[1].bar & ~3;
	} else if(p->mem[1].size == 64){
		ctlr->port = p->mem[1].bar & ~3;
		ctlr->mixport = p->mem[0].bar & ~3;
	} else if(p->mem[0].size == 256){			/* sis7012 */
		ctlr->port = p->mem[1].bar & ~3;
		ctlr->mixport = p->mem[0].bar & ~3;
	} else if(p->mem[1].size == 256){
		ctlr->port = p->mem[0].bar & ~3;
		ctlr->mixport = p->mem[1].bar & ~3;
	}

	irq = p->intl;
	tbdf = p->tbdf;

	print("#A%d: ac97 port 0x%04lux mixport 0x%04lux irq %d\n",
		adev->ctlrno, ctlr->port, ctlr->mixport, irq);

	pcisetbme(p);
	pcisetioe(p);

	ctlr->mic = xspanalloc(Bufsize, 8, 0);
	ctlr->in = xspanalloc(Bufsize, 8, 0);
	ctlr->out = xspanalloc(Bufsize, 8, 0);

	for(i = 0; i < Ndesc; i++){
		int size, off = i * (Bufsize/Ndesc);
		
		if(sis7012)
			size = (Bufsize/Ndesc);
		else
			size = (Bufsize/Ndesc) / 2;
		
		ctlr->micdesc[i].addr = PCIWADDR(ctlr->mic + off);
		ctlr->micdesc[i].size = Ioc | size;
		ctlr->indesc[i].addr = PCIWADDR(ctlr->in + off);
		ctlr->indesc[i].size = Ioc | size;
		ctlr->outdesc[i].addr = PCIWADDR(ctlr->out + off);
		ctlr->outdesc[i].size = Ioc | size;
	}

	ctlr->outavail = Bufsize - Bufsize/Ndesc;
	ctlr->outp = ctlr->out;

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

	adev->attach = ac97attach;
	adev->write = ac97write;
	adev->status = ac97status;
	adev->ctl = ac97ctl;
	adev->buffered = ac97buffered;
	
	ac97mixreset(adev, ac97mixw, ac97mixr);

	intrenable(irq, ac97interrupt, adev, tbdf, adev->name);

	return 0;
}

void
audioac97link(void)
{
	addaudiocard("ac97audio", ac97reset);
}
