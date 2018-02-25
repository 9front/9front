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
extern ushort ipcsum(uchar *);

static Ether *etherxx[MaxEther];
static Ether *etherprobe(int cardno, int ctlrno, char *conf);

Chan*
etherattach(char* spec)
{
	ulong ctlrno;
	char *conf;
	Chan *chan;

	ctlrno = 0;
	if(*spec){
		ctlrno = strtoul(spec, &conf, 10);
		if(ctlrno >= MaxEther)
			error(Enodev);
		if(conf == spec)
			error(Ebadspec);
		if(*conf){
			if(*conf != ':')
				error(Ebadspec);
			*conf++ = 0;
			if(!iseve())
				error(Enoattach);
			if(etherxx[ctlrno] != nil)
				error(Einuse);
			etherxx[ctlrno] = etherprobe(-1, ctlrno, conf);
		}
	}
	if(etherxx[ctlrno] == nil)
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
	Ether *ether = etherxx[chan->dev];

	if(NETTYPE(chan->qid.path) == Ndataqid && ether->f[NETID(chan->qid.path)]->bridge)
		memset(ether->mactab, 0, sizeof(ether->mactab));

	netifclose(ether, chan);
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
	Block *bp;

	if(qwindow(f->in) <= 0)
		return;
	bp = iallocb(64);
	if(bp == nil)
		return;
	memmove(bp->wp, pkt, len < 64 ? len : 64);
	if(f->type != -2){
		u32int ms = TK2MS(MACHP(0)->ticks);
		bp->wp[58] = len>>8;
		bp->wp[59] = len;
		bp->wp[60] = ms>>24;
		bp->wp[61] = ms>>16;
		bp->wp[62] = ms>>8;
		bp->wp[63] = ms;
	}
	bp->wp += 64;
	qpass(f->in, bp);
}

static Macent*
macent(Ether *ether, uchar *ea)
{
	u32int h = (ea[0] | ea[1]<<8 | ea[2]<<16 | ea[3]<<24) ^ (ea[4] | ea[5]<<8);
	return &ether->mactab[h % nelem(ether->mactab)];
}

/*
 * Multiplex the packet to all the connections which want it.
 * If the packet is not to be used subsequently (tome || from == nil),
 * attempt to simply pass it into one of the connections, thereby
 * saving a copy of the data (usual case hopefully).
 */
static Block*
ethermux(Ether *ether, Block *bp, Netfile **from)
{
	Block *xbp;
	Etherpkt *pkt;
	Netfile *f, *x, **fp;
	int len, multi, tome, port, type, dispose;

	len = BLEN(bp);
	if(len < ETHERHDRSIZE)
		goto Drop;
	pkt = (Etherpkt*)bp->rp;
	if(!(multi = pkt->d[0] & 1)){
		tome = memcmp(pkt->d, ether->ea, Eaddrlen) == 0;
		if(!tome && from != nil && ether->prom == 0)
			return bp;
	} else {
		tome = 0;
		if(from == nil && ether->prom == 0
		&& memcmp(pkt->d, ether->bcast, Eaddrlen) != 0
		&& !activemulti(ether, pkt->d, Eaddrlen))
			goto Drop;
	}

	port = -1;
	if(ether->prom){
		if((from == nil || (*from)->bridge) && (pkt->s[0] & 1) == 0){
			Macent *t = macent(ether, pkt->s);
			t->port = from == nil ? 0 : 1+(from - ether->f);
			memmove(t->ea, pkt->s, Eaddrlen);
		}
		if(!tome && !multi){
			Macent *t = macent(ether, pkt->d);
			if(memcmp(t->ea, pkt->d, Eaddrlen) == 0)
				port = t->port;
		}
	}

	x = nil;
	type = (pkt->type[0]<<8)|pkt->type[1];
	dispose = tome || from == nil || port > 0;

	for(fp = ether->f; fp < &ether->f[Ntypes]; fp++){
		if((f = *fp) == nil)
			continue;
		if(f->type != type && f->type >= 0)
			continue;
		if(!tome && !multi && !f->prom)
			continue;
		if(f->bridge){
			if(tome || fp == from)
				continue;
			if(port >= 0 && port != 1+(fp - ether->f))
				continue;
		}
		if(f->headersonly || f->type == -2){
			etherrtrace(f, pkt, len);
			continue;
		}
		if(dispose && x == nil)
			x = f;
		else if((xbp = iallocb(len)) != nil){
			memmove(xbp->wp, pkt, len);
			xbp->wp += len;
			xbp->flag = bp->flag;
			if(qpass(f->in, xbp) < 0)
				ether->soverflows++;
		} else
			ether->soverflows++;
	}
	if(x != nil){
		if(qpass(x->in, bp) < 0)
			ether->soverflows++;
		return nil;
	}

	if(dispose){
Drop:		freeb(bp);
		return nil;
	}
	return bp;
}

void
etheriq(Ether* ether, Block* bp)
{
	ether->inpackets++;
	ethermux(ether, bp, nil);
}

static void
etheroq(Ether* ether, Block* bp, Netfile **from)
{
	if((*from)->bridge == 0)
		memmove(((Etherpkt*)bp->rp)->s, ether->ea, Eaddrlen);

	bp = ethermux(ether, bp, from);
	if(bp == nil)
		return;

	ether->outpackets++;
	qbwrite(ether->oq, bp);
	if(ether->transmit != nil)
		ether->transmit(ether);
}

static long
etherwrite(Chan* chan, void* buf, long n, vlong)
{
	Ether *ether = etherxx[chan->dev];
	Block *bp;
	int nn, onoff;
	Cmdbuf *cb;

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
	poperror();
	bp->wp += n;

	etheroq(ether, bp, &ether->f[NETID(chan->qid.path)]);
	return n;
}

static long
etherbwrite(Chan* chan, Block* bp, ulong)
{
	Ether *ether;
	long n = BLEN(bp);

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
	etheroq(ether, bp, &ether->f[NETID(chan->qid.path)]);
	return n;
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
etherprobe(int cardno, int ctlrno, char *conf)
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
		if(conf != nil){
			kstrdup(&ether->type, conf);
			ether->nopt = tokenize(ether->type, ether->opt, nelem(ether->opt));
			if(ether->nopt < 1)
				goto Nope;
			memmove(&ether->opt[0], &ether->opt[1], --ether->nopt*sizeof(ether->opt[0]));
		} else if(isaconfig("ether", ctlrno, ether) == 0)
			goto Nope;

		for(cardno = 0; cards[cardno].type != nil; cardno++)
			if(cistrcmp(cards[cardno].type, ether->type) == 0)
				break;
		if(cards[cardno].type == nil)
			goto Nope;

		for(i = 0; i < ether->nopt; i++){
			if(strncmp(ether->opt[i], "ea=", 3) == 0){
				if(parseether(ether->ea, &ether->opt[i][3]))
					memset(ether->ea, 0, Eaddrlen);
			}
		}
	}
	if(cardno >= MaxEther || cards[cardno].type == nil)
		goto Nope;
	snprint(ether->name, sizeof(ether->name), "ether%d", ctlrno);
	if(cards[cardno].reset(ether) < 0){
Nope:
		if(conf != nil) free(ether->type);	/* see kstrdup() above */
		free(ether);
		return nil;
	}
	ether->type = cards[cardno].type;

	print("#l%d: %s: %dMbps port 0x%luX irq %d ea %E\n",
		ctlrno, ether->type, ether->mbps, ether->port, ether->irq, ether->ea);

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

static void netconsole(int);

static void
etherreset(void)
{
	Ether *ether;
	int cardno, ctlrno;

	fmtinstall('E', eipfmt);

	for(ctlrno = 0; ctlrno < MaxEther; ctlrno++){
		if((ether = etherprobe(-1, ctlrno, nil)) == nil)
			continue;
		etherxx[ctlrno] = ether;
	}

	if(getconf("*noetherprobe") == nil){
		cardno = ctlrno = 0;
		while(cards[cardno].type != nil && ctlrno < MaxEther){
			if(etherxx[ctlrno] != nil){
				ctlrno++;
				continue;
			}
			if((ether = etherprobe(cardno, ctlrno, nil)) == nil){
				cardno++;
				continue;
			}
			etherxx[ctlrno] = ether;
			ctlrno++;
		}
	}

	netconsole(1);
}

static void
ethershutdown(void)
{
	Ether *ether;
	int i;

	netconsole(0);

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

static void
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

static PhysUart netconsphys = { .putc = netconsputc };
static Uart netconsuart = { .phys = &netconsphys };

static void
netconsole(int on)
{
	char *p;
	char *r;
	int i;
	int srcport, devno, dstport;
	u8int srcip[4], dstip[4];
	u64int dstmac;
	Netconsole *nc;

	if(!on){
		if(consuart == &netconsuart)
			consuart = nil;
		return;
	}

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
