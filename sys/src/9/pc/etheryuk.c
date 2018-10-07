/*
 * marvell 88e8057 yukon2
 * copyright © 2009-10 erik quanstrom
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

#define Pciwaddrh(x)	0
#define Pciwaddrl(x)	PCIWADDR(x)
#define is64()		(sizeof(uintptr) == 8)
#define dprint(...)	if(debug) print(__VA_ARGS__); else {}

extern	void	sfence(void);

enum {
	Nctlr	= 4,
	Nrb	= 1024,
	Rbalign	= 64,
	Fprobe	= 1<<0,
	Sringcnt	= 2048,
	Tringcnt	= 512,
	Rringcnt	= 512,
	Rringl	= Rringcnt - 8,
};

enum {
	/* pci registers */
	Pciphy	= 0x40,
	Pciclk	= 0x80,
	Pciasp	= 0x84,
	Pcistate	= 0x88,
	Pcicf0	= 0x90,
	Pcicf1	= 0x94,

	/* “csr” registers */
	Ctst	= 0x0004/2,		/* control and status */
	Pwrctl	= 0x0007,		/* power control */
	Isr	= 0x0008/4,		/* interrupt src */
	Ism	= 0x000c/4,		/* interrupt mask */
	Hwe	= 0x0010/4,		/* hw error */
	Hwem	= 0x0014/4,		/* hw error mask*/
	Isrc2	= 0x001c/4,
	Eisr	= 0x0024/4,
	Lisr	= 0x0028/4,		/* leave isr */
	Icr	= 0x002c/4,
	Macadr	= 0x0100,		/* mac address 2ports*3 */
	Pmd	= 0x0119,
	Maccfg	= 0x011a,
	Chip	= 0x011b,
	Ramcnt	= 0x011c,		/* # of 4k blocks */
	Hres	= 0x011e,
	Clkgate	= 0x011d,
	Clkctl	= 0x0120/4,
	Tstctl1	= 0x0158,
	Tstctl2	= 0x0159,
	Gpio	= 0x015c/4,

	Rictl	= 0x01a0,		/* ri ram buffer ctl */
	Rib	= 0x0190,		/* ri buffer0 */

	/* other unoffset registers */
	Asfcs	= 0x0e68,		/* asf command and status */
	Asfhost	= 0x0e6c/4,

	Statctl	= 0x0e80/4,		/* status */
	Stattl	= 0x0e84/2,		/* tail (previous) status addr */
	Stataddr	= 0x0e88/4,		/* status address low */
	Statth	= 0x0e98/2,
	Stathd	= 0x0e9c/2,
	Statwm	= 0x0eac,		/* stat watermark */
	Statiwm	= 0x0ead,		/* isr stat watermark */

	Dpolltm	= 0x0e08/4,		/* descriptor pool timer */

	/* timers */
	Tgv	= 0x0e14/4,		/* gmac timer current value */
	Tgc	= 0x0e18,		/* gmac timer ctl */
	Tgt	= 0x0e1a,		/* gmac timer test */

	Tsti	= 0x0ec0/4,		/* stat tx timer ini */
	Tlti	= 0x0eb0/4,		/* level */
	Titi	= 0x0ed0/4,		/* isr */

	Tstc	= 0x0ec8,		/* stat tx timer ctl */
	Tltc	= 0x0eb8,		/* level timer ctl */
	Titc	= 0x0ed8,		/* isr timer ctl */

	/* “gmac” registers */
	Stat	= 0x000/2,
	Ctl	= 0x004/2,
	Txctl	= 0x008/2,
	Rxctl	= 0x00c/2,
	Txflow	= 0x010/2,
	Txparm	= 0x014/2,
	Serctl	= 0x018/2,		/* serial mode */
	Mchash	= 0x034/2,		/* 4 registers; 4 bytes apart */

	/* interrupt sources and masks */
	Txirq	= 0x044/2,
	Rxirq	= 0x048/2,
	Trirq	= 0x04c/2,		/* tx/rx overflow irq source */
	Txmask	= 0x050/2,
	Rxmask	= 0x054/2,
	Trmask	= 0x058/2,

	Smictl	= 0x080/2,		/* serial mode control */
	Smidata	= 0x084/2,
	Phyaddr	= 0x088/2,

	Ea0	= 0x01c/2,		/* 3 16 bit gmac registers */
	Ea1	= 0x028/2,

	Stats	= 0x0100/4,

	/* mac registers */
	Txactl	= 0x210,			/* transmit arbiter ctl */

	Grxea	= 0x0c40/4,		/* rx fifo end address */
	Gfrxctl	= 0x0c48/4,		/* gmac rxfifo ctl */
	Grxfm	= 0x0c4c/4,		/* fifo flush mask */
	Grxft	= 0x0c50/4,		/* fifo flush threshold */
	Grxtt	= 0x0c54/4,		/* rx truncation threshold */
	Gmfea	= 0x0d40/4,		/* end address */
	Gmfae	= 0x0d44/4,		/* almost empty thresh */
	Gmfctl	= 0x0d48/4,		/* tx gmac fifo ctl */

	Rxphi	= 0x0c58,		/* pause high watermark */
	Rxplo	= 0x0c5c,		/* pause low watermark */

	Rxwp	= 0x0c60/4,
	Rxwlev	= 0x0c68/4,
	Rxrp	= 0x0c70/4,
	Rxrlev	= 0x0c78/4,

	Mac	= 0x0f00/4,		/* global mac control */
	Phy	= 0x0f04/4,		/* phy control register */

	Irq	= 0x0f08,		/* irq source */
	Irqm	= 0x0f0c,		/* irq mask */
	Linkctl	= 0x0f10,

	/* queue registers; all offsets from Qbase*/
	Qbase	= 0x0400,
	Qportsz	= 0x0080,		/* BOTCH; tx diff is 2x rx diff */

	Qr	= 0x000,
	Qtxs	= 0x200,
	Qtx	= 0x280,

	/* queue offsets */
	Qd	= 0x00,
	Qvlan	= 0x20,
	Qdone	= 0x24,
	Qaddrl	= 0x28,
	Qaddrh	= 0x2c,
	Qbc	= 0x30,
	Qcsr	= 0x34,			/* 32bit */
	Qtest	= 0x38,
	Qwm	= 0x40,

	/* buffer registers; all offsets from Rbase */
	Rbase	= 0x0800,

	Rstart	= 0x00,
	Rend	= 0x04,
	Rwp	= 0x08,
	Rrp	= 0x0c,
	Rpon	= 0x10,			/* pause frames on */
	Rpoff	= 0x14,			/* pause frames off */
	Rhon	= 0x18,			/* high-priority frames on */
	Rhoff	= 0x1c,			/* high-priority  frames off */
	Rctl	= 0x28,

	/* prefetch */
	Pbase	= 0x450,
	Pctl	= 0x00,
	Plidx	= 0x04,			/* last addr; 16 bit */
	Paddrl	= 0x08,
	Paddrh	= 0x0c,
	Pgetidx	= 0x10,			/* 16 bit */
	Pputidx	= 0x14,			/* 16 bit */
	Pfifow	= 0x20,			/* 8 bit */
	Pfifor	= 0x24,			/* 8 bit */
	Pfifowm	= 0x20,			/* 8 bit */

	/* indirect phy registers */
	Phyctl	= 0x000,
	Phystat	= 0x001,
	Phyid0	= 0x002,
	Phyid1	= 0x003,
	Phyana	= 0x004,			/* auto neg advertisement */
	Phylpa	= 0x005,			/* link partner ability */
	Phyanee	= 0x006,			/* auto neg adv expansion */
	Phynp	= 0x007,			/* next page */
	Phylnp	= 0x008,			/* link partner next page */
	Gbectl	= 0x009,
	Gbestat	= 0x00a,
	Phyphy	= 0x010,			/* phy specific ctl */
	Phylstat	= 0x011,
	Phyintm	= 0x012,			/* phy interrupt mask */
	Phyint	= 0x013,
	Phyextctl	= 0x014,
	Phyrxe	= 0x015,			/* rx error counter */
	Phypage	= 0x016,			/* external address */
	Phypadr	= 0x01d,			/* phy page address */
};

enum {
	/* Pciasp */
	Aspforce		= 1<<15,
	Aspglinkdn	= 1<<14,	/* gphy link down */
	Aspfempty	= 1<<13,
	Aspclkrun	= 1<<12,
	Aspmsk		= Aspforce | Aspglinkdn | Aspfempty | Aspclkrun,

	/* Pcistate */
	Vmain		= 3<<27,

	/* Stat */
	Sfast		= 1<<15,	/* 100mbit */
	Duplex		= 1<<14,
	Txnofc		= 1<<13,	/* tx flow control disabled */
	Link		= 1<<12,	/* link up */
	Pausest		= 1<<11,	/* pause state */
	Txactive		= 1<<10,
	Excesscol	= 1<<9,
	Latecol		= 1<<8,
	Physc		= 1<<5,	/* phy status change */
	Sgbe		= 1<<4,	/* gbe speed */
	Rxnofc		= 1<<2,	/* rx flow control disabled */
	Promisc		= 1<<1,	/* promiscuous mode enabled */

	/* Ctl */
	Promiscen	= 1<<14,
	Txfcdis		= 1<<13,
	Txen		= 1<<12,
	Rxen		= 1<<11,
	Bursten		= 1<<10,
	Loopen		= 1<<9,
	Gbeen		= 1<<7,
	Fpass		= 1<<6,	/* "force link pass" ? */
	Duplexen	= 1<<5,
	Rxfcdis		= 1<<4,
	Fasten		= 1<<3,	/* enable 100mbit */
	Adudis		= 1<<2,	/* disable auto upd duplex */
	Afcdis		= 1<<1,	/* disable auto upd flow ctl */
	Aspddis		= 1<<0,	/* disable auto upd speed */

	/* Rxctl */
	Ufilter		= 1<<15,	/* unicast filter */
	Mfilter		= 1<<14,	/* multicast filter */
	Rmcrc		= 1<<13,	/* remove frame crc */

	/* Serctl */
	Vlanen		= 1<<9,
	Jumboen		= 1<<8,

	/* Txactl */
	Txaclr		= 1<<1,
	Txarst		= 1<<0,

	/* Asfcs: yukex only */
	Asfbrrst		= 1<<9,	/* bridge reset */
	Asfcpurst	= 1<<8,	/* cpu reset */
	Asfucrst		= 3<<0,	/* µctlr reset */

	/* Asfcs */
	Asfhvos		= 1<<4,	/* os present */
	Asfrst		= 1<<3,
	Asfrun		= 1<<2,
	Asfcirq		= 1<<1,
	Afsirq		= 1<<0,

	/* Statctl */
	Statirqclr	= 1<<4,
	Staton		= 1<<3,
	Statoff		= 1<<2,
	Statclr		= 1<<1,
	Statrst		= 1<<0,

	/* Mac */
	Nomacsec	= 1<<13 | 1<<11,
	Nortx		= 1<<9,
	Macpause	= 1<<3,
	Macpauseoff	= 1<<2,
	Macrstclr	= 1<<1,
	Macrst		= 1<<0,

	/* Phy */
	Gphyrstclr	= 1<<1,
	Gphyrst		= 1<<0,

	/* Irqm */
	Txovfl		= 1<<5,	/* tx counter overflow */
	Rxovfl		= 1<<4,	/* rx counter overflow */
	Txurun		= 1<<3,	/* transmit fifo underrun */
	Txdone		= 1<<2,	/* frame tx done */
	Rxorun		= 1<<1,	/* rx fifo overrun */
	Rxdone		= 1<<0,	/* frame rx done */

	/* Linkctl */
	Linkclr		= 1<<1,
	Linkrst	 	= 1<<0,

	/* Smictl */
	Smiread		= 1<<5,
	Smiwrite		= 0<<5,
	Smirdone	= 1<<4,
	Smibusy		= 1<<3,

	/* Phyaddr */
	Mibclear		= 1<<5,

	/* Ctst */
	Asfdis		= 1<<12,	/* asf disable */
	Clken		= 1<<11,	/* enable clock */

	Swirq		= 1<<7,
	Swirqclr		= 1<<6,
	Mstopped	= 1<<5,	/* master is stopped */
	Mstop		= 1<<4,	/* stop master */
	Mstrclr		= 1<<3,	/* master reset clear */
	Mstrrset		= 1<<2,	/* master reset */
	Swclr		= 1<<1,
	Swrst		= 1<<0,

	/* Pwrctl */
	Vauxen		= 1<<7,
	Vauxdis		= 1<<6,
	Vccen		= 1<<5,
	Vccdis		= 1<<4,
	Vauxon		= 1<<3,
	Vauxoff		= 1<<2,
	Vccon		= 1<<1,
	Vccoff		= 1<<0,

	/* timers */
	Tstart		= 1<<2,
	Tstop		= 1<<1,
	Tclrirq		= 1<<0,

	/* Dpolltm */
	Pollstart		= 1<<1,
	Pollstop		= 1<<0,

	/* csr interrupts: Isrc2, Eisr, etc. */
	Ihwerr		= 1<<31,
	Ibmu		= 1<<30,	/* sring irq */
	Isoftware	= 1<<25,

	Iphy		= 1<<4,
	Imac		= 1<<3,
	Irx		= 1<<2,
	Itxs		= 1<<1,	/* descriptor error */
	Itx		= 1<<0,	/* descriptor error */

	Iport		= 0x1f,
	Iphy2base	= 8,
	Ierror		= (Imac | Itx | Irx)*(1 | 1<<Iphy2base),

	/* hwe interrupts: Hwe Hwem */
	Htsof		= 1<<29,	/* timer stamp overflow */
	Hsensor		= 1<<28,
	Hmerr		= 1<<27,	/* master error */
	Hstatus		= 1<<26,	/* status exception */
	Hpcie		= 1<<25,	/* pcie error */
	Hpcie2		= 1<<24,	/* " */

	Hrparity		= 1<<5,	/* ram read parity error */
	Hwparity		= 1<<4,	/* ram write parity error */
	Hmfault		= 1<<3,	/* mac fault */
	Hrxparity	= 1<<2,	/* rx parity */
	Htcptxs		= 1<<1,	/* tcp length mismatch */
	Htcptxa		= 1<<0,	/* tcp length mismatch */

	H1base		= 1<<0,
	H2base		= 1<<8,
	Hmask		= 0x3f,
	Hdflt		= Htsof | Hmerr | Hstatus | Hmask*(H1base | H2base),

	/* Clkctl */
	Clkdiven		= 1<<1,
	Clkdivdis	= 1<<0,

	/* Clkgate */
	Link2inactive	= 1<<7,

	/* Phyctl */
	Phyrst	= 1<<15,
	Phy100	= 1<<14,		/* manual enable 100mbit */
	Aneen	= 1<<12,		/* auto negotiation enable */
	Phyoff	= 1<<11,		/* turn phy off */
	Anerst	= 1<<9,		/* auto neg reset */
	Phydpx	= 1<<8,
	Phy1000	= 1<<5,		/* manual enable gbe */

	/* Phyana */
	Annp	= 1<<15,		/* request next page */
	Anack	= 1<<14,		/* ack rx (read only) */
	Anrf	= 1<<13,		/* remote fault */
	Anpa	= 1<<11,		/* try asymmetric pause */
	Anp	= 1<<10,		/* try pause */
	An100f	= 1<<8,
	An100h	= 1<<7,
	An10f	= 1<<6,
	An10h	= 1<<5,
	Anonly	= 1<<0,
	Anall	= An100f | An100h | An10f | An10h | Anonly,

	/* Gbectl */
	Gbef	= 1<<9,		/* auto neg gbe full */
	Gbeh	= 1<<8,		/* auto neg gbe half */
	Gbexf	= 1<<6,		/* auto neg gbe full fiber */
	Gbexh	= 1<<5,		/* auto neg gbe full fiber */

	/* Phyphy */
	Pptf	= 3<<14,		/* tx fifo depth */
	Pprf	= 3<<12,		/* rx fifo depth */
	Pped	= 3<<8,		/* energy detect */
	Ppmdix	= 3<<5,		/* mdix conf */
	Ppmdixa	= 3<<5,		/* automdix */

	Ppengy	= 1<<14,		/* fe+ enable energy detect */
	Ppscrdis	= 1<<9,		/* fe+ scrambler disable */
	Ppnpe	= 1<<12,		/* fe+ enable next page */

	/* Phylstat */
	Physpd	= 3<<14,
	Phydupx	= 1<<13,
	Phypr	= 1<<12,		/* page rx */
	Phydone	= 1<<11,		/* speed and duplex neg. done */
	Plink	= 1<<10,
	Pwirelen	= 7<<7,
	Pmdi	= 1<<6,
	Pdwnsh	= 1<<5,		/* downshift */
	Penergy	= 1<<4,		/* energy detect */
	Ptxpause = 1<<3,		/* tx pause enabled */
	Prxpause	= 1<<2,		/* rx pause enabled */
	Ppol	= 1<<2,		/* polarity */
	Pjarjar	= 1<<1,		/* mesa no understasa */

	/* Phyintm */
	Anerr	= 1<<15,		/* an error */
	Lsp	= 1<<14,		/* link speed change */
	Andc	= 1<<13,		/* an duplex change */
	Anok	= 1<<11,
	Lsc	= 1<<10,		/* link status change */
	Symerr	= 1<<9,		/* symbol error */
	Fcarr	= 1<<8,		/* false carrier */
	Fifoerr	= 1<<7,
	Mdich	= 1<<6,
	Downsh	= 1<<5,
	Engych	= 1<<4,		/* energy change */
	Dtech	= 1<<2,		/* dte power det status */
	Polch	= 1<<1,		/* polarity change */
	Jabber 	= 1<<0,

	/* Phyextctl */
	Dnmstr	= 1<<9,		/* master downshift; 0: 1x; 1: 2x; 2: 3x */
	Dnslv	= 1<<8,

	/* Tgc */
	Tgstart	= 1<<2,
	Tgstop	= 1<<1,
	Tgclr	= 1<<0,		/* clear irq */

	/* Tstctl1 */
	Tstwen	= 1<<1,		/* enable config reg r/w */
	Tstwdis	= 1<<0,		/* disable config reg r/w */

	/* Gpio */
	Norace	= 1<<13,

	/* Rictl */
	Rirpclr	= 1<<9,
	Riwpclr	= 1<<8,
	Riclr	= 1<<1,
	Rirst	= 1<<0,

	/* Rbase opcodes */
	Rsfon	= 1<<5,		/* enable store/fwd */
	Rsfoff	= 1<<4,
	Renable	= 1<<3,
	Rdisable	= 1<<2,
	Rrstclr	= 1<<1,
	Rrst	= 1<<0,

	/* Qbase opcodes */
	Qidle	= 1<<31,
	Qtcprx	= 1<<30,
	Qiprx	= 1<<29,
	Qrssen	= 1<<15,
	Qrssdis	= 1<<14,
	Qsumen	= 1<<13,		/* tcp/ip cksum */
	Qsumdis	= 1<<12,
	Qcirqpar	= 1<<11,		/* clear irq on parity errors */
	Qcirqck	= 1<<10,
	Qstop	= 1<<9,
	Qstart	= 1<<8,
	Qfifoon	= 1<<7,
	Qfifooff	= 1<<6,
	Qfifoen	= 1<<5,
	Qfiforst	= 1<<4,
	Qenable	= 1<<3,
	Qdisable	= 1<<2,
	Qrstclr	= 1<<1,
	Qrst	= 1<<0,

	Qallclr	= Qfiforst | Qfifooff | Qrstclr,
	Qgo	= Qcirqpar | Qcirqck | Qstart | Qfifoen | Qenable,

	/* Qtest bits */
	Qckoff	= 1<<31,		/* tx: auto checksum off */
	Qckon	= 1<<30,
	Qramdis	= 1<<24,		/* rx: ram disable */

	/* Pbase opcodes */
	Prefon	= 1<<3,		/* prefetch on */
	Prefoff	= 1<<2,
	Prefrstclr	= 1<<1,
	Prefrst	= 1<<0,

	/* ring opcodes */
	Hw	= 0x80,			/* bitmask */
	Ock	= 0x12,			/* tcp checksum start */
	Oaddr64	= 0x21,
	Obuf	= 0x40,
	Opkt	= 0x41,
	Orxstat	= 0x60,
	Orxts	= 0x61,			/* rx timestamp */
	Orxvlan	= 0x62,
	Orxchks	= 0x64,
	Otxidx	= 0x68,
	Omacs	= 0x6c,			/* macsec */
	Oputidx	= 0x70,

	/* ring status */
	Eop	= 0x80,

	/* Gfrxctl */
	Gftrunc	= 1<<27,
	Gftroff	= 1<<26,

	Gfroon	= 1<<19,	/* flush on rx overrun */
	Gfrooff	= 1<<18,
	Gffon	= 1<<7,	/* rx fifo flush mode on */
	Gffoff	= 1<<6,
	Gfon	= 1<<3,
	Gfoff	= 1<<2,
	Gfrstclr	= 1<<1,
	Gfrst	= 1<<0,

	/* Gmfctl */
	Gmfsfoff	= 1<<31,	/* disable store-forward (ec ultra) */
	Gmfsfon	= 1<<30,	/* able store-forward (ec ultra) */
	Gmfvon	= 1<<25,	/* vlan tag on */
	Gmfvoff	= 1<<24,	/* vlan off */
	Gmfjon	= 1<<23,	/* jumbo on (ec ultra) */
	Gmfjoff	= 1<<22,	/* jumbo off */
	Gmfcfu	= 1<<6,	/* clear fifio underrun irq */
	Gmfcfc	= 1<<5,	/* clear frame complete irq */
	Gmfcpe	= 1<<4,	/* clear parity error irq */
	Gmfon	= 1<<3,
	Gmfoff	= 1<<2,
	Gmfclr	= 1<<1,
	Gmfrst	= 1<<0,

	/* rx frame */
	Flen	= 0x7fff<<17,
	Fvlan	= 1<<13,
	Fjabbr	= 1<<12,
	Ftoosm	= 1<<11,
	Fmc	= 1<<10,	/* multicast */
	Fbc	= 1<<9,
	Fok	= 1<<8,	/* good frame */
	Fokfc	= 1<<7,
	Fbadfc	= 1<<6,
	Fmiierr	= 1<<5,
	Ftoobg	= 1<<4,	/* oversized */
	Ffrag	= 1<<3,	/* fragment */
	Fcrcerr	= 1<<1,
	Ffifoof	= 1<<0,	/* fifo overflow */
	Ferror	= Ffifoof | Fcrcerr | Ffrag | Ftoobg
		| Fmiierr | Fbadfc | Ftoosm | Fjabbr,

	/* rx checksum bits in Status.ctl */
	Badck	= 5,		/* arbitrary bad checksum */

	Ctcpok	= 1<<7,	/* tcp or udp cksum ok */
	Cisip6	= 1<<3,
	Cisip4	= 1<<1,

	/* more status ring rx bits */
	Rxvlan	= 1<<13,
	Rxjab	= 1<<12,	/* jabber */
	Rxsmall	= 1<<11,	/* too small */
	Rxmc	= 1<<10,	/* multicast */
	Rxbc	= 1<<9,	/* bcast */
	Rxok	= 1<<8,
	Rxfcok	= 1<<7,	/* flow control pkt */
	Rxfcbad	= 1<<6,
	Rxmiierr	= 1<<5,
	Rxbig	= 1<<4,	/* too big */
	Rxfrag	= 1<<3,
	Rxcrcerr	= 1<<1,
	Rxfov	= 1<<0,	/* fifo overflow */
	Rxerror	= Rxfov | Rxcrcerr | Rxfrag | Rxbig | Rxmiierr
		| Rxfcbad | Rxsmall | Rxjab,
};

enum {
	Ffiber	= 1<<0,
	Fgbe	= 1<<1,
	Fnewphy	= 1<<2,
	Fapwr	= 1<<3,
	Fnewle	= 1<<4,
	Fram	= 1<<5,
	Fancy	=Fgbe | Fnewphy | Fapwr,

	Yukxl	= 0,
	Yukecu,
	Yukex,
	Yukec,
	Yukfe,
	Yukfep,
	Yuksup,
	Yukul2,
	Yukba,		/* doesn't exist */
	Yukopt,
	Nyuk,
};

typedef struct Chipid Chipid;
typedef struct Ctlr Ctlr;
typedef void (*Freefn)(Block*);
typedef struct Kproc Kproc;
typedef struct Mc Mc;
typedef struct Stattab Stattab;
typedef struct Status Status;
typedef struct Sring Sring;
typedef struct Vtab Vtab;

struct Chipid {
	uchar	feat;
	uchar	okrev;
	uchar	mhz;
	char	*name;
};

struct Kproc {
	Rendez;
	uint	event;
};

struct Sring {
	uint	wp;
	uint	rp;
	uint	cnt;
	uint	m;
	Status	*r;
};

struct Ctlr {
	Pcidev	*p;
	Ctlr	*oport;		/* port 2 */
	uchar	qno;
	uchar	attach;
	uchar	rxinit;
	uchar	txinit;
	uchar	flag;
	uchar	feat;
	uchar	type;
	uchar	rev;
	uchar	nports;
	uchar	portno;
	uintptr	io;
	uchar	*reg8;
	ushort	*reg16;
	uint	*reg;
	uint	rbsz;
	uchar	ra[Eaddrlen];
	uint	mca;
	uint	nmc;
	Mc	*mc;
	void	*alloc;
	Sring	status;
	Sring	tx;
	Block	*tbring[Tringcnt];
	Sring	rx;
	Block	*rbring[Rringcnt];
	Kproc	txmit;
	Kproc	rxmit;
	Kproc	iproc;
};

struct Mc {
	Mc	*next;
	uchar	ea[Eaddrlen];
};

struct Stattab {
	uint	offset;
	char	*name;
};

struct Status {
	uchar	status[4];
	uchar	l[2];
	uchar	ctl;
	uchar	op;
};

struct Vtab {
	int	vid;
	int	did;
	int	mtu;
	char	*name;
};

static Chipid idtab[] = {
[Yukxl]		Fgbe | Fnewphy,		0xff,	156,	"yukon-2 xl",
[Yukecu]	Fancy,			0xff,	125,	"yukon-2 ec ultra",
[Yukex]		Fancy | Fnewle,		0xff,	125,	"yukon-2 extreme",
[Yukec]		Fgbe,			2,	125,	"yukon-2 ec",
[Yukfe]		0,			0xff,	100,	"yukon-2 fe",
[Yukfep]	Fnewphy|Fapwr | Fnewle,	0xff,	50,	"yukon-2 fe+",
[Yuksup]	Fgbe | Fnewphy | Fnewle,	0xff,	125,	"yukon-2 supreme",
[Yukul2]		Fgbe |Fapwr,		0xff,	125,	"yukon-2 ultra2",
[Yukba]		0,			0,	0,	"??",
[Yukopt]	Fancy,			0xff,	125,	"yukon-2 optima",
};

static Vtab vtab[] = {
	0x11ab,	0x4354,	1514,	"88e8040",	/* unsure on mtu */
	0x11ab,	0x4362,	1514,	"88e8053",
	0x11ab, 0x4363, 1514,	"88e8055",
	0x11ab,	0x4364,	1514,	"88e8056",
	0x11ab,	0x4380,	1514,	"88e8057",
	0x11ab,	0x436b,	1514,	"88e8071",	/* unsure on mtu */
	0x1186,	0x4b00,	9000,	"dge-560t",
	0x1186,	0x4b02,	1514,	"dge-550sx",
	0x1186,	0x4b03,	1514,	"dge-550t",
};

static Stattab stattab[] = {
	0,	"rx ucast",
	8,	"rx bcast",
	16,	"rx pause",
	24,	"rx mcast",
	32,	"rx chk seq",

	48,	"rx ok low",
	56,	"rx ok high",
	64,	"rx bad low",
	72,	"rx bad high",

	80,	"rx frames < 64",
	88,	"rx frames < 64 fcs",
	96,	"rx frames 64",
	104,	"rx frames 65-127",
	112,	"rx frames 128-255",
	120,	"rx frames 256-511",
	128,	"rx frames 512-1023",
	136,	"rx frames 1024-1518",
	144,	"rx frames 1519-mtu",
	152,	"rx frames too long",
	160,	"rx jabber",
	176,	"rx fifo oflow",

	192,	"tx ucast",
	200,	"tx bcast",
	208,	"tx pause",
	216,	"tx mcast",

	224,	"tx ok low",
	232,	"tx ok hi",

	240,	"tx frames 64",
	248,	"tx frames 65-127",
	256,	"tx frames 128-255",
	264,	"tx frames 256-511",
	272,	"tx frames 512-1023",
	280,	"tx frames 1024-1518",
	288,	"tx frames 1519-mtu",

	304,	"tx coll",
	312,	"tx late coll",
	320,	"tx excess coll",
	328,	"tx mul col",
	336,	"tx single col",
	344,	"tx underrun",
};

static	uint	phypwr[] = {1<<26, 1<<27};
static	uint	coma[] = {1<<28, 1<<29};
static	uchar	nilea[Eaddrlen];
static	int	debug;
static	Ctlr	*ctlrtab[Nctlr];
static	int	nctlr;

static int
icansleep(void *v)
{
	Kproc *k;

	k = v;
	return k->event != 0;
}

static void
unstarve(Kproc *k)
{
	k->event = 1;
	wakeup(k);
}

static void
starve(Kproc *k)
{
	sleep(k, icansleep, k);
	k->event = 0;
}

static int
getnslot(Sring *r, uint *wp, Status **t, uint n)
{
	int i;

	if(r->m - (int)(wp[0] - r->rp) < n)
		return -1;
	for(i = 0; i < n; i++)
		t[i] = r->r + (wp[0]++ & r->m);
	return 0;
}

static uint
macread32(Ctlr *c, uint r)
{
	return c->reg[c->portno*0x20 + r];
}

static void
macwrite32(Ctlr *c, uint r, uint v)
{
	c->reg[c->portno*0x20 + r] = v;
}

static uint
macread16(Ctlr *c, uint r)
{
	return c->reg16[c->portno*0x40 + r];
}

static void
macwrite16(Ctlr *c, uint r, uint v)
{
	c->reg16[c->portno*0x40 + r] = v;
}

static uint
macread8(Ctlr *c, uint r)
{
	return c->reg8[c->portno*0x80 + r];
}

static void
macwrite8(Ctlr *c, uint r, uint v)
{
	c->reg8[c->portno*0x80 + r] = v;
}

static uint gmac32[2] = {
	0x2800/4,
	0x3800/4,
};

static ushort
gmacread32(Ctlr *c, uint r)
{
	return c->reg[gmac32[c->portno] + r];
}

static void
gmacwrite32(Ctlr *c, uint r, uint v)
{
	c->reg[gmac32[c->portno] + r] = v;
}

static uint gmac[2] = {
	0x2800/2,
	0x3800/2,
};

static ushort
gmacread(Ctlr *c, uint r)
{
	return c->reg16[gmac[c->portno] + r];
}

static void
gmacwrite(Ctlr *c, uint r, ushort v)
{
	c->reg16[gmac[c->portno] + r] = v;
}

static uint
qrread(Ctlr *c, uint r)
{
	return c->reg[Qbase + c->portno*Qportsz + r>>2];
}

static void
qrwrite(Ctlr *c, uint r, uint v)
{
	c->reg[Qbase + c->portno*Qportsz + r>>2] = v;
}

static uint
qrread16(Ctlr *c, uint r)
{
	return c->reg16[Qbase + c->portno*Qportsz + r>>1];
}

static void
qrwrite16(Ctlr *c, uint r, uint v)
{
	c->reg16[Qbase + c->portno*Qportsz + r>>1] = v;
}

static uint
qrread8(Ctlr *c, uint r)
{
	return c->reg8[Qbase + c->portno*Qportsz + r>>0];
}

static void
qrwrite8(Ctlr *c, uint r, uint v)
{
	c->reg8[Qbase + c->portno*Qportsz + r>>0] = v;
}

static uint
rrread32(Ctlr *c, uint r)
{
	return c->reg[Rbase + c->portno*Qportsz + r>>2];
}

static void
rrwrite32(Ctlr *c, uint r, uint v)
{
	c->reg[Rbase + c->portno*Qportsz + r>>2] = v;
}

static void
rrwrite8(Ctlr *c, uint r, uint v)
{
	c->reg8[Rbase + c->portno*Qportsz + r] = v;
}

static uint
rrread8(Ctlr *c, uint r)
{
	return c->reg8[Rbase + c->portno*Qportsz + r];
}

static uint
prread32(Ctlr *c, uint r)
{
	return c->reg[Pbase + c->portno*Qportsz + r>>2];
}

static void
prwrite32(Ctlr *c, uint r, uint v)
{
	c->reg[Pbase + c->portno*Qportsz + r>>2] = v;
}

static uint
prread16(Ctlr *c, uint r)
{
	return c->reg16[Pbase + c->portno*Qportsz + r>>1];
}

static void
prwrite16(Ctlr *c, uint r, uint v)
{
	c->reg16[Pbase + c->portno*Qportsz + r>>1] = v;
}

static ushort
phyread(Ctlr *c, uint r)
{
	ushort v;

	gmacwrite(c, Smictl, Smiread | r<<6);
	for(;;){
		v = gmacread(c, Smictl);
		if(v == 0xffff)
			error("phy read");
		if(v & Smirdone)
			return gmacread(c, Smidata);
		microdelay(10);
	}
}

static ushort
phywrite(Ctlr *c, uint r, ushort v)
{
	gmacwrite(c, Smidata, v);
	gmacwrite(c, Smictl, Smiwrite | r<<6);
	for(;;){
		v = gmacread(c, Smictl);
		if(v == 0xffff)
			error("phy write");
		if((v & Smibusy) == 0)
			return gmacread(c, Smidata);
		microdelay(10);
	}
}

static uvlong lorder = 0x0706050403020100ull;

static uvlong
getle(uchar *t, int w)
{
	uint i;
	uvlong r;

	r = 0;
	for(i = w; i != 0; )
		r = r<<8 | t[--i];
	return r;
}

static void
putle(uchar *t, uvlong r, int w)
{
	uchar *o, *f;
	uint i;

	f = (uchar*)&r;
	o = (uchar*)&lorder;
	for(i = 0; i < w; i++)
		t[o[i]] = f[i];
}

static void
bufinit(Ctlr *c, uint q, uint start, uint end)
{
	uint t;

	rrwrite8(c, q + Rctl, Rrstclr);
	rrwrite32(c, q + Rstart, start);
	rrwrite32(c, q + Rend, end-1);
	rrwrite32(c, q + Rwp, start);
	rrwrite32(c, q + Rrp, start);

	if(q == Qr || q == Qr + Qportsz){
		t = start-end;
		rrwrite32(c, q + Rpon, t - 8192/8);
		rrwrite32(c, q + Rpoff, t - 16384/8);
	} else
		rrwrite8(c, q + Rctl, Rsfon);
	rrwrite8(c, q + Rctl, Renable);
	rrread8(c, q + Rctl);
}

static void
qinit(Ctlr *c, uint queue)
{
	qrwrite(c, queue + Qcsr, Qallclr);
	qrwrite(c, queue + Qcsr, Qgo);
	qrwrite(c, queue + Qcsr, Qfifoon);
	qrwrite16(c, queue + Qwm,  0x600);		/* magic */
//	qrwrite16(c, queue + Qwm,  0x80);		/* pcie magic; assume pcie; no help */
}

/* initialized prefetching */
static void
pinit(Ctlr *c, uint queue, Sring *r)
{
	union {
		uchar	u[4];
		uint	l;
	} u;

	prwrite32(c, queue + Pctl, Prefrst);
	prwrite32(c, queue + Pctl, Prefrstclr);
	putle(u.u, Pciwaddrh(r->r), 4);
	prwrite32(c, queue + Paddrh, u.l);
	putle(u.u, Pciwaddrl(r->r), 4);
	prwrite32(c, queue + Paddrl, u.l);
	prwrite16(c, queue + Plidx, r->m);
	prwrite32(c, queue + Pctl, Prefon);
	prread32(c, queue + Pctl);
}

static void
txinit(Ether *e)
{
	Ctlr *c;
	Sring *r;

	c = e->ctlr;
	r = &c->tx;
	if(c->txinit == 1)
		return;
	c->txinit = 1;
	r->wp = 0;
	r->rp = 0;
	qinit(c, Qtx);
	pinit(c,  Qtx, &c->tx);
}

static void
linkup(Ctlr *c, uint w)
{
	static Lock l;

	lock(&l);
	gmacwrite(c, Ctl, w|gmacread(c, Ctl));
	unlock(&l);
}

static void
tproc(void *v)
{
	Block *b;
	Ctlr *c;
	Ether *e;
	Sring *r;
	Status *tab[2], *t;

	e = v;
	c = e->ctlr;
	r = &c->tx;

	txinit(e);
	linkup(c, Txen);
	while(waserror())
		;
	for(;;){
		if((b = qbread(e->oq, 100000)) == nil)
			break;
		while(getnslot(r, &r->wp, tab, 1 + is64()) == -1)
			starve(&c->txmit);
		t = tab[is64()];
		c->tbring[t - r->r] = b;
		if(is64()){
			Status *t = tab[0];
			t->ctl = 0;
			t->op = Oaddr64 | Hw;
			putle(t->status, Pciwaddrh(b->rp), 4);
		}
		putle(t->status, Pciwaddrl(b->rp), 4);
		putle(t->l, BLEN(b), 2);
		t->op = Opkt | Hw;
		t->ctl = Eop;
		sfence();
		prwrite16(c, Qtx + Pputidx, r->wp & r->m);
	}
	print("#l%d: tproc: queue closed\n", e->ctlrno);
	pexit("queue closed", 1);
}

static void
rxinit(Ether *e)
{
	Ctlr *c;
	Sring *r;
	Status *t;

	c = e->ctlr;
	r = &c->rx;
	if(c->rxinit == 1)
		return;
	c->rxinit = 1;
	qinit(c, Qr);
	if(c->type == Yukecu && (c->rev == 2 || c->rev == 3))
		qrwrite(c, Qr + Qtest, Qramdis);
	pinit(c,  Qr, &c->rx);

	if((c->flag & Fnewle) == 0){
		while(getnslot(r, &r->wp, &t, 1) == -1)
			starve(&c->rxmit);
		putle(t->status, 14<<16 | 14, 4);
		t->ctl = 0;
		t->op = Ock | Hw;
		qrwrite(c, Qr + Qcsr, Qsumen);
	}
	macwrite32(c, Gfrxctl, Gftroff);
}

/* debug; remove */
#include "yukdump.h"
static int
rxscrew(Ether *e, Sring *r, Status *t, uint wp)
{
	Ctlr *c;

	c = e->ctlr;
	if((int)(wp - r->rp) >= r->cnt){
		print("rxscrew1 wp %ud(%ud) rp %ud %zd\n", wp, r->wp, r->rp, t-r->r);
		return -1;
	}
	if(c->rbring[t - r->r]){
		print("rxscrew2 wp %ud rp %ud %zd\n", wp, r->rp, t-r->r);
		descriptorfu(e, Qr);
		return -1;
	}
	return 0;
}

static int
replenish(Ether *e, Ctlr *c)
{
	int n, lim;
	uint wp;
	Block *b;
	Sring *r;
	Status *tab[2], *t;

	r = &c->rx;
	wp = r->wp;

	lim = r->cnt/2;
	if(lim > 128)
		lim = 128;		/* hw limit? */
	for(n = 0; n < lim; n++){
		b = iallocb(c->rbsz + Rbalign);
		if(b == nil || getnslot(r, &wp, tab, 1 + is64()) == -1){
			freeb(b);
			break;
		}
		b->rp = b->wp = (uchar*)ROUND((uintptr)b->base, Rbalign);

		t = tab[is64()];
		if(rxscrew(e, r, t, wp) == -1){
			freeb(b);
			break;
		}
		c->rbring[t - r->r] = b;

		if(is64()){
			Status *t = tab[0];
			putle(t->status, Pciwaddrh(b->wp), 4);
			t->ctl = 0;
			t->op = Oaddr64 | Hw;
		}
		putle(t->status, Pciwaddrl(b->wp), 4);
		putle(t->l, c->rbsz, 2);
		t->ctl = 0;
		t->op = Opkt | Hw;
	}
	if(n>0){
		r->wp = wp;
		sfence();
		prwrite16(c, Qr + Pputidx, wp & r->m);
		dprint("yuk: replenish %d %ud-%ud [%d-%d]\n", n, r->rp, wp, r->rp&r->m, wp&r->m);
	}
	return n == lim;
}

static void
rproc(void *v)
{
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;

	rxinit(e);
	linkup(c, Rxen);
	while(waserror())
		;
	for(;;){
		if(replenish(e, c) == 0)
			starve(&c->rxmit);
	}
}

static void
promiscuous(void *a, int on)
{
	uint r;
	Ether *e;
	Ctlr *c;

	e = a;
	c = e->ctlr;
	r = gmacread(c, Rxctl);
	if(on)
		r &= ~(Ufilter|Mfilter);
	else
		r |= Ufilter|Mfilter;
	gmacwrite(c, Rxctl, r);
}

static uchar pauseea[] = {1, 0x80, 0xc2, 0, 0, 1};

static void
multicast(void *a, uchar *ea, int on)
{
	uchar f[8];
	uint i, r, b;
	Ctlr *c;
	Ether *e;
	Mc **ll, *l, *p;

	e = a;
	c = e->ctlr;
	r = gmacread(c, Rxctl);
	if(on){
		for(ll = &c->mc; *ll != nil; ll = &(*ll)->next)
			if(memcmp((*ll)->ea, ea, Eaddrlen) == 0)
				return;
		*ll = malloc(sizeof **ll);
		memmove((*ll)->ea, ea, Eaddrlen);
	}else{
		for(p = nil, l = c->mc; l != nil; p = l, l = l->next)
			if(memcmp(l->ea, ea, Eaddrlen) == 0)
				break;
		if(l == nil)
			return;
		if(p != nil)
			p->next = l->next;
		else
			c->mc = l->next;
		free(l);
	}
	memset(f, 0, sizeof f);
	if(0 /* flow control */){
		b = ethercrc(pauseea, Eaddrlen) & 0x3f;
		f[b>>3] |= 1 << (b & 7);
	}
	for(l = c->mc; l != nil; l = l->next){
		b = ethercrc(l->ea, Eaddrlen) & 0x3f;
		f[b>>3] |= 1 << (b & 7);
	}
	for(i = 0; i < sizeof f / 2; i++)
		gmacwrite(c, Mchash + 2*i, f[i] | f[i+1]<<8);
	gmacwrite(c, Rxctl, r | Mfilter);
}

static int spdtab[4] = {
	10, 100, 1000, 0,
};

static void
link(Ether *e)
{
	uint i, s, spd;
	Ctlr *c;

	c = e->ctlr;
	i = phyread(c, Phyint);
	s = phyread(c, Phylstat);
	dprint("#l%d: yuk: link %.8ux %.8ux\n", e->ctlrno, i, s);
	spd = 0;
	e->link = (s & Plink) != 0;
	if(e->link && c->feat&Ffiber)
		spd = 1000;
	else if(e->link){
		spd = s & Physpd;
		spd >>= 14;
		spd = spdtab[spd];
	}
	e->mbps = spd;
	dprint("#l%d: yuk: link %d spd %d\n", e->ctlrno, e->link, e->mbps);
}

static void
txcleanup(Ctlr *c, uint end)
{
	uint rp;
	Block *b;
	Sring *r;
	Status *t;

	r = &c->tx;
	end &= r->m;
	for(rp = r->rp & r->m; rp != end; rp = r->rp & r->m){
		t = r->r + rp;
		r->rp++;
		if((t->ctl & Eop) == 0)
			continue;
		b = c->tbring[rp];
		c->tbring[rp] = nil;
		if(b != nil)
			freeb(b);
	}
	unstarve(&c->txmit);
}

static void
rx(Ether *e, uint l, uint x, uint flag)
{
	uint cnt, i, rp;
	Block *b;
	Ctlr *c;
	Sring *r;

	c = e->ctlr;
	r = &c->rx;
	for(rp = r->rp;;){
		if(rp == r->wp){
			print("#l%d: yuk rx empty\n", e->ctlrno);
			return;
		}
		i = rp++&r->m;
		b = c->rbring[i];
		c->rbring[i] = nil;
		if(b != nil)
			break;
	}
	r->rp = rp;
	cnt = x>>16 & 0x7fff;
	if((cnt != l || x&Rxerror) &&
	!(c->type == Yukfep && c->rev == 0)){
		print("#l%d: yuk rx error %.4ux\n", e->ctlrno, x&0xffff);
		freeb(b);
	}else{
		b->wp += l;
		b->flag |= flag;
		etheriq(e, b);
	}
	unstarve(&c->rxmit);
}

static uint
cksum(Ctlr *c, uint ck, uint css)
{
	if(c->flag & Fnewle && css&(Cisip4|Cisip6) && css&Ctcpok)
		return Bipck | Btcpck | Budpck;
	else if(ck == 0xffff || ck == 0)
		return Bipck;
	return 0;
}

static void
sring(Ether *e)
{
	uint i, lim, op, l, x;
	Ctlr *c;
	Sring *r;
	Status *s;
	static uint ck = Badck;

	c = e->ctlr;
	r = &c->status;
	lim = c->reg16[Stathd] & r->m;
	for(;;){
		i = r->rp & r->m;
		if(i == lim){
			lim = c->reg16[Stathd] & r->m;
			if(i == lim)
				break;
		}
		s = r->r + i;
		op = s->op;
		if((op & Hw) == 0)
			break;
		op &= ~Hw;
		switch(op){
		case Orxchks:
			ck = getle(s->status, 4) & 0xffff;
			break;
		case Orxstat:
			l = getle(s->l, 2);
			x = getle(s->status, 4);
			rx(e, l, x, cksum(c, ck, s->ctl));
			ck = Badck;
			break;
		case Otxidx:
			l = getle(s->l, 2);
			x = getle(s->status, 4);
			txcleanup(c, x & 0xfff);

			x = l>>24 & 0xff | l<< 8;
			x &= 0xfff;
			if(x != 0 && c->oport)
				txcleanup(c->oport, x);
			break;
		default:
			print("#l%d: yuk: funny opcode %.2ux\n", e->ctlrno, op);
			break;
		}
		s->op = 0;
		r->rp++;
	}
	c->reg[Statctl] = Statirqclr;
}

enum {
	Pciaer	= 0x1d00,
	Pciunc	= 0x0004,
};

static void
hwerror(Ether *e, uint cause)
{
	uint u;
	Ctlr *c;

	c = e->ctlr;
	cause = c->reg[Hwe];
	if(cause == 0)
		print("hwe: no cause\n");
	if(cause & Htsof){
		c->reg8[Tgc] = Tgclr;
		cause &= ~Htsof;
	}
	if(cause & (Hmerr | Hstatus)){
		c->reg8[Tstctl1] = Tstwen;
		u = pcicfgr16(c->p, PciPSR) | 0x7800;
		pcicfgw16(c->p, PciPSR, u);
		c->reg8[Tstctl1] = Tstwdis;
		cause &= ~(Hmerr | Hstatus);
	}
	if(cause & Hpcie){
		c->reg8[Tstctl1] = Tstwen;
		c->reg[Pciaer + Pciunc>>2] = ~0;
		u =  c->reg[Pciaer + Pciunc>>2];
		USED(u);
		print("#l%d: pcierror %.8ux\n", e->ctlrno, u);
		c->reg8[Tstctl1] = Tstwdis;
		cause &= ~Hpcie;
	}
	if(cause & Hrxparity){
		print("#l%d: ram parity read error.  bug? ca %.8ux\n", e->ctlrno, cause);
		qrwrite(c, Qtx + Qcsr, Qcirqpar);
		cause &= ~Hrxparity;
	}
	if(cause & Hrparity){
		print("#l%d: ram parity read error.  bug? ca %.8ux\n", e->ctlrno, cause);
		descriptorfu(e, Qr);
		descriptorfu(e, Qtx);
		c->reg16[Rictl + c->portno*0x40>>1] = Rirpclr;
		cause &= ~Hrparity;
	}
	if(cause & Hwparity){
		print("#l%d: ram parity write error.  bug? ca %.8ux\n", e->ctlrno, cause);
		descriptorfu(e, Qr);
		descriptorfu(e, Qtx);
		c->reg16[Rictl + c->portno*0x40>>1] = Riwpclr;
		cause &= ~Hwparity;
	}
	if(cause & Hmfault){
		print("#l%d: mac parity error\n", e->ctlrno);
		macwrite32(c, Gmfctl, Gmfcpe);
		cause &= ~Hmfault;
	}
	if(cause)
		print("#l%d: leftover hwe %.8ux\n", e->ctlrno, cause);
}

static void
macintr(Ether *e)
{
	uint cause;
	Ctlr *c;

	c = e->ctlr;
	cause = macread8(c, Irq);
	cause  &= ~(Rxdone | Txdone);
	if(cause == 0)
		return;
	print("#l%d: mac error %.8ux\n", e->ctlrno, cause);
	if(cause & Txovfl){
		gmacread32(c, Txirq);
		cause &= ~Txovfl;
	}
	if(cause & Rxovfl){
		gmacread32(c, Rxirq);
		cause &= ~Rxovfl;
	}
	if(cause & Rxorun){
		macwrite32(c, Gfrxctl, Gmfcfu);
		cause &= ~Rxorun;
	}
	if(cause & Txurun){
		macwrite32(c, Gmfctl, Gmfcfu);
		cause &= ~Txurun;
	}
	if(cause)
		print("#l%d: leftover mac error %.8ux\n", e->ctlrno, cause);
}

static struct {
	uint	i;
	uint	q;
	char	*s;
} emap[] = {
	Irx,		Qr,		"qr",
	Itxs,		Qtxs,		"qtxs",
	Itx,		Qtx,		"qtx",
	Irx<<Iphy2base,	Qr + 0x80,	"qr1",
	Itxs<<Iphy2base,	Qtxs + 0x100,	"qtxs1",
	Itx<<Iphy2base,	Qtx + 0x100,	"qtx1",
};

static void
eerror(Ether *e, uint cause)
{
	uint i, o, q;
	Ctlr *c;

	c = e->ctlr;

	if(cause & Imac){
		macintr(e);
		cause &= ~Imac;
	}
	if(cause & (Irx | Itxs | Itx)*(1 | 1<<Iphy2base))
		for(i = 0; i < nelem(emap); i++){
			if((cause & emap[i].i) == 0)
				continue;
			q = emap[i].q;
			o = prread16(c, q + Pgetidx);
			print("#l%d: yuk: bug: %s: @%d ca=%.8ux\n", 
				e->ctlrno, emap[i].s, o, cause);
			descriptorfu(e, q);
			qrwrite(c, emap[i].q + Qcsr, Qcirqck);
			cause &= ~emap[i].i;
		}
	if(cause)
		print("#l%d: leftover error %.8ux\n", e->ctlrno, cause);
}

static void
iproc(void *v)
{
	uint cause, d;
	Ether *e;
	Ctlr *c;

	e = v;
	c = e->ctlr;
	while(waserror())
		;
	for(;;){
		starve(&c->iproc);
		cause = c->reg[Eisr];
		if(cause & Iphy)
			link(e);
		if(cause & Ihwerr)
			hwerror(e, cause);
		if(cause & Ierror)
			eerror(e, cause & Ierror);
		if(cause & Ibmu)
			sring(e);
		d = c->reg[Lisr];
		USED(d);
	}
}

static void
interrupt(Ureg*, void *v)
{
	uint cause;
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;

	/* reading Isrc2 masks interrupts */
	cause = c->reg[Isrc2];
	if(cause == 0 || cause == ~0){
		/* reenable interrupts */
		c->reg[Icr] = 2;
		return;
	}
	unstarve(&c->iproc);
}

static void
storefw(Ctlr *c)
{
	if(c->type == Yukex && c->rev != 1
	|| c->type == Yukfep
	|| c->type == Yuksup)
		macwrite32(c, Gmfctl, Gmfjon | Gmfsfon);
	else{
		macwrite32(c, Gmfae, 0x8000 | 0x70);	/* tx gmac fifo */
		macwrite32(c, Gmfctl, Gmfsfoff);
	}
}

static void
raminit(Ctlr *c)
{
	uint ram, rx;

	if(ram = c->reg8[Ramcnt] * 4096/8){	/* in qwords */
		c->flag |= Fram;
		rx = ROUNDUP((2*ram)/3, 1024/8);
		bufinit(c, Qr, 0, rx);
		bufinit(c, Qtx, rx, ram);
		rrwrite8(c, Qtxs + Rctl, Rrst);	/* sync tx off */
	}else{
		macwrite8(c, Rxplo, 768/8);
		macwrite8(c, Rxphi, 1024/8);
		storefw(c);
	}
}

static void
attach(Ether *e)
{
	char buf[KNAMELEN];
	Ctlr *c;
	static Lock l;

	c = e->ctlr;
	if(c->attach == 1)
		return;
	lock(&l);
	if(c->attach == 1){
		unlock(&l);
		return;
	}
	c->attach = 1;
	unlock(&l);

	snprint(buf, sizeof buf, "#l%dtproc", e->ctlrno);
	kproc(buf, tproc, e);
	snprint(buf, sizeof buf, "#l%drproc", e->ctlrno);
	kproc(buf, rproc, e);
	snprint(buf, sizeof buf, "#l%diproc", e->ctlrno);
	kproc(buf, iproc, e);

 	c->reg[Ism] |= Ibmu | Iport<<Iphy2base*c->portno;
}

static long
ifstat(Ether *e0, void *a, long n, ulong offset)
{
	char *s, *e, *p;
	int i;
	uint u;
	Ctlr *c;

	c = e0->ctlr;
	p = s = malloc(READSTR);
	e = p + READSTR;
	for(i = 0; i < nelem(stattab); i++){
		u = gmacread32(c, Stats + stattab[i].offset/4);
		if(u > 0)
			p = seprint(p, e, "%s\t%ud\n", stattab[i].name, u);
	}
	p = seprint(p, e, "stat %.4ux ctl %.3ux\n", gmacread(c, Stat), gmacread(c, Ctl));
	p = seprint(p, e, "pref %.8ux %.4ux\n", prread32(c, Qr + Pctl), prread16(c, Qr + Pgetidx));
	if(debug){
		p = dumppci(c, p, e);
		p = dumpgmac(c, p, e);
		p = dumpmac(c, p, e);
		p = dumpreg(c, p, e);
	}
	seprint(p, e, "%s rev %d phy %s\n", idtab[c->type].name,
		c->rev, c->feat&Ffiber? "fiber": "copper");
	n = readstr(offset, a, n, s);
	free(s);
	return n;
}

static Cmdtab ctltab[] = {
	1,	"debug",		1,
	2,	"descriptorfu",	1,
};

static long
ctl(Ether *e, void *buf, long n)
{
	Cmdbuf *cb;
	Cmdtab *t;

	cb = parsecmd(buf, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	t = lookupcmd(cb, ctltab, nelem(ctltab));
	switch(t->index){
	case 0:
		debug ^= 1;
		break;
	case 1:
		descriptorfu(e, Qr);
		break;
	}
	free(cb);
	poperror();
	return n;
}

static uint
yukpcicfgr32(Ctlr *c, uint r)
{
	return c->reg[r + 0x1c00>>2];
}

static void
yukpcicfgw32(Ctlr *c, uint r, uint v)
{
	c->reg[r + 0x1c00>>2] = v;
}

static void
phypower(Ctlr *c)
{
	uint u, u0;

	u = u0 = yukpcicfgr32(c, Pciphy);
	u &= ~phypwr[c->portno];
	if(c->type == Yukxl && c->rev > 1)
		u |= coma[c->portno];
	if(u != u0 || 1){
		c->reg8[Tstctl1] = Tstwen;
		yukpcicfgw32(c, Pciphy, u);
		c->reg8[Tstctl1] = Tstwdis;
	}
	if(c->type == Yukfe)
		c->reg8[Phyctl] = Aneen;
	else if(c->flag & Fapwr)
		macwrite32(c, Phy, Gphyrstclr);
}

static void
phyinit(Ctlr *c)
{
	uint u;

	if((c->feat & Fnewphy) == 0){
		u = phyread(c, Phyextctl);
		u &= ~0xf70;			/* clear downshift counters */
		u |= 0x7<<4;			/* mac tx clock = 25mhz */
		if(c->type == Yukec)
			u |= 2*Dnmstr | Dnslv;
		else
			u |= Dnslv;
		phywrite(c, Phyextctl, u);
	}
	u = phyread(c, Phyphy);

	/* questionable value */
	if(c->feat & Ffiber)
		u &= ~Ppmdix;
	else if(c->feat & Fgbe){
		u &= ~Pped;
		u |= Ppmdixa;
		if(c->flag & Fnewphy){
		//	u &= ~(7<<12);
		//	u |= 2*(1<<12) | 1<<11;	/* like 2*Dnmstr | Dnslv */
			u |= 2*(1<<9) | 1<<11;
		}
	}else{
		u |= Ppmdixa >> 1;		/* why the shift? */
		if(c->type == Yukfep && c->rev == 0){
		}
	}

	phywrite(c, Phyphy, u);
	/* copper/fiber specific stuff gmacwrite(c, Ctl, 0); */
	gmacwrite(c, Ctl, 0);
	if(c->feat & Fgbe)
		if(c->feat & Ffiber)
			phywrite(c, Gbectl, Gbexf | Gbexh);
		else
			phywrite(c, Gbectl, Gbef | Gbeh);
	phywrite(c, Phyana, Anall);
	phywrite(c, Phyctl, Phyrst | Anerst | Aneen);
	/* chip specific stuff? */
	if (c->type == Yukfep){
		u = phyread(c, Phyphy) | Ppnpe;
		u &= ~(Ppengy | Ppscrdis);
		phywrite(c, Phyphy, u);
//		phywrite(c, 0x16, 0x0b54);		/* write to fe_led_par */

		/* yukfep and rev 0: apply workaround for integrated resistor calibration */
		phywrite(c, Phypadr, 17);
		phywrite(c, 0x1e, 0x3f60);
	}
	phywrite(c, Phyintm, Anok | Anerr | Lsc);
	dprint("phyid %.4ux step %.4ux\n", phyread(c, 2), phyread(c, 3));
}

static int
identify(Ctlr *c)
{
	char t;

	pcicfgw32(c->p, Pciclk, 0);
	c->reg16[Ctst] = Swclr;

	c->type = c->reg8[Chip] - 0xb3;
	c->rev = c->reg8[Maccfg]>>4 & 0xf;
	if(c->type >= Nyuk)
		return -1;
	if(idtab[c->type].okrev != 0xff)
	if(c->rev != idtab[c->type].okrev)
		return -1;
	c->feat |= idtab[c->type].feat;

	t = c->reg8[Pmd];
	if(t == 'L' || t == 'S' || t == 'P')
		c->feat |= Ffiber;
	c->portno = 0;
	/* check second port ... whatever */
	return 0;
}

static uint
µ2clk(Ctlr *c, int µs)
{
	return idtab[c->type].mhz * µs;
}

static void
gmacsetea(Ctlr *c, uint r)
{
	uchar *ra;
	int i;

	ra = c->ra;
	for(i = 0; i < Eaddrlen; i += 2)
		gmacwrite(c, r + i, ra[i + 0] | ra[i + 1]<<8);
}

static int
reset(Ctlr *c)
{
	uint i, j;
	Block *b;

	identify(c);

	if(c->type == Yukex)
		c->reg16[Asfcs/2] &= ~(Asfbrrst | Asfcpurst | Asfucrst);
	else
		c->reg8[Asfcs] = Asfrst;
	c->reg16[Ctst] = Asfdis;

	c->reg16[Ctst] = Swrst;
	c->reg16[Ctst] = Swclr;

	c->reg8[Tstctl1] = Tstwen;
	pcicfgw16(c->p, PciPSR, pcicfgr16(c->p, PciPSR) | 0xf100);
	c->reg16[Ctst] = Mstrclr;
	/* fixup pcie extended error goes here */

	c->reg8[Pwrctl] = Vauxen | Vccen | Vauxoff | Vccon;
	c->reg[Clkctl] = Clkdivdis;
	if(c->type == Yukxl && c->rev > 1)
		c->reg8[Clkgate] = ~Link2inactive;
	else
		c->reg8[Clkgate] = 0;
	if(c->flag & Fapwr){
		pcicfgw32(c->p, Pciclk, 0);
		pcicfgw32(c->p, Pciasp, pcicfgr32(c->p, Pciasp) & Aspmsk);
		pcicfgw32(c->p, Pcistate, pcicfgr32(c->p, Pcistate) & Vmain);
		pcicfgw32(c->p, Pcicf1, 0);
		c->reg[Gpio] |= Norace;
		print("yuk2: advanced power %.8ux\n", c->reg[Gpio]);
	}
	c->reg8[Tstctl1] = Tstwdis;

	for(i = 0; i < c->nports; i++){
		macwrite8(c, Linkctl, Linkrst);
		macwrite8(c, Linkctl, Linkclr);
		if(c->type == Yukex || c->type == Yuksup)
			macwrite16(c, Mac, Nomacsec | Nortx);
	}

	c->reg[Dpolltm] = Pollstop;

	for(i = 0; i < c->nports; i++)
		macwrite8(c, Txactl, Txaclr);
	for(i = 0; i < c->nports; i++){
		c->reg8[i*64 + Rictl] = Riclr;
		for(j = 0; j < 12; j++)
			c->reg8[i*64 + Rib + j] = 36;	/* qword times */
	}

	c->reg[Hwem] = Hdflt;
	macwrite8(c, Irqm, 0);
	for(i = 0; i < 4; i++)
		gmacwrite(c, Mchash + 2*i, 0);
	gmacwrite(c, Rxctl, Ufilter | Mfilter | Rmcrc);

	for(i = 0; i < nelem(c->tbring); i++)
		if(b = c->tbring[i]){
			c->tbring[i] = nil;
			freeb(b);
		}
	for(i = 0; i < nelem(c->rbring); i++)
		if(b = c->rbring[i]){
			c->rbring[i] = nil;
			freeb(b);
		}

	memset(c->tbring, 0, sizeof c->tbring[0] * nelem(c->tbring));
	memset(c->rbring, 0, sizeof c->rbring[0] * nelem(c->rbring));
	memset(c->tx.r, 0, sizeof c->tx.r[0] * c->tx.cnt);
	memset(c->rx.r, 0, sizeof c->rx.r[0] * c->rx.cnt);
	memset(c->status.r, 0, sizeof c->status.r[0] * c->status.cnt);
	c->reg[Statctl] = Statrst;
	c->reg[Statctl] = Statclr;
	c->reg[Stataddr + 0] = Pciwaddrl(c->status.r);
	c->reg[Stataddr + 4] = Pciwaddrh(c->status.r);
	c->reg16[Stattl] = c->status.m;
	c->reg16[Statth] = 10;
	c->reg8[Statwm] = 16;
	if(c->type == Yukxl && c->rev == 0)
		c->reg8[Statiwm] = 4;
	else
		c->reg8[Statiwm] = 4; //16;

	/* set transmit, isr,  level timers */
	c->reg[Tsti] = µ2clk(c, 1000);
	c->reg[Titi] = µ2clk(c, 20);
	c->reg[Tlti] = µ2clk(c, 100);

	c->reg[Statctl] = Staton;

	c->reg8[Tstc] = Tstart;
	c->reg8[Tltc] = Tstart;
	c->reg8[Titc] = Tstart;

	return 0;
}

static void
macinit(Ctlr *c)
{
	uint r, i;

	r = macread32(c, Phy) & ~(Gphyrst | Gphyrstclr);
	macwrite32(c, Phy, r | Gphyrst);
	macwrite32(c, Phy, r | Gphyrstclr);
	/* macwrite32(c, Mac, Macrst); ? */
	macwrite32(c, Mac, Macrstclr);

	if(c->type == Yukxl && c->rev == 0 && c->portno == 1){
	}

	macread8(c, Irq);
	macwrite8(c, Irqm, Txurun);

	phypower(c);
	phyinit(c);

	gmacwrite(c, Phyaddr, (r = gmacread(c, Phyaddr)) | Mibclear);
	for(i = 0; i < nelem(stattab); i++)
		gmacread32(c, Stats + stattab[i].offset/4);
	gmacwrite(c, Phyaddr, r);

	gmacwrite(c, Txctl, 4<<10);	/* collision distance */
	gmacwrite(c, Txflow, 0xffff);	/* flow control */
	gmacwrite(c, Txparm, 3<<14 | 0xb<<9 | 0x1c<<4 | 4);
	gmacwrite(c, Rxctl, Ufilter | Mfilter | Rmcrc);
	gmacwrite(c, Serctl, 0x04<<11 /* blind */ | Jumboen | 0x1e /* ipig */);

	gmacsetea(c, Ea0);
	gmacsetea(c, Ea1);

	gmacwrite(c, Txmask, 0);
	gmacwrite(c, Rxmask, 0);
	gmacwrite(c, Trmask, 0);

	macwrite32(c, Gfrxctl, Gfrstclr);
	r = Gfon | Gffon;
	if(c->type == Yukex || c->type == Yukfep)
		r |= Gfroon;
	macwrite32(c, Gfrxctl, r);
	if(c->type == Yukxl)
		macwrite32(c, Grxfm, 0);
	else
		macwrite32(c, Grxfm, Ferror);
	if(c->type == Yukfep && c->rev == 0)
		macwrite32(c, Grxft, 0x178);
	else
		macwrite32(c, Grxft, 0xb);

	macwrite32(c, Gmfctl, Gmfclr);	/* clear reset */
	macwrite32(c, Gmfctl, Gmfon);	/* on */

	raminit(c);
	if(c->type == Yukfep && c->rev == 0)
		c->reg[Gmfea] = c->reg[Gmfea] & ~3;

	c->rxinit = 0;
	c->txinit = 0;
}

static void*
slice(void **v, uint r, uint sz)
{
	uintptr a;

	a = (uintptr)*v;
	a = ROUNDUP(a, r);
	*v = (void*)(a + sz);
	return (void*)a;
}

static void
setupr(Sring *r, uint cnt)
{
	r->rp = 0;
	r->wp = 0;
	r->cnt = cnt;
	r->m = cnt - 1;
}

static int
setup(Ctlr *c)
{
	uint n;
	void *v, *mem;
	Pcidev *p;

	p = c->p;
	c->io = p->mem[0].bar&~0xf;
	mem = vmap(c->io, p->mem[0].size);
	if(mem == nil){
		print("yuk: cant map %#p\n", c->io);
		return -1;
	}
	pcienable(p);
	c->p = p;
	c->reg = (uint*)mem;
	c->reg8 = (uchar*)mem;
	c->reg16 = (ushort*)mem;
	if(memcmp(c->ra, nilea, sizeof c->ra) == 0)
		memmove(c->ra, c->reg8 + Macadr + 8*c->portno, Eaddrlen);

	setupr(&c->status, Sringcnt);
	setupr(&c->tx, Tringcnt);
	setupr(&c->rx, Rringcnt);

	n = sizeof c->status.r[0] * (c->status.cnt + c->tx.cnt + c->rx.cnt);
	n += 16*4096*2;				/* rounding slop */
	c->alloc = xspanalloc(n, 16*4096, 0);	/* unknown alignment constraints */
	memset(c->alloc, 0, n);

	v = c->alloc;
	c->status.r = slice(&v, 16*4096, sizeof c->status.r[0] * c->status.cnt);
	c->tx.r = slice(&v, 16*4096, sizeof c->tx.r[0] * c->tx.cnt);
	c->rx.r = slice(&v, 16*4096, sizeof c->rx.r[0] * c->rx.cnt);

	c->nports = 1;				/* BOTCH */
	if(reset(c)){
		print("yuk: cant reset\n");
		free(c->alloc);
		vunmap(mem, p->mem[0].size);
		pcidisable(p);
		return -1;
	}
	macinit(c);
	pcisetbme(p);
	return 0;
}

static void
shutdown(Ether *e)
{
	Ctlr *c;
	Pcidev *p;

	c = e->ctlr;

	reset(c);
	if(0){
		p = c->p;
		vunmap(c->reg, p->mem[0].size);
		free(c->alloc);
	}
}

static void
scan(void)
{
	int i;
	Pcidev *p;
	Ctlr *c;

	for(p = nil; p = pcimatch(p, 0, 0); ){
		for(i = 0; i < nelem(vtab); i++)
			if(vtab[i].vid == p->vid)
			if(vtab[i].did == p->did)
				break;
		if(i == nelem(vtab))
			continue;
		if(nctlr == nelem(ctlrtab)){
			print("yuk: too many controllers\n");
			return;
		}
		c = malloc(sizeof *c);
		if(c == nil){
			print("yuk: no memory for Ctlr\n");
			return;
		}
		c->p = p;
		c->qno = nctlr;
		c->rbsz = vtab[i].mtu;
		ctlrtab[nctlr++] = c;
	}
}

static int
pnp(Ether *e)
{
	int i;
	Ctlr *c;

	if(nctlr == 0)
		scan();
	for(i = 0;; i++){
		if(i == nctlr)
			return -1;
		c = ctlrtab[i];
		if(c == nil || c->flag&Fprobe)
			continue;
		if(e->port != 0 && e->port != (ulong)(uintptr)c->reg)
			continue;
		c->flag |= Fprobe;
		if(setup(c) != 0)
			continue;
		break;
	}
	e->ctlr = c;
	e->port = c->io;
	e->irq = c->p->intl;
	e->tbdf = c->p->tbdf;
	e->mbps = 1000;
	e->maxmtu = c->rbsz;
	memmove(e->ea, c->ra, Eaddrlen);
	e->arg = e;
	e->attach = attach;
	e->ctl = ctl;
	e->ifstat = ifstat;
	e->multicast = multicast;
	e->promiscuous = promiscuous;
	e->shutdown = shutdown;
	e->transmit = nil;

	intrenable(e->irq, interrupt, e, e->tbdf, e->name);

	return 0;
}

void
etheryuklink(void)
{
	addethercard("yuk", pnp);
}
