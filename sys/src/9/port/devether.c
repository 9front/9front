#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "pool.h"
#include "ureg.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"

extern int eipfmt(Fmt*);

static Ether *etherxx[MaxEther];

Chan*
etherattach(char* spec)
{
	ulong ctlrno;
	char *p;
	Chan *chan;

	ctlrno = 0;
	if(spec && *spec){
		ctlrno = strtoul(spec, &p, 0);
		if((ctlrno == 0 && p == spec) || *p || (ctlrno >= MaxEther))
			error(Ebadarg);
	}
	if(etherxx[ctlrno] == 0)
		error(Enodev);

	chan = devattach('l', spec);
	if(waserror()){
		chanfree(chan);
		nexterror();
	}
	chan->dev = ctlrno;
	if(etherxx[ctlrno]->attach)
		etherxx[ctlrno]->attach(etherxx[ctlrno]);
	poperror();
	return chan;
}

static Walkqid*
etherwalk(Chan* chan, Chan* nchan, char** name, int nname)
{
	return netifwalk(etherxx[chan->dev], chan, nchan, name, nname);
}

static int
etherstat(Chan* chan, uchar* dp, int n)
{
	return netifstat(etherxx[chan->dev], chan, dp, n);
}

static Chan*
etheropen(Chan* chan, int omode)
{
	return netifopen(etherxx[chan->dev], chan, omode);
}

static Chan*
ethercreate(Chan*, char*, int, ulong)
{
	error(Eperm);
	return 0;
}

static void
etherclose(Chan* chan)
{
	netifclose(etherxx[chan->dev], chan);
}

static long
etherread(Chan* chan, void* buf, long n, vlong off)
{
	Ether *ether;
	ulong offset = off;

	ether = etherxx[chan->dev];
	if((chan->qid.type & QTDIR) == 0 && ether->ifstat){
		/*
		 * With some controllers it is necessary to reach
		 * into the chip to extract statistics.
		 */
		if(NETTYPE(chan->qid.path) == Nifstatqid)
			return ether->ifstat(ether, buf, n, offset);
		else if(NETTYPE(chan->qid.path) == Nstatqid)
			ether->ifstat(ether, buf, 0, offset);
	}

	return netifread(ether, chan, buf, n, offset);
}

static Block*
etherbread(Chan* chan, long n, ulong offset)
{
	return netifbread(etherxx[chan->dev], chan, n, offset);
}

static int
etherwstat(Chan* chan, uchar* dp, int n)
{
	return netifwstat(etherxx[chan->dev], chan, dp, n);
}

static void
etherrtrace(Netfile* f, Etherpkt* pkt, int len)
{
	int i, n;
	Block *bp;

	if(qwindow(f->in) <= 0)
		return;
	if(len > 58)
		n = 58;
	else
		n = len;
	bp = iallocb(64);
	if(bp == nil)
		return;
	memmove(bp->wp, pkt->d, n);
	i = TK2MS(MACHP(0)->ticks);
	bp->wp[58] = len>>8;
	bp->wp[59] = len;
	bp->wp[60] = i>>24;
	bp->wp[61] = i>>16;
	bp->wp[62] = i>>8;
	bp->wp[63] = i;
	bp->wp += 64;
	qpass(f->in, bp);
}

Block*
etheriq(Ether* ether, Block* bp, int fromwire)
{
	Etherpkt *pkt;
	ushort type;
	int len, multi, tome, fromme;
	Netfile **ep, *f, **fp, *fx;
	Block *xbp;

	ether->inpackets++;

	pkt = (Etherpkt*)bp->rp;
	len = BLEN(bp);
	type = (pkt->type[0]<<8)|pkt->type[1];
	fx = 0;
	ep = &ether->f[Ntypes];

	multi = pkt->d[0] & 1;
	/* check for valid multicast addresses */
	if(multi && memcmp(pkt->d, ether->bcast, sizeof(pkt->d)) != 0 && ether->prom == 0){
		if(!activemulti(ether, pkt->d, sizeof(pkt->d))){
			if(fromwire){
				freeb(bp);
				bp = 0;
			}
			return bp;
		}
	}

	/* is it for me? */
	tome = memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	fromme = memcmp(pkt->s, ether->ea, sizeof(pkt->s)) == 0;

	/*
	 * Multiplex the packet to all the connections which want it.
	 * If the packet is not to be used subsequently (fromwire != 0),
	 * attempt to simply pass it into one of the connections, thereby
	 * saving a copy of the data (usual case hopefully).
	 */
	for(fp = ether->f; fp < ep; fp++){
		if(f = *fp)
		if(f->type == type || f->type < 0)
		if(tome || multi || f->prom){
			/* Don't want to hear loopback or bridged packets */
			if(f->bridge && (tome || !fromwire && !fromme))
				continue;
			if(!f->headersonly){
				if(fromwire && fx == 0)
					fx = f;
				else if(xbp = iallocb(len)){
					memmove(xbp->wp, pkt, len);
					xbp->wp += len;
					xbp->flag = bp->flag;
					if(qpass(f->in, xbp) < 0) {
						// print("soverflow for f->in\n");
						ether->soverflows++;
					}
				}
				else {
					// print("soverflow iallocb\n");
					ether->soverflows++;
				}
			}
			else
				etherrtrace(f, pkt, len);
		}
	}

	if(fx){
		if(qpass(fx->in, bp) < 0) {
			// print("soverflow for fx->in\n");
			ether->soverflows++;
		}
		return 0;
	}
	if(fromwire){
		freeb(bp);
		return 0;
	}

	return bp;
}

static int
etheroq(Ether* ether, Block* bp)
{
	int len, loopback;
	Etherpkt *pkt;

	ether->outpackets++;

	/*
	 * Check if the packet has to be placed back onto the input queue,
	 * i.e. if it's a loopback or broadcast packet or the interface is
	 * in promiscuous mode.
	 * If it's a loopback packet indicate to etheriq that the data isn't
	 * needed and return, etheriq will pass-on or free the block.
	 * To enable bridging to work, only packets that were originated
	 * by this interface are fed back.
	 */
	pkt = (Etherpkt*)bp->rp;
	len = BLEN(bp);
	loopback = memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	if(loopback || memcmp(pkt->d, ether->bcast, sizeof(pkt->d)) == 0 || ether->prom)
		if(etheriq(ether, bp, loopback) == 0)
			return len;

	qbwrite(ether->oq, bp);
	if(ether->transmit != nil)
		ether->transmit(ether);
	return len;
}

static long
etherwrite(Chan* chan, void* buf, long n, vlong)
{
	Ether *ether;
	Block *bp;
	int nn, onoff;
	Cmdbuf *cb;

	ether = etherxx[chan->dev];
	if(NETTYPE(chan->qid.path) != Ndataqid) {
		nn = netifwrite(ether, chan, buf, n);
		if(nn >= 0)
			return nn;
		cb = parsecmd(buf, n);
		if(cb->f[0] && strcmp(cb->f[0], "nonblocking") == 0){
			if(cb->nf <= 1)
				onoff = 1;
			else
				onoff = atoi(cb->f[1]);
			qnoblock(ether->oq, onoff);
			free(cb);
			return n;
		}
		free(cb);
		if(ether->ctl!=nil)
			return ether->ctl(ether,buf,n);

		error(Ebadctl);
	}

	if(n > ether->maxmtu)
		error(Etoobig);
	if(n < ether->minmtu)
		error(Etoosmall);

	bp = allocb(n);
	if(waserror()){
		freeb(bp);
		nexterror();
	}
	memmove(bp->rp, buf, n);
	if(!ether->f[NETID(chan->qid.path)]->bridge)
		memmove(bp->rp+Eaddrlen, ether->ea, Eaddrlen);
	poperror();
	bp->wp += n;

	return etheroq(ether, bp);
}

static long
etherbwrite(Chan* chan, Block* bp, ulong)
{
	Ether *ether;
	long n;

	n = BLEN(bp);
	if(NETTYPE(chan->qid.path) != Ndataqid){
		if(waserror()) {
			freeb(bp);
			nexterror();
		}
		n = etherwrite(chan, bp->rp, n, 0);
		poperror();
		freeb(bp);
		return n;
	}
	ether = etherxx[chan->dev];

	if(n > ether->maxmtu){
		freeb(bp);
		error(Etoobig);
	}
	if(n < ether->minmtu){
		freeb(bp);
		error(Etoosmall);
	}

	return etheroq(ether, bp);
}

static struct {
	char*	type;
	int	(*reset)(Ether*);
} cards[MaxEther+1];

void
addethercard(char* t, int (*r)(Ether*))
{
	static int ncard;

	if(ncard == MaxEther)
		panic("too many ether cards");
	cards[ncard].type = t;
	cards[ncard].reset = r;
	ncard++;
}

static Ether*
etherprobe(int cardno, int ctlrno)
{
	int i, lg;
	ulong mb, bsz;
	Ether *ether;

	ether = malloc(sizeof(Ether));
	if(ether == nil){
		print("etherprobe: no memory for Ether\n");
		return nil;
	}
	memset(ether, 0, sizeof(Ether));
	ether->tbdf = BUSUNKNOWN;
	ether->irq = -1;
	ether->ctlrno = ctlrno;
	ether->mbps = 10;
	ether->minmtu = ETHERMINTU;
	ether->maxmtu = ETHERMAXTU;

	if(cardno < 0){
		if(isaconfig("ether", ctlrno, ether) == 0){
			free(ether);
			return nil;
		}
		for(cardno = 0; cards[cardno].type; cardno++){
			if(cistrcmp(cards[cardno].type, ether->type))
				continue;
			for(i = 0; i < ether->nopt; i++){
				if(strncmp(ether->opt[i], "ea=", 3))
					continue;
				if(parseether(ether->ea, &ether->opt[i][3]))
					memset(ether->ea, 0, Eaddrlen);
			}
			break;
		}
	}

	if(cardno >= MaxEther || cards[cardno].type == nil){
		free(ether);
		return nil;
	}
	snprint(ether->name, sizeof(ether->name), "ether%d", ctlrno);
	if(cards[cardno].reset(ether) < 0){
		free(ether);
		return nil;
	}

	print("#l%d: %s: %dMbps port 0x%luX irq %d ea %E\n",
		ctlrno, cards[cardno].type,
		ether->mbps, ether->port, ether->irq, ether->ea);

	/* compute log10(ether->mbps) into lg */
	for(lg = 0, mb = ether->mbps; mb >= 10; lg++)
		mb /= 10;
	if (lg > 0)
		lg--;
	if (lg > 14)			/* 2^(14+17) = 2³¹ */
		lg = 14;
	/* allocate larger output queues for higher-speed interfaces */
	bsz = 1UL << (lg + 17);		/* 2¹⁷ = 128K, bsz = 2ⁿ × 128K */
	while (bsz > mainmem->maxsize / 8 && bsz > 128*1024)
		bsz /= 2;

	netifinit(ether, ether->name, Ntypes, bsz);
	if(ether->oq == nil) {
		ether->oq = qopen(bsz, Qmsg, 0, 0);
		ether->limit = bsz;
	}
	if(ether->oq == nil)
		panic("etherreset %s: can't allocate output queue of %ld bytes", ether->name, bsz);

	ether->alen = Eaddrlen;
	memmove(ether->addr, ether->ea, Eaddrlen);
	memset(ether->bcast, 0xFF, Eaddrlen);

	return ether;
}

static void
etherreset(void)
{
	Ether *ether;
	int cardno, ctlrno;

	fmtinstall('E', eipfmt);

	for(ctlrno = 0; ctlrno < MaxEther; ctlrno++){
		if((ether = etherprobe(-1, ctlrno)) == nil)
			continue;
		etherxx[ctlrno] = ether;
	}

	if(getconf("*noetherprobe"))
		return;

	cardno = ctlrno = 0;
	while(cards[cardno].type != nil && ctlrno < MaxEther){
		if(etherxx[ctlrno] != nil){
			ctlrno++;
			continue;
		}
		if((ether = etherprobe(cardno, ctlrno)) == nil){
			cardno++;
			continue;
		}
		etherxx[ctlrno] = ether;
		ctlrno++;
	}
}

static void
ethershutdown(void)
{
	Ether *ether;
	int i;

	for(i = 0; i < MaxEther; i++){
		ether = etherxx[i];
		if(ether == nil)
			continue;
		if(ether->shutdown == nil) {
			print("#l%d: no shutdown function\n", i);
			continue;
		}
		(*ether->shutdown)(ether);
	}
}


#define POLY 0xedb88320

/* really slow 32 bit crc for ethers */
ulong
ethercrc(uchar *p, int len)
{
	int i, j;
	ulong crc, b;

	crc = 0xffffffff;
	for(i = 0; i < len; i++){
		b = *p++;
		for(j = 0; j < 8; j++){
			crc = (crc>>1) ^ (((crc^b) & 1) ? POLY : 0);
			b >>= 1;
		}
	}
	return crc;
}

Dev etherdevtab = {
	'l',
	"ether",

	etherreset,
	devinit,
	ethershutdown,
	etherattach,
	etherwalk,
	etherstat,
	etheropen,
	ethercreate,
	etherclose,
	etherread,
	etherbread,
	etherwrite,
	etherbwrite,
	devremove,
	etherwstat,
};

enum { PktHdr = 42 };
typedef struct Netconsole Netconsole;
struct Netconsole {
	char buf[512];
	int n;
	Lock;
	Ether *ether;
};
static Netconsole *netcons;

extern ushort ipcsum(uchar *);

void
netconsputc(Uart *, int c)
{
	char *p;
	u16int cs;

	ilock(netcons);
	netcons->buf[netcons->n++] = c;
	if(c != '\n' && netcons->n < sizeof(netcons->buf)){
		iunlock(netcons);
		return;
	}
	p = netcons->buf;
	p[16] = netcons->n - 14 >> 8;
	p[17] = netcons->n - 14;
	p[24] = 0;
	p[25] = 0;
	cs = ipcsum((uchar*) p + 14);
	p[24] = cs >> 8;
	p[25] = cs;
	p[38] = netcons->n - 34 >> 8;
	p[39] = netcons->n - 34;
	memmove(p+Eaddrlen, netcons->ether->ea, Eaddrlen);
	qiwrite(netcons->ether->oq, p, netcons->n);
	netcons->n = PktHdr;
	iunlock(netcons);
	if(netcons->ether->transmit != nil)
		netcons->ether->transmit(netcons->ether);
}

PhysUart netconsphys = {
	.putc = netconsputc,
};
Uart netconsuart = { .phys = &netconsphys };

void
netconsole(void)
{
	char *p;
	char *r;
	int i;
	int srcport, devno, dstport;
	u8int srcip[4], dstip[4];
	u64int dstmac;
	Netconsole *nc;

	if((p = getconf("console")) == nil || strncmp(p, "net ", 4) != 0)
		return;
	p += 4;
	for(i = 0; i < 4; i++){
		srcip[i] = strtol(p, &r, 0);
		p = r + 1;
		if(i == 3) break;
		if(*r != '.') goto err;
	}
	if(*r == '!'){
		srcport = strtol(p, &r, 0);
		p = r + 1;
	}else
		srcport = 6665;
	if(*r == '/'){
		devno = strtol(p, &r, 0);
		p = r + 1;
	}else
		devno = 0;
	if(*r != ',') goto err;
	for(i = 0; i < 4; i++){
		dstip[i] = strtol(p, &r, 0);
		p = r + 1;
		if(i == 3) break;
		if(*r != '.') goto err;
	}
	if(*r == '!'){
		dstport = strtol(p, &r, 0);
		p = r + 1;
	}else
		dstport = 6666;
	if(*r == '/'){
		dstmac = strtoull(p, &r, 16);
		if(r - p != 12) goto err;
	}else
		dstmac = ((uvlong)-1) >> 16;
	if(*r != 0) goto err;
	
	if(devno >= MaxEther || etherxx[devno] == nil){
		print("netconsole: no device #l%d\n", devno);
		return;
	}
	
	nc = malloc(sizeof(Netconsole));
	if(nc == nil){
		print("netconsole: out of memory");
		return;
	}
	memset(nc, 0, sizeof(Netconsole));
	nc->ether = etherxx[devno];
	
	uchar header[PktHdr] = {
		/* 0 */ dstmac >> 40, dstmac >> 32, dstmac >> 24, dstmac >> 16, dstmac >> 8, dstmac >> 0,
		/* 6 */ 0, 0, 0, 0, 0, 0,
		/* 12 */ 0x08, 0x00,
		/* 14 */ 0x45, 0x00,
		/* 16 */ 0x00, 0x00, /* total length */
		/* 18 */ 0x00, 0x00, 0x00, 0x00,
		/* 22 */ 64, /* ttl */
		/* 23 */ 0x11, /* protocol */
		/* 24 */ 0x00, 0x00, /* checksum */
		/* 26 */ srcip[0], srcip[1], srcip[2], srcip[3],
		/* 30 */ dstip[0], dstip[1], dstip[2], dstip[3],
		/* 34 */ srcport >> 8, srcport, dstport >> 8, dstport,
		/* 38 */ 0x00, 0x00, /* length */
		/* 40 */ 0x00, 0x00 /* checksum */
	};
	
	memmove(nc->buf, header, PktHdr);
	nc->n = PktHdr;
	
	netcons = nc;
	consuart = &netconsuart;
	return;

err:
	print("netconsole: invalid string %#q\n", getconf("console"));
	print("netconsole: usage: srcip[!srcport][/srcdev],dstip[!dstport][/dstmac]\n");
}
