#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/audioif.h"
#include "../port/error.h"

typedef struct Ctlr Ctlr;
typedef struct Ring Ring;

enum {
	Byteps = 4,

	TCSR = 0x08/4,
		TCSR_TE = 1<<31,
		TCSR_FR = 1<<25,
		TCSR_SR = 1<<24,
		TCSR_FEF = 1<<18, /* w1c fifo error */
		TCSR_FWF = 1<<17,
		TCSR_FRF = 1<<16, /* watermark hit */
		TCSR_FEIE = 1<<10,
		TCSR_FWIE = 1<<9,
		TCSR_FRIE = 1<<8,
	TCR1 = 0x0c/4,
	TCR2 = 0x10/4,
		TCR2_MSEL_MCLK1 = 1<<26,
		TCR2_BCP = 1<<25,
		TCR2_BCD_MASTER = 1<<24,
	TCR3 = 0x14/4,
		TCR3_TCE_SHIFT = 16,
	TCR4 = 0x18/4,
		TCR4_FPACK_16BIT = 3<<24,
		TCR4_FRSZ_SHIFT = 16,
		TCR4_SYWD_SHIFT = 8,
		TCR4_CHMOD = 1<<5,
		TCR4_MSB_FIRST = 1<<4,
		TCR4_FSE = 1<<3,
		TCR4_FSP_LOW = 1<<1,
		TCR4_FSD_MASTER = 1<<0,
	TCR5 = 0x1c/4,
		TCR5_WNW_SHIFT = 24,
		TCR5_W0W_SHIFT = 16,
		TCR5_FBT_SHIFT = 8,
	TDR0 = 0x20/4,
	TFR0 = 0x40/4,
		TFRx_WFP_SHIFT = 16,
		TFRx_RFP_SHIFT = 0,
	TMR = 0x60/4,
};

struct Ring {
	Rendez r;

	uchar *buf;
	ulong nbuf;

	ulong ri;
	ulong wi;
};

struct Ctlr {
	u32int *reg;
	Audio *adev;

	Ring w;
	int wactive;

	Lock;
};

#define wr(a, v) ctlr->reg[a] = v
#define rd(a) ctlr->reg[a]

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

	m = (r->nbuf - Byteps) - buffered(r);
	if(m < 0)
		m = 0;
	return m;
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
	int delay = ctlr->adev->delay*Byteps;

	return delay <= 0 || buffered(&ctlr->w) <= delay || ctlr->wactive == 0;
}

static void
saikick(Ctlr *ctlr)
{
	int delay;

	delay = ctlr->adev->delay*Byteps;
	if(buffered(&ctlr->w) >= delay){
		ctlr->wactive = 1;
		/* activate channel 1 */
		wr(TCR3, 1<<TCR3_TCE_SHIFT);
		wr(TCSR, TCSR_TE | TCSR_FEF | TCSR_FRIE | TCSR_FWIE | TCSR_FEIE);
	}
}

static void
saistop(Ctlr *ctlr)
{
	if(!ctlr->wactive)
		return;
	ctlr->wactive = 0;
	wr(TCSR, TCSR_FR | TCSR_SR);
}

static long
saiwrite(Audio *adev, void *a, long n, vlong)
{
	Ctlr *ctlr = adev->ctlr;
	uchar *p, *e;
	Ring *r;

	p = a;
	e = p + n;
	r = &ctlr->w;
	while(p < e){
		if((n = writering(r, p, e - p)) <= 0){
			saikick(ctlr);
			sleep(&r->r, outavail, r);
			continue;
		}
		p += n;
	}
	saikick(ctlr);
	while(outrate(ctlr) == 0)
		sleep(&r->r, outrate, ctlr);
	return p - (uchar*)a;
}

static void
saiclose(Audio *adev, int mode)
{
	Ctlr *ctlr = adev->ctlr;

	if(mode == OWRITE || mode == ORDWR)
		saistop(ctlr);
}

static void
saireset(Ctlr *ctlr)
{
	/* fifo+software reset */
	wr(TCSR, TCSR_FR | TCSR_SR);
	delay(1);
	wr(TCSR, 0);
	delay(1);

	/* watermark - hit early enough */
	wr(TCR1, 32);
	/* derive from mclk1; generate bitclock (active low), f = mclk1/(8+1)*2 = mclk1/18 */
	wr(TCR2, TCR2_MSEL_MCLK1 | TCR2_BCD_MASTER | TCR2_BCP | 8);
	/* activate channel 1 */
	wr(TCR3, 1<<TCR3_TCE_SHIFT);
	/* set up for i²s */
	wr(TCR4,
		TCR4_CHMOD | /* output mode, no TDM */
		TCR4_MSB_FIRST |
		TCR4_FPACK_16BIT | /* 16-bit packed words */
		1<<TCR4_FRSZ_SHIFT | /* two words per frame */
		15<<TCR4_SYWD_SHIFT | /* frame sync per word */
		/* frame sync */
		TCR4_FSE | /* one bit earlier */
		TCR4_FSP_LOW | /* active high */
		TCR4_FSD_MASTER /* generate internally */
	);
	/* 16-bit words, MSB first */
	wr(TCR5, 15<<TCR5_WNW_SHIFT | 15<<TCR5_W0W_SHIFT | 15<<TCR5_FBT_SHIFT);
	/* mask all but first two words */
	wr(TMR, ~3UL);
}

static long
saictl(Audio *adev, void *a, long n, vlong)
{
	char *p, *e, *x, *tok[4];
	Ctlr *ctlr = adev->ctlr;
	int ntok;
	u32int v;

	p = a;
	e = p + n;
	for(; p < e; p = x){
		if(x = strchr(p, '\n'))
			*x++ = 0;
		else
			x = e;
		ntok = tokenize(p, tok, 4);
		if(ntok <= 0)
			continue;

		if(cistrcmp(tok[0], "div") == 0 && ntok >= 2){
			v = strtoul(tok[1], nil, 0);
			wr(TCR2, (rd(TCR2) & ~0xff) | (v & 0xff));
		}else if(cistrcmp(tok[0], "msel") == 0 && ntok >= 2){
			v = strtoul(tok[1], nil, 0);
			wr(TCR2, (rd(TCR2) & ~(3<<26)) | (v & 3)<<26);
		}else if(cistrcmp(tok[0], "reset") == 0){
			saireset(ctlr);
		}else
			error(Ebadctl);
	}
	return n;
}

static long
fifo(Ctlr *ctlr, long n)
{
	long n0, m;
	u32int *p;
	Ring *r;

	n0 = n;
	r = &ctlr->w;
	while(n > 0){
		if((m = buffered(r)) <= 0 || m < 4)
			break;
		if(m > n)
			m = n;

		if(r->ri + m > r->nbuf)
			m = r->nbuf - r->ri;
		m &= ~(Byteps-1);
		for(p = (u32int*)(r->buf + r->ri); p < (u32int*)(r->buf + r->ri + m); p++)
			wr(TDR0, *p);

		r->ri = (r->ri + m) % r->nbuf;
		n -= m;
	}
	return n0 - n;
}

static void
saiinterrupt(Ureg *, void *arg)
{
	Ctlr *ctlr = arg;
	u32int v;
	Ring *r;

	ilock(ctlr);
	v = rd(TCSR);
	if(v & (TCSR_FEF | TCSR_FRF | TCSR_FWF)){
		r = &ctlr->w;
		if(ctlr->wactive){
			if(buffered(r) < 128*Byteps) /* having less than fifo buffered */
				saistop(ctlr);
			else if(fifo(ctlr, (128-32)*Byteps) > 0)
				v |= TCSR_TE;
		}
		wakeup(&r->r);
	}
	wr(TCSR, v | TCSR_FEF);
	iunlock(ctlr);
}

static long
saistatus(Audio *adev, void *a, long n, vlong)
{
	Ctlr *ctlr = adev->ctlr;
	u32int v, p;
	char *s, *e;

	s = a;
	e = s + n;
	v = rd(TCSR);
	p = rd(TFR0);
	s = seprint(s, e, "transmit wfp %d rfp %d delay %d buf %ld avail %ld active %d %s%s%s%s\n",
		(p>>TFRx_WFP_SHIFT) & 0xff,
		(p>>TFRx_RFP_SHIFT) & 0xff,
		adev->delay,
		buffered(&ctlr->w),
		available(&ctlr->w),
		ctlr->wactive,
		(v & TCSR_TE) ? " enabled" : "",
		(v & TCSR_FEF) ? " fifo_error" : "",
		(v & TCSR_FWF) ? " fifo_warn" : "",
		(v & TCSR_FRF) ? " fifo_req" : ""
	);

	return s - (char*)a;
}

static long
saibuffered(Audio *adev)
{
	Ctlr *ctlr = adev->ctlr;

	return buffered(&ctlr->w);
}

static int
saiprobe(Audio *adev)
{
	Ctlr *ctlr;

	if(adev->ctlrno > 0)
		return -1;

	ctlr = mallocz(sizeof(Ctlr), 1);
	if(ctlr == nil)
		return -1;

	ctlr->w.buf = malloc(ctlr->w.nbuf = 44100*Byteps*2);
	ctlr->reg = (u32int*)(VIRTIO + 0x8b0000);
	ctlr->adev = adev;

	adev->delay = 1024;
	adev->ctlr = ctlr;
	adev->write = saiwrite;
	adev->close = saiclose;
	adev->buffered = saibuffered;
	adev->status = saistatus;
	adev->ctl = saictl;

	saireset(ctlr);

	intrenable(IRQsai2, saiinterrupt, ctlr, BUSUNKNOWN, "sai2");

	return 0;
}

void
sailink(void)
{

	iomuxpad("pad_sai2_rxfs", "sai2_rx_sync", "SION ~LVTTL HYS PUE ~ODE FAST 45_OHM VSEL_0");
	iomuxpad("pad_sai2_rxc", "sai2_rx_bclk", "SION ~LVTTL HYS PUE ~ODE FAST 45_OHM VSEL_0");
	iomuxpad("pad_sai2_rxd0", "sai2_rx_data0", "SION ~LVTTL HYS PUE ~ODE FAST 45_OHM VSEL_0");
	iomuxpad("pad_sai2_txfs", "sai2_tx_sync", "SION ~LVTTL HYS PUE ~ODE FAST 45_OHM VSEL_0");
	iomuxpad("pad_sai2_txc", "sai2_tx_bclk", "SION ~LVTTL HYS PUE ~ODE FAST 45_OHM VSEL_0");
	iomuxpad("pad_sai2_txd0", "sai2_tx_data0", "SION ~LVTTL HYS PUE ~ODE FAST 45_OHM VSEL_0");
	iomuxpad("pad_sai2_mclk", "sai2_mclk", "SION ~LVTTL HYS PUE ~ODE FAST 45_OHM VSEL_0");

	setclkgate("sai2.ipg_clk", 1);

	addaudiocard("sai", saiprobe);
}
