/*
 * intel 10GB ethernet pci-express driver
 * 6.0.0:	net  02.00.00 8086/15c8  11 0:dfc0000c 2097152 4:dfe0400c 16384
 *	Intel Corporation Ethernet Connection X553/X550-AT 10GBASE-T
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"


enum {
	/* general */
	Ctrl		= 0x00000/4,	/* Device Control */
	Status		= 0x00008/4,	/* Device Status */
	Ctrlext		= 0x00018/4,	/* Extended Device Control */
	Tcptimer	= 0x0004c/4,	/* tcp timer */

	/* nvm */
	Eec		= 0x10010/4,	/* eeprom/flash control */
	Eemngctl	= 0x10110/4,	/* Manageability EEPROM-Mode Control */

	/* interrupt */
	Icr		= 0x00800/4,	/* interrupt cause read */
	Ics		= 0x00808/4,	/* " set */
	Ims		= 0x00880/4,	/* " mask read/set */
	Imc		= 0x00888/4,	/* " mask clear */
	Iac		= 0x00810/4,	/* " auto clear */
	Iam		= 0x00890/4,	/* " auto mask enable */
	Itr		= 0x00820/4,	/* " throttling rate (0-19) */
	Ivar		= 0x00900/4,	/* " vector allocation regs. */

	/* rx dma */
	Rdbal		= 0x01000/4,	/* rx desc base low (0-63) +0x40n */
	Rdbah		= 0x01004/4,	/* " high */
	Rdlen		= 0x01008/4,	/* " length */
	Rdh		= 0x01010/4,	/* " head */
	Rdt		= 0x01018/4,	/* " tail */
	Rxdctl		= 0x01028/4,	/* " control */

	Srrctl		= 0x02100/4,	/* split and replication rx ctl. */
	Rdrxctl		= 0x02f00/4,	/* rx dma control */
	Rxpbsize	= 0x03c00/4,	/* rx packet buffer size */
	Rxctrl		= 0x03000/4,	/* rx control */

	/* rx */
	Rxcsum		= 0x05000/4,	/* rx checksum control */
	Mcstctrl	= 0x05090/4,	/* multicast control register */
	Mta		= 0x05200/4,	/* multicast table array (0-127) */
	Ral		= 0x05400/4,	/* rx address low */
	Rah		= 0x05404/4,
	Vfta		= 0x0a000/4,	/* vlan filter table array. */
	Fctrl		= 0x05080/4,	/* filter control */

	/* tx */
	Tdbal		= 0x06000/4,	/* tx desc base low +0x40n */
	Tdbah		= 0x06004/4,	/* " high */
	Tdlen		= 0x06008/4,	/* " len */
	Tdh		= 0x06010/4,	/* " head */
	Tdt		= 0x06018/4,	/* " tail */
	Txdctl		= 0x06028/4,	/* " control */
	Dmatxctl	= 0x04a80/4,

	/* mac */
	Hlreg0		= 0x04240/4,	/* highlander control reg 0 */
	Hlreg1		= 0x04244/4,	/* highlander control reg 1 (ro) */
	Maxfrs		= 0x04268/4,	/* max frame size */
	Links		= 0x042a4/4,	/* link status */
};

enum {
	/* Ctrl */
	Rst		= 1<<26,	/* full nic reset */
	
	/* Ctrlext */
	Drvload		= 1<<28,	/* Driver Load */
	
	/* Eec */
	AutoRd		= 1<<9,		/* NVM auto read done */

	/* Eemngctl */
	CfgDone0	= 1<<18,	/* Configuration Done Port 0 */
	CfgDone1	= 1<<19,	/* Configuration Done Port 1 */

	/* Txdctl */
	Pthresh		= 0,		/* prefresh threshold shift in bits */
	Hthresh		= 8,		/* host buffer minimum threshold */
	Wthresh		= 16,		/* writeback threshold */
	Ten		= 1<<25,

	/* Fctrl */
	Bam		= 1<<10,	/* broadcast accept mode */
	Upe 		= 1<<9,		/* unicast promiscuous */
	Mpe 		= 1<<8,		/* multicast promiscuous */

	/* Rxdctl */
	Renable		= 1<<25,

	/* Dmatxctl */
	Txen		= 1<<0,

	/* Rxctl */
	Rxen		= 1<<0,

	/* Rdrxctl */
	Dmaidone	= 1<<3,

	/* Rxcsum */
	Ippcse		= 1<<12,	/* ip payload checksum enable */

	/* Mcstctrl */
	Mo		= 0,		/* multicast offset 47:36 */
	Mfe		= 1<<2,		/* multicast filter enable */

	/* Rah */
	Av		= 1<<31,

	/* interrupts */
	Irx0		= 1<<0,		/* driver defined - rx interrupt */
	Itx0		= 1<<1,		/* driver defined - tx interrupt */
	Lsc		= 1<<20,	/* link status change */
	
	/* Ivar Interrupt Vector Allocation Register */
	Intalloc0	= 0,		/* Map the 0th queue Rx interrupt to the 0th bit of EICR register */
	Intallocval0	= 1<<7,
	intalloc1	= 1<<8,		/* Map the 0th queue Tx interrupt to the 1st bit of EICR register */
	Intallocval1	= 1<<15,

	/* Links */
	Lnkup	= 1<<30,
	Lnkspd	= 1<<29,

	/* Hlreg0 */
	Jumboen	= 1<<2,
};

typedef struct {
	uint	reg;
	char	*name;
} Stat;

static
Stat stattab[] = {
	0x4000,	"crc error",
	0x4004,	"illegal byte",
	0x4008,	"short packet",
	0x3fa0,	"missed pkt0",
	0x4034,	"mac local flt",
	0x4038,	"mac rmt flt",
	0x4040,	"rx length err",
	0x405c,	"rx 040",
	0x4060,	"rx 07f",
	0x4064,	"rx 100",
	0x4068,	"rx 200",
	0x406c,	"rx 3ff",
	0x4070,	"rx big",
	0x4074,	"rx ok",
	0x4078,	"rx bcast",
	0x407c,	"rx mcast",
	0x4080,	"tx ok",
	0x40a4,	"rx runt",
	0x40a8,	"rx frag",
	0x40ac,	"rx ovrsz",
	0x40b0,	"rx jab",
	0x40d0,	"rx pkt",
	0x40d4,	"tx pkt",
	0x40d8,	"tx 040",
	0x40dc,	"tx 07f",
	0x40e0,	"tx 100",
	0x40e4,	"tx 200",
	0x40e8,	"tx 3ff",
	0x40ec,	"tx big",
	0x40f0,	"tx mcast",
	0x40f4,	"tx bcast",
	0x4120,	"xsum err",
};

/* status */
enum {
	Pif	= 1<<7,	/* past exact filter (sic) */
	Ipcs	= 1<<6,	/* ip checksum calcuated */
	L4cs	= 1<<5,	/* layer 2 */
	Udpcs	= 1<<4,	/* udp checksum calcuated */
	Vp	= 1<<3,	/* 802.1q packet matched vet */
	Reop	= 1<<1,	/* end of packet */
	Rdd	= 1<<0,	/* descriptor done */
};

typedef struct {
	u32int	addr[2];
	ushort	length;
	ushort	cksum;
	uchar	status;
	uchar	errors;
	ushort	vlan;
} Rd;

enum {
	/* Td cmd */
	Rs	= 1<<3,
	Ic	= 1<<2,
	Ifcs	= 1<<1,
	Teop	= 1<<0,

	/* Td status */
	Tdd	= 1<<0,
};

typedef struct {
	u32int	addr[2];
	u16int	length;
	uchar	cso;
	uchar	cmd;
	uchar	status;
	uchar	css;
	ushort	vlan;
} Td;

enum {
	Factive		= 1<<0,
	Fstarted	= 1<<1,
};

typedef struct {
	Pcidev	*p;
	Ether	*edev;
	uintptr	io;
	u32int	*reg;
	u32int	*regmsi;
	uchar	flag;
	int	nrd;
	int	ntd;
	int	rbsz;
	Lock	slock;
	Lock	alock;
	QLock	tlock;
	Rendez	lrendez;
	Rendez	trendez;
	Rendez	rrendez;
	uint	im;
	uint	lim;
	uint	rim;
	uint	tim;
	Lock	imlock;
	char	*alloc;

	Rd	*rdba;
	Block	**rb;
	uint	rdt;
	uint	rdfree;

	Td	*tdba;
	uint	tdh;
	uint	tdt;
	Block	**tb;

	uchar	ra[Eaddrlen];
	u32int	mta[128];
	ulong	stats[nelem(stattab)];
	uint	speeds[3];
} Ctlr;

/* tweakable paramaters */
enum {
	Rbsz	= 12*1024,
	Nrd	= 256,
	Ntd	= 256,
	Nrb	= 256,
};

static	Ctlr	*ctlrtab[4];
static	int	nctlr;

static void
readstats(Ctlr *c)
{
	int i;

	lock(&c->slock);
	for(i = 0; i < nelem(c->stats); i++)
		c->stats[i] += c->reg[stattab[i].reg >> 2];
	unlock(&c->slock);
}

static int speedtab[] = {
	0,
	1000,
	10000,
};

static long
ifstat(Ether *e, void *a, long n, ulong offset)
{
	uint i, *t;
	char *s, *p, *q;
	Ctlr *c;

	p = s = smalloc(READSTR);
	q = p + READSTR;

	c = e->ctlr;
	readstats(c);
	for(i = 0; i < nelem(stattab); i++)
		if(c->stats[i] > 0)
			p = seprint(p, q, "%.10s  %uld\n", stattab[i].name,
					c->stats[i]);
	t = c->speeds;
	p = seprint(p, q, "speeds: 0:%d 1000:%d 10000:%d\n", t[0], t[1], t[2]);
	seprint(p, q, "rdfree %d rdh %d rdt %d\n", c->rdfree, c->reg[Rdt],
		c->reg[Rdh]);
	n = readstr(offset, a, n, s);
	free(s);

	return n;
}

static void
im(Ctlr *c, int i)
{
	ilock(&c->imlock);
	c->im |= i;
	c->reg[Ims] = c->im;
	iunlock(&c->imlock);
}

static int
lim(void *v)
{
	return ((Ctlr*)v)->lim != 0;
}

static void
lproc(void *v)
{
	int r, i;
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;
	while(waserror())
		;
	for (;;) {
		r = c->reg[Links];
		e->link = (r & Lnkup) != 0;
		i = 0;
		if(e->link)
			i = 1 + ((r & Lnkspd) != 0);
		c->speeds[i]++;
		e->mbps = speedtab[i];
		c->lim = 0;
		im(c, Lsc);
		sleep(&c->lrendez, lim, c);
		c->lim = 0;
	}
}

static long
ctl(Ether *, void *, long)
{
	error(Ebadarg);
	return -1;
}

#define Next(x, m)	(((x)+1) & (m))

static int
cleanup(Ctlr *c, int tdh)
{
	Block *b;
	uint m, n;

	m = c->ntd - 1;
	while(c->tdba[n = Next(tdh, m)].status & Tdd){
		tdh = n;
		b = c->tb[tdh];
		c->tb[tdh] = 0;
		freeb(b);
		c->tdba[tdh].status = 0;
	}
	return tdh;
}

static void
transmit(Ether *e)
{
	uint i, m, tdt, tdh;
	Ctlr *c;
	Block *b;
	Td *t;

	c = e->ctlr;
	if(!canqlock(&c->tlock)){
		im(c, Itx0);
		return;
	}
	tdh = c->tdh = cleanup(c, c->tdh);
	tdt = c->tdt;
	m = c->ntd - 1;
	for(i = 0; i < 8; i++){
		if(Next(tdt, m) == tdh){
			im(c, Itx0);
			break;
		}
		if(!(b = qget(e->oq)))
			break;
		t = c->tdba + tdt;
		t->addr[0] = PCIWADDR(b->rp);
		t->length = BLEN(b);
		t->cmd = Rs | Ifcs | Teop;
		c->tb[tdt] = b;
		tdt = Next(tdt, m);
	}
	if(i){
		c->tdt = tdt;
		c->reg[Tdt] = tdt;
	}
	qunlock(&c->tlock);
}

static int
tim(void *c)
{
	return ((Ctlr*)c)->tim != 0;
}

static void
tproc(void *v)
{
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;
	while(waserror())
		;
	for (;;) {
		sleep(&c->trendez, tim, c);	/* transmit kicks us */
		c->tim = 0;
		transmit(e);
	}
}

static void
rxinit(Ctlr *c)
{
	int i;
	Block *b;

	c->reg[Rxctrl] &= ~Rxen;
	/* Pg 144 Step 2
		Receive buffers of appropriate size should be allocated
		and pointers to these buffers should be stored in the
		descriptor ring - replinish() does this? */
	for(i = 0; i < c->nrd; i++){
		b = c->rb[i];
		c->rb[i] = 0;
		if(b)
			freeb(b);
	}
	c->rdfree = 0;

	c->reg[Fctrl] |= Bam;
	c->reg[Rxcsum] |= Ippcse;
	c->reg[Srrctl] = (c->rbsz + 1023)/1024;
	c->reg[Maxfrs] = c->rbsz << 16;
	c->reg[Hlreg0] |= Jumboen;

	c->reg[Rdbal] = PCIWADDR(c->rdba);
	c->reg[Rdbah] = 0;
	c->reg[Rdlen] = c->nrd*sizeof(Rd);
	c->reg[Rdh] = 0;
	c->reg[Rdt] = c->rdt = 0;

	c->reg[Rxdctl] = Renable;
	while((c->reg[Rxdctl] & Renable) == 0)
		;
	/* TODO? bump the tail pointer RDT to enable descriptors
		fetching by setting it to the ring length minus 1. Pg 145 */
	c->reg[Rxctrl] |= Rxen;
}

static void
replenish(Ctlr *c, uint rdh)
{
	int rdt, m, i;
	Block *b;
	Rd *r;

	m = c->nrd - 1;
	i = 0;
	for(rdt = c->rdt; Next(rdt, m) != rdh; rdt = Next(rdt, m)){
		b = allocb(c->rbsz+BY2PG);
		b->rp = (uchar*)PGROUND((uintptr)b->base);
		b->wp = b->rp;
		c->rb[rdt] = b;
		r = c->rdba + rdt;
		r->addr[0] = PCIWADDR(b->rp);
		r->status = 0;
		c->rdfree++;
		i++;
	}
	if(i)
		c->reg[Rdt] = c->rdt = rdt;
}

static int
rim(void *v)
{
	return ((Ctlr*)v)->rim != 0;
}

static uchar zeroea[Eaddrlen];

static void
rproc(void *v)
{
	uint m, rdh;
	Block *b;
	Ctlr *c;
	Ether *e;
	Rd *r;

	e = v;
	c = e->ctlr;
	m = c->nrd - 1;
	rdh = 0;
	while(waserror())
		;
loop:
	replenish(c, rdh);
	im(c, Irx0);
	sleep(&c->rrendez, rim, c);
loop1:
	c->rim = 0;
	if(c->nrd - c->rdfree >= 16)
		replenish(c, rdh);
	r = c->rdba + rdh;
	if(!(r->status & Rdd))
		goto loop;		/* UGH */
	b = c->rb[rdh];
	c->rb[rdh] = 0;
	b->wp += r->length;
	if((r->status & 1)){
		if(r->status & Ipcs)
			b->flag |= Bipck;
		b->checksum = r->cksum;
	}
//	r->status = 0;
	etheriq(e, b);
	c->rdfree--;
	rdh = Next(rdh, m);
	goto loop1;			/* UGH */
}

static void
promiscuous(void *a, int on)
{
	Ctlr *c;
	Ether *e;

	e = a;
	c = e->ctlr;
	if(on)
		c->reg[Fctrl] |= Upe | Mpe;
	else
		c->reg[Fctrl] &= ~(Upe | Mpe);
}

static void
multicast(void *a, uchar *ea, int on)
{
	int b, i;
	Ctlr *c;
	Ether *e;

	e = a;
	c = e->ctlr;

	/*
	 * multiple ether addresses can hash to the same filter bit,
	 * so it's never safe to clear a filter bit.
	 * if we want to clear filter bits, we need to keep track of
	 * all the multicast addresses in use, clear all the filter bits,
	 * then set the ones corresponding to in-use addresses.
	 *
	 *  Extracts the 12 bits, from a multicast address, to determine which
	 *  bit-vector to set in the multicast table. The hardware uses 12 bits, from
	 *  incoming rx multicast addresses, to determine the bit-vector to check in
	 *  the MTA. Which of the 4 combination, of 12-bits, the hardware uses is set
	 *  by the MO field of the MCSTCTRL. The MO field is set during initialization
	 *  to mc_filter_type.
	 *
	 * The MTA is a register array of 128 32-bit registers. It is treated
	 * like an array of 4096 bits.  We want to set bit
	 * BitArray[vector_value]. So we figure out what register the bit is
	 * in, read it, OR in the new bit, then write back the new value.  The
	 * register is determined by the upper 7 bits of the vector value and
	 * the bit within that register are determined by the lower 5 bits of
	 * the value.
	 *
	 * when Mcstctrl.Mo == 0, use bits [47:36] of the address
	 * register index = bits [47:41]
	 * which bit in the above register = bits [40:36]
	 */
	i = ea[5] >> 1;			/* register index = 47:41 (7 bits) */
	b = (ea[5]&1)<<4 | ea[4]>>4;	/* which bit in the above register = 40:36 (5 bits) */
	b = 1 << b;
	if(on)
		c->mta[i] |= b;
//	else
//		c->mta[i] &= ~b;
	c->reg[Mta+i] = c->mta[i];
	c->reg[Mcstctrl] = Mfe;
	/* for(i = 0; i < 128; i++) c->reg[Mta + i] = -1; brute force it to work for testing */
}

static int
detach(Ctlr *c)
{
	int i;
	u32int l, h;

	l = c->reg[Ral];
	h = c->reg[Rah];
	if (h & Av) {
		c->ra[0] = l & 0xFF;
		c->ra[1] = l>>8 & 0xFF;
		c->ra[2] = l>>16 & 0xFF;
		c->ra[3] = l>>24 & 0xFF;
		c->ra[4] = h & 0xFF;
		c->ra[5] = h>>8 & 0xFF;
	}
	c->reg[Imc] = ~0;
	c->reg[Ctrl] |= Rst;
	for(i = 0; i < 100; i++){
		delay(1);
		if((c->reg[Ctrl] & Rst) == 0)
			break;
	}
	if (i >= 100)
		return -1;
	delay(10);

	/* not cleared by reset; kill it manually. */
	for(i = 1; i < 16; i++)
		c->reg[Rah + i] &= ~(1 << 31);
	for(i = 0; i < 128; i++)
		c->reg[Mta + i] = 0;
	for(i = 1; i < 640; i++)
		c->reg[Vfta + i] = 0;
	c->reg[Ctrlext] &= ~Drvload; /* driver works without this */
	return 0;
}

static void
shutdown(Ether *e)
{
	detach(e->ctlr);
}

static int
reset(Ctlr *c)
{
	int i;

	while((c->reg[Eec] & AutoRd) == 0)
		;
	while((c->reg[Eemngctl] & CfgDone0) == 0)
		;
	while((c->reg[Eemngctl] & CfgDone1) == 0)
		;
	while((c->reg[Rdrxctl] & Dmaidone) == 0)
		;
	if(detach(c)){
		print("iX550: reset timeout\n");
		return -1;
	}
	while((c->reg[Eec] & AutoRd) == 0)
		;
	while((c->reg[Eemngctl] & CfgDone0) == 0)
		;
	while((c->reg[Eemngctl] & CfgDone1) == 0)
		;
	while((c->reg[Rdrxctl] & Dmaidone) == 0)
		;
	readstats(c);
	for(i = 0; i<nelem(c->stats); i++)
		c->stats[i] = 0;

	/* configure interrupt mapping */
	c->reg[Ivar] =   Intalloc0 | Intallocval0 | intalloc1 | Intallocval1;

	/* interrupt throttling goes here. */
	for(i = Itr; i < Itr + 20; i++)
		c->reg[i] = 1<<3;	/* 1 interval */
	return 0;
}

static void
txinit(Ctlr *c)
{
	Block *b;
	int i;

	c->reg[Txdctl] = 16<<Wthresh | 16<<Pthresh;
	for(i = 0; i < c->ntd; i++){
		b = c->tb[i];
		c->tb[i] = 0;
		if(b)
			freeb(b);
	}
	memset(c->tdba, 0, c->ntd * sizeof(Td));
	c->reg[Tdbal] = PCIWADDR(c->tdba);
	c->reg[Tdbah] = 0;
	c->reg[Tdlen] = c->ntd*sizeof(Td);
	c->reg[Tdh] = 0;
	c->reg[Tdt] = 0;
	c->tdh = c->ntd - 1;
	c->tdt = 0;

	c->reg[Txdctl] |= Ten;
	c->reg[Dmatxctl] |= Txen;

}

static void
attach(Ether *e)
{
	Ctlr *c;
	int t;
	char buf[KNAMELEN];

	c = e->ctlr;
	c->edev = e;			/* point back to Ether* */
	lock(&c->alock);
	if(c->alloc){
		unlock(&c->alock);
		return;
	}

	c->nrd = Nrd;
	c->ntd = Ntd;
	t  = c->nrd * sizeof *c->rdba + 255;
	t += c->ntd * sizeof *c->tdba + 255;
	t += (c->ntd + c->nrd) * sizeof(Block*);
	c->alloc = malloc(t);
	unlock(&c->alock);
	if(c->alloc == nil)
		error(Enomem);

	c->rdba = (Rd*)ROUNDUP((uintptr)c->alloc, 256);
	c->tdba = (Td*)ROUNDUP((uintptr)(c->rdba + c->nrd), 256);
	c->rb = (Block**)(c->tdba + c->ntd);
	c->tb = (Block**)(c->rb + c->nrd);

	rxinit(c);
	txinit(c);

	c->reg[Ctrlext] |= Drvload; /* driver works without this */
	snprint(buf, sizeof buf, "#l%dl", e->ctlrno);
	kproc(buf, lproc, e);
	snprint(buf, sizeof buf, "#l%dr", e->ctlrno);
	kproc(buf, rproc, e);
	snprint(buf, sizeof buf, "#l%dt", e->ctlrno);
	kproc(buf, tproc, e);
}

static void
interrupt(Ureg*, void *v)
{
	int icr, im;
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;
	ilock(&c->imlock);
	c->reg[Imc] = ~0;
	im = c->im;
	while((icr = c->reg[Icr] & c->im) != 0){
		if(icr & Lsc){
			im &= ~Lsc;
			c->lim = icr & Lsc;
			wakeup(&c->lrendez);
		}
		if(icr & Irx0){
			im &= ~Irx0;
			c->rim = icr & Irx0;
			wakeup(&c->rrendez);
		}
		if(icr & Itx0){
			im &= ~Itx0;
			c->tim = icr & Itx0;
			wakeup(&c->trendez);
		}
	}
	c->reg[Ims] = c->im = im;
	iunlock(&c->imlock);
}

extern void addvgaseg(char*, ulong, ulong);

static void
scan(void)
{
	uintptr io, iomsi;
	void *mem, *memmsi;
	int pciregs, pcimsix;
	Ctlr *c;
	Pcidev *p;

	p = 0;
	while(p = pcimatch(p, 0x8086, 0x15c8)){	/* X553/X550-AT 10GBASE-T */
		pcimsix = 4;
		pciregs = 0;
		if(nctlr == nelem(ctlrtab)){
			print("iX550: too many controllers\n");
			return;
		}
		c = malloc(sizeof *c);
		if(c == nil){
			print("iX550: can't allocate memory\n");
			continue;
		}
		io = p->mem[pciregs].bar & ~0xf;
		mem = vmap(io, p->mem[pciregs].size);
		if(mem == nil){
			print("iX550: can't map regs %#p\n", io);
			free(c);
			continue;
		}
		if (nctlr == 0)
			addvgaseg("pci.ctlr0.bar0", p->mem[pciregs].bar & ~0xf, p->mem[pciregs].size);
		else if (nctlr == 1)
			addvgaseg("pci.ctlr1.bar0", p->mem[pciregs].bar & ~0xf, p->mem[pciregs].size);
		iomsi = p->mem[pcimsix].bar & ~0xf;
		memmsi = vmap(iomsi, p->mem[pcimsix].size);
		if(memmsi == nil){
			print("iX550: can't map msi-x regs %#p\n", iomsi);
			vunmap(mem, p->mem[pciregs].size);
			free(c);
			continue;
		}
		pcienable(p);
		c->p = p;
		c->io = io;
		c->reg = (u32int*)mem;
		c->regmsi = (u32int*)memmsi;
		c->rbsz = Rbsz;
		if(reset(c)){
			print("iX550: can't reset\n");
			free(c);
			vunmap(mem, p->mem[pciregs].size);
			vunmap(memmsi, p->mem[pcimsix].size);
			continue;
		}
		pcisetbme(p);
		ctlrtab[nctlr++] = c;
	}
}

static int
pnp(Ether *e)
{
	static uchar zeros[Eaddrlen];
	int i;
	Ctlr *c = nil;
	uchar *p;

	if(nctlr == 0)
		scan();
	for(i = 0; i < nctlr; i++){
		c = ctlrtab[i];
		if(c == nil || c->flag & Factive)
			continue;
		if(e->port == 0 || e->port == c->io)
			break;
	}
	if (i >= nctlr)
		return -1;

	if(memcmp(c->ra, zeros, Eaddrlen) != 0)
		memmove(e->ea, c->ra, Eaddrlen);

	p = e->ea;
	c->reg[Ral] = p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
	c->reg[Rah] = p[5]<<8 | p[4] | 1<<31;

	c->flag |= Factive;
	e->ctlr = c;
	e->port = (uintptr)c->reg;
	e->irq = c->p->intl;
	e->tbdf = c->p->tbdf;
	e->mbps = 10000;
	e->maxmtu = c->rbsz;

	e->arg = e;
	e->attach = attach;
	e->ctl = ctl;
	e->ifstat = ifstat;
	e->multicast = multicast;
	e->promiscuous = promiscuous;
	e->shutdown = shutdown;
	e->transmit = transmit;

	intrenable(e->irq, interrupt, e, e->tbdf, e->name);

	return 0;
}

void
etherx550link(void)
{
	addethercard("iX550", pnp);
}
