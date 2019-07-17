/* override default macros from ../port/usb.h */
#undef	dprint
#undef	ddprint
#undef	deprint
#undef	ddeprint
#define dprint		if(ehcidebug)print
#define ddprint		if(ehcidebug>1)print
#define deprint		if(ehcidebug || ep->debug)print
#define ddeprint	if(ehcidebug>1 || ep->debug>1)print

enum {
	/* typed links  */
	Lterm		= 1,
	Litd		= 0<<1,
	Lqh		= 1<<1,
	Lsitd		= 2<<1,
	Lfstn		= 3<<1,		/* we don't use these */

	/* Cmd reg. */
	Cstop		= 0x00000,	/* stop running */
	Crun		= 0x00001,	/* start operation */
	Chcreset	= 0x00002,	/* host controller reset */
	Cflsmask	= 0x0000C,	/* frame list size bits */
	Cfls1024	= 0x00000,	/* frame list size 1024 */
	Cfls512		= 0x00004,	/* frame list size 512 frames */
	Cfls256		= 0x00008,	/* frame list size 256 frames */
	Cpse		= 0x00010,	/* periodic sched. enable */
	Case		= 0x00020,	/* async sched. enable */
	Ciasync		= 0x00040,	/* interrupt on async advance doorbell */
	Citc1		= 0x10000,	/* interrupt threshold ctl. 1 µframe */
	Citc4		= 0x40000,	/* same. 2 µframes */
	/* ... */
	Citc8		= 0x80000,	/* same. 8 µframes (can go up to 64) */

	/* Sts reg. */
	Sasyncss	= 0x08000,	/* aync schedule status */
	Speriodss	= 0x04000,	/* periodic schedule status */
	Srecl		= 0x02000,	/* reclamnation (empty async sched.) */
	Shalted		= 0x01000,	/* h.c. is halted */
	Sasync		= 0x00020,	/* interrupt on async advance */
	Sherr		= 0x00010,	/* host system error */
	Sfrroll		= 0x00008,	/* frame list roll over */
	Sportchg	= 0x00004,	/* port change detect */
	Serrintr	= 0x00002,		/* error interrupt */
	Sintr		= 0x00001,	/* interrupt */
	Sintrs		= 0x0003F,	/* interrupts status */

	/* Portsc reg. */
	Pspresent	= 0x00000001,	/* device present */
	Psstatuschg	= 0x00000002,	/* Pspresent changed */
	Psenable	= 0x00000004,	/* device enabled */
	Pschange	= 0x00000008,	/* Psenable changed */
	Psresume	= 0x00000040,	/* resume detected */
	Pssuspend	= 0x00000080,	/* port suspended */
	Psreset		= 0x00000100,	/* port reset */
	Pspower		= 0x00001000,	/* port power on */

	/* Intr reg. */
	Iusb		= 0x01,		/* intr. on usb */
	Ierr		= 0x02,		/* intr. on usb error */
	Iportchg	= 0x04,		/* intr. on port change */
	Ifrroll		= 0x08,		/* intr. on frlist roll over */
	Ihcerr		= 0x10,		/* intr. on host error */
	Iasync		= 0x20,		/* intr. on async advance enable */
	Iall		= 0x3F,		/* all interrupts */
	
	Callmine	= 1,
	
	/* hack to disable port handoff */
	Psowner = 0,
	Pslinemask = 0,
	Pslow = -1
};

typedef struct Ctlr Ctlr;
typedef void Ecapio;
typedef struct Eopio Eopio;
typedef struct Isoio Isoio;
typedef struct Poll Poll;
typedef struct Qh Qh;
typedef struct Qtree Qtree;

#pragma incomplete Ctlr

struct Eopio
{
/*140*/	ulong cmd;
/*144*/	ulong sts;
/*148*/	ulong intr;
/*14c*/	ulong frno;

/*150*/	ulong reserved1;

/*154*/	ulong frbase;
/*158*/	ulong link;

/*15c*/	ulong _ttctrl;
/*160*/	ulong _burstsize;
/*164*/	ulong _txfilltuning;
/*168*/ ulong _txttfilltuning;
/*16c*/	ulong _ic_usb;
/*170*/	ulong _ulpi_viewport;

/*174*/	ulong reserved2;

/*178*/	ulong _endptnak;
/*17c*/	ulong _endptnaken;

/*180*/	ulong config;
/*184*/	ulong portsc[1];
};

struct Poll
{
	Lock;
	Rendez;
	int must;
	int does;
};

struct Ctlr
{
	Rendez;			/* for waiting to async advance doorbell */
	Lock;			/* for ilock. qh lists and basic ctlr I/O */
	QLock	portlck;	/* for port resets/enable... (and doorbell) */
	int	active;		/* in use or not */
	void*	capio;		/* base address for debug info */
	Eopio*	opio;		/* Operational i/o regs */

	void*	(*tdalloc)(ulong,int,ulong);
	void*	(*dmaalloc)(ulong);
	void	(*dmafree)(void*);

	int	nframes;	/* 1024, 512, or 256 frames in the list */
	ulong*	frames;		/* periodic frame list (hw) */
	Qh*	qhs;		/* async Qh circular list for bulk/ctl */
	Qtree*	tree;		/* tree of Qhs for the periodic list */
	int	ntree;		/* number of dummy qhs in tree */
	Qh*	intrqhs;		/* list of (not dummy) qhs in tree  */
	Isoio*	iso;		/* list of active Iso I/O */
	ulong	load;
	ulong	isoload;
	int	nintr;		/* number of interrupts attended */
	int	ntdintr;	/* number of intrs. with something to do */
	int	nqhintr;	/* number of async td intrs. */
	int	nisointr;	/* number of periodic td intrs. */
	int	nreqs;
	Poll	poll;
	
	ulong	base;
	int	irq;
	ulong*	r;
};

extern int ehcidebug;

void	ehcilinkage(Hci *hp);
void	ehcimeminit(Ctlr *ctlr);
void	ehcirun(Ctlr *ctlr, int on);
