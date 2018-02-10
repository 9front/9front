#include <u.h>
#include "dat.h"
#include "fns.h"
#include "mem.h"

enum {
	ETHLEN = 1600,
	UDPLEN = 576,
	NRX = 64,
	RXBASE = 128 * 1024 * 1024,
	
	ETHHEAD = 14,
	IPHEAD = 20,
	UDPHEAD = 8,
	
	BOOTREQ = 1,
	DHCPDISCOVER = 1,
	DHCPOFFER,
	DHCPREQUEST,
	DHCPDECLINE,
};

enum {
	NET_CTRL,
	NET_CFG,
	NET_STATUS,
	DMA_CFG = 4,
	TX_STATUS,
	RX_QBAR,
	TX_QBAR,
	RX_STATUS,
	INTR_STATUS,
	INTR_EN,
	INTR_DIS,
	INTR_MASK,
	PHY_MAINT,
	RX_PAUSEQ,
	TX_PAUSEQ,
	HASH_BOT = 32,
	HASH_TOP,
	SPEC_ADDR1_BOT,
	SPEC_ADDR1_TOP,
};

enum {
	MDCTRL,
	MDSTATUS,
	MDID1,
	MDID2,
	MDAUTOADV,
	MDAUTOPART,
	MDAUTOEX,
	MDAUTONEXT,
	MDAUTOLINK,
	MDGCTRL,
	MDGSTATUS,
	MDPHYCTRL = 0x1f,
};

enum {
	/* NET_CTRL */
	RXEN = 1<<2,
	TXEN = 1<<3,
	MDEN = 1<<4,
	STARTTX = 1<<9,
	/* NET_CFG */
	SPEED = 1<<0,
	FDEN = 1<<1,
	RX1536EN = 1<<8,
	GIGE_EN = 1<<10,
	RXCHKSUMEN = 1<<24,
	/* NET_STATUS */
	PHY_IDLE = 1<<2,
	/* DMA_CFG */
	TXCHKSUMEN  = 1<<11,
	/* TX_STATUS */
	TXCOMPL = 1<<5,
	/* MDCTRL */
	MDRESET = 1<<15,
	AUTONEG = 1<<12,
	FULLDUP = 1<<8,
	/* MDSTATUS */
	LINK = 1<<2,
	/* MDGSTATUS */
	RECVOK = 3<<12,
};

typedef struct {
	uchar edest[6];
	uchar esrc[6];
	ulong idest;
	ulong isrc;
	ushort dport, sport;
	ushort len;
	uchar data[UDPLEN];
} udp;

static ulong *eth0 = (ulong *) 0xe000b000;
static int phyaddr = 7;

static u32int myip, dhcpip, tftpip, xid;
static uchar mac[6] = {0x0E, 0xA7, 0xDE, 0xAD, 0xBE, 0xEF};
static uchar tmac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static char file[128];

static udp ubuf, urbuf;
static uchar txbuf[ETHLEN];
static ulong txdesc[4], *txact, *rxact;
static ulong rxdesc[NRX*2];

void
mdwrite(ulong *r, int reg, u16int val)
{
	while((r[NET_STATUS] & PHY_IDLE) == 0)
		;
	r[PHY_MAINT] = 1<<30 | 1<<28 | 1<<17 | phyaddr << 23 | reg << 18 | val;
	while((r[NET_STATUS] & PHY_IDLE) == 0)
		;
}

u16int
mdread(ulong *r, int reg)
{
	while((r[NET_STATUS] & PHY_IDLE) == 0)
		;
	r[PHY_MAINT] = 1<<30 | 1<<29 | 1<<17 | phyaddr << 23 | reg << 18;
	while((r[NET_STATUS] & PHY_IDLE) == 0)
		;
	return r[PHY_MAINT];
}

void
ethinit(ulong *r)
{
	int v;
	ulong *p;
	ulong d;

	r[NET_CTRL] = 0;
	r[RX_STATUS] = 0xf;
	r[TX_STATUS] = 0xff;
	r[INTR_DIS] = 0x7FFFEFF;
	r[RX_QBAR] = r[TX_QBAR] = 0;
	r[NET_CFG] = MDC_DIV << 18 | FDEN | SPEED | RX1536EN | GIGE_EN | RXCHKSUMEN;
	r[SPEC_ADDR1_BOT] = mac[0] | mac[1] << 8 | mac[2] << 16 | mac[3] << 24;
	r[SPEC_ADDR1_TOP] = mac[4] | mac[5] << 8;
	r[DMA_CFG] = TXCHKSUMEN | 0x18 << 16 | 1 << 10 | 3 << 8 | 0x10;

	txdesc[0] = 0;
	txdesc[1] = 1<<31;
	txdesc[2] = 0;
	txdesc[3] = 1<<31 | 1<<30;
	txact = txdesc;
	r[TX_QBAR] = (ulong) txdesc;
	for(p = rxdesc, d = RXBASE; p < rxdesc + nelem(rxdesc); d += ETHLEN){
		*p++ = d;
		*p++ = 0;
	}
	p[-2] |= 2;
	rxact = rxdesc;
	r[RX_QBAR] = (ulong) rxdesc;
	
	r[NET_CTRL] = MDEN;
//	mdwrite(r, MDCTRL, MDRESET);
	mdwrite(r, MDCTRL, AUTONEG);
	if((mdread(r, MDSTATUS) & LINK) == 0){
		puts("Waiting for Link ...\n");
		while((mdread(r, MDSTATUS) & LINK) == 0)
			;
	}
	*(u32int*)(SLCR_BASE + SLCR_UNLOCK) = UNLOCK_KEY;
	v = mdread(r, MDPHYCTRL);
	if((v & 0x40) != 0){
		puts("1000BASE-T");
		while((mdread(r, MDGSTATUS) & RECVOK) != RECVOK)
			;
		r[NET_CFG] |= GIGE_EN;
		*(u32int*)(SLCR_BASE + GEM0_CLK_CTRL) = 1 << 20 | 8 << 8 | 1;
	}else if((v & 0x20) != 0){
		puts("100BASE-TX");
		r[NET_CFG] = r[NET_CFG] & ~GIGE_EN | SPEED;
		*(u32int*)(SLCR_BASE + GEM0_CLK_CTRL) = 5 << 20 | 8 << 8 | 1;
	}else if((v & 0x10) != 0){
		puts("10BASE-T");
		r[NET_CFG] = r[NET_CFG] & ~(GIGE_EN | SPEED);
		*(u32int*)(SLCR_BASE + GEM0_CLK_CTRL) = 20 << 20 | 20 << 8 | 1;
	}else
		puts("???");
	*(u32int*)(SLCR_BASE + SLCR_UNLOCK) = LOCK_KEY;
	if((v & 0x08) != 0)
		puts(" Full Duplex\n");
	else{
		puts(" Half Duplex\n");
		r[NET_CFG] &= ~FDEN;
	}
	r[NET_CTRL] |= TXEN | RXEN;
}

void
ethtx(ulong *r, uchar *buf, int len)
{
	txact[0] = (ulong) buf;
	txact[1] = 1<<15 | len;
	if(txact == txdesc + nelem(txdesc) - 2){
		txact[1] |= 1<<30;
		txact = txdesc;
	}else
		txact += 2;
	r[TX_STATUS] = -1;
	r[NET_CTRL] |= STARTTX;
	while((r[TX_STATUS] & TXCOMPL) == 0)
		;
}

void
udptx(ulong *r, udp *u)
{
	uchar *p, *q;
	int n;
	
	p = q = txbuf;
	memcpy(p, u->edest, 6);
	memcpy(p + 6, u->esrc, 6);
	q += 12;
	*q++ = 8;
	*q++ = 0;

	*q++ = 5 | 4 << 4;
	*q++ = 0;
	n = IPHEAD + UDPHEAD + u->len;
	*q++ = n >> 8;
	*q++ = n;

	*q++ = 0x13;
	*q++ = 0x37;
	*q++ = 1<<6;
	*q++ = 0;

	*q++ = 1;
	*q++ = 0x11;
	*q++ = 0;
	*q++ = 0;
	q = u32put(q, u->isrc);
	q = u32put(q, u->idest);
	
	*q++ = u->sport >> 8;
	*q++ = u->sport;
	*q++ = u->dport >> 8;
	*q++ = u->dport;
	n = UDPHEAD + u->len;
	*q++ = n >> 8;
	*q++ = n;
	*q++ = 0;
	*q++ = 0;
	
	memcpy(q, u->data, u->len);
	ethtx(r, p, ETHHEAD + IPHEAD + UDPHEAD + u->len);
}

void
dhcppkg(ulong *r, int t)
{
	uchar *p;
	udp *u;
	
	u = &ubuf;
	p = u->data;
	*p++ = BOOTREQ;
	*p++ = 1;
	*p++ = 6;
	*p++ = 0;
	p = u32put(p, xid);
	p = u32put(p, 0x8000);
	memset(p, 0, 16);
	u32put(p + 8, dhcpip);
	p += 16;
	memcpy(p, mac, 6);
	p += 6;
	memset(p, 0, 202);
	p += 202;
	*p++ = 99;
	*p++ = 130;
	*p++ = 83;
	*p++ = 99;

	*p++ = 53;
	*p++ = 1;
	*p++ = t;
	if(t == DHCPREQUEST){
		*p++ = 50;
		*p++ = 4;
		p = u32put(p, myip);
		*p++ = 54;
		*p++ = 4;
		p = u32put(p, dhcpip);
	}
	
	*p++ = 0xff;

	memset(u->edest, 0xff, 6);
	memcpy(u->esrc, mac, 6);
	u->sport = 68;
	u->dport = 67;
	u->idest = -1;
	u->isrc = 0;
	u->len = p - u->data;
	udptx(r, u);
}

uchar *
ethrx(void)
{
	while((*rxact & 1) == 0)
		if(timertrig())
			return nil;
	return (uchar *) (*rxact & ~3);
}

void
ethnext(void)
{
	*rxact &= ~1;
	if((*rxact & 2) != 0)
		rxact = rxdesc;
	else
		rxact += 2;
}

void
arp(int op, uchar *edest, ulong idest)
{
	uchar *p;
	static uchar broad[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	p = txbuf;
	if(edest == nil)
		edest = broad;
	memcpy(p, edest, 6);
	memcpy(p + 6, mac, 6);
	p[12] = 8;
	p[13] = 6;
	p += 14;
	p = u32put(p, 0x00010800);
	p = u32put(p, 0x06040000 | op);
	memcpy(p, mac, 6);
	p = u32put(p + 6, myip);
	memcpy(p, edest, 6);
	p = u32put(p + 6, idest);
	ethtx(eth0, txbuf, p - txbuf);
}

void
arpc(uchar *p)
{
	p += 14;
	if(u32get(p) != 0x00010800 || p[4] != 6 || p[5] != 4 || p[6] != 0)
		return;
	switch(p[7]){
	case 1:
		if(myip != 0 && u32get(p + 24) == myip)
			arp(2, p + 8, u32get(p + 14));
		break;
	case 2:
		if(tftpip != 0 && u32get(p + 14) == tftpip)
			memcpy(tmac, p + 8, 6);
		break;
	}
}

udp *
udprx(void)
{
	uchar *p;
	ulong v;
	udp *u;

	u = &urbuf;
	for(;; ethnext()){
		p = ethrx();
		if(p == nil)
			return nil;
		if(p[12] != 8)
			continue;
		if(p[13] == 6){
			arpc(p);
			continue;
		}
		if(p[13] != 0)
			continue;
		p += ETHHEAD;
		if((p[0] >> 4) != 4 || p[9] != 0x11)
			continue;
		v = u32get(p + 16);
		if(v != (ulong) -1 && v != myip)
			continue;
		u->idest = v;
		u->isrc = u32get(p + 12);
		p += (p[0] & 0xf) << 2;
		u->sport = p[0] << 8 | p[1];
		u->dport = p[2] << 8 | p[3];
		u->len = p[4] << 8 | p[5];
		if(u->len < 8)
			continue;
		u->len -= 8;
		if(u->len >= sizeof(u->data))
			u->len = sizeof(u->data);
		memcpy(u->data, p + 8, u->len);
		ethnext();
		return u;
	}
}

void
arpreq(void)
{
	uchar *p;

	arp(1, nil, tftpip);
	timeren(ARPTIMEOUT);
	for(;; ethnext()){
		p = ethrx();
		if(p == nil){
			print("ARP timeout\n");
			timeren(ARPTIMEOUT);
			arp(1, nil, tftpip);
		}
		if(p[12] != 8 || p[13] != 6)
			continue;
		arpc(p);
		if(tmac[0] != 0xff)
			break;
	}
	timeren(-1);
}

void
dhcp(ulong *r)
{
	udp *u;
	uchar *p;
	uchar type;

	xid = 0xdeadbeef;
	tftpip = 0;
	dhcppkg(r, DHCPDISCOVER);
	timeren(DHCPTIMEOUT);
	for(;;){
		u = udprx();
		if(u == nil){
			timeren(DHCPTIMEOUT);
			dhcppkg(r, DHCPDISCOVER);
			print("DHCP timeout\n");
		}
		p = u->data;
		if(u->dport != 68 || p[0] != 2 || u32get(p + 4) != xid || u32get(p + 236) != 0x63825363)
			continue;
		p += 240;
		type = 0;
		dhcpip = 0;
		for(; p < u->data + u->len && *p != 0xff; p += 2 + p[1])
			switch(*p){
			case 53:
				type = p[2];
				break;
			case 54:
				dhcpip = u32get(p + 2);
				break;
			}
		if(type != DHCPOFFER)
			continue;
		p = u->data;
		if(p[108] == 0){
			print("Offer from %I for %I with no boot file\n", dhcpip, u32get(p + 16));
			continue;
		}
		myip = u32get(p + 16);
		tftpip = u32get(p + 20);
		memcpy(file, p + 108, 128);
		print("Offer from %I for %I with boot file '%s' at %I\n", dhcpip, myip, file, tftpip);
		break;
	}
	timeren(-1);
	dhcppkg(r, DHCPREQUEST);
}

udp *
tftppkg(void)
{
	udp *u;
	
	u = &ubuf;
	memcpy(u->edest, tmac, 6);
	memcpy(u->esrc, mac, 6);
	u->idest = tftpip;
	u->isrc = myip;
	u->sport = 69;
	u->dport = 69;
	return u;
}

void
tftp(ulong *r, char *q, uintptr base)
{
	udp *u, *v;
	uchar *p;
	int bn, len;

restart:
	u = tftppkg();
	p = u->data;
	*p++ = 0;
	*p++ = 1;
	do
		*p++ = *q;
	while(*q++ != 0);
	memcpy(p, "octet", 6);
	p += 6;
	u->len = p - u->data;
	udptx(r, u);
	timeren(TFTPTIMEOUT);
	
	for(;;){
		v = udprx();
		if(v == nil){
			print("TFTP timeout");
			goto restart;
		}
		if(v->dport != 69 || v->isrc != tftpip || v->idest != myip)
			continue;
		if(v->data[0] != 0)
			continue;
		switch(v->data[1]){
		case 3:
			bn = v->data[2] << 8 | v->data[3];
			len = v->len - 4;
			if(len < 0)
				continue;
			if(len > 512)
				len = 512;
			memcpy((char*)base + ((bn - 1) << 9), v->data + 4, len);
			if((bn & 127) == 0)
				putc('.');
			p = u->data;
			*p++ = 0;
			*p++ = 4;
			*p++ = bn >> 8;
			*p = bn;
			u->len = 4;
			udptx(r, u);
			if(len < 512){
				putc(10);
				timeren(-1);
				return;
			}
			timeren(TFTPTIMEOUT);
			break;
		case 5:
			v->data[v->len - 1] = 0;
			print("TFTP error: %s\n", v->data + 4);
			timeren(-1);
			return;
		}
	}
}

int
netboot(void)
{
	ethinit(eth0);
	myip = 0;
	dhcp(eth0);
	arpreq();
	tftp(eth0, file, TZERO);
	memset((void *) CONF, 0, CONFSIZE);
	tftp(eth0, "/cfg/pxe/0ea7deadbeef", CONF);
	return 1;
}
