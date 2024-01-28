/*
 * keyboard input
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

enum {
	Data=		0x40+3,		/* data port */
	Cmd=		0x44+3,		/* command port (write) */
	Status=		0x44+3,		/* status port (read) */
	 Inready=	0x01,		/*  input character ready */
	 Outbusy=	0x02,		/*  output busy */
	 Sysflag=	0x04,		/*  system flag */
	 Cmddata=	0x08,		/*  cmd==0, data==1 */
	 Inhibit=	0x10,		/*  keyboard/mouse inhibited */
	 Minready=	0x20,		/*  mouse character ready */
	 Rtimeout=	0x40,		/*  general timeout */
	 Parity=	0x80,
};

enum
{
	/* controller command byte */
	Cscs1=		(1<<6),		/* scan code set 1 */
	Cauxdis=	(1<<5),		/* mouse disable */
	Ckbddis=	(1<<4),		/* kbd disable */
	Csf=		(1<<2),		/* system flag */
	Cauxint=	(1<<1),		/* mouse interrupt enable */
	Ckbdint=	(1<<0),		/* kbd interrupt enable */
};

enum {
	Qdir,
	Qscancode,
	Qleds,
};

static Dirtab kbdtab[] = {
	".",		{Qdir, 0, QTDIR},	0,	0555,
	"scancode",	{Qscancode, 0},		0,	0440,
	"leds",		{Qleds, 0},		0,	0220,
};

static Lock i8042lock;
static uchar ccc, dummy;

static struct {
	Ref ref;
	Queue *q;
	uchar *io;
} kbd;


#define	inb(r)		(dummy=kbd.io[r])
#define	outb(r,b)	(kbd.io[r]=b)

/*
 *  wait for output no longer busy
 */
static int
outready(void)
{
	int tries;

	for(tries = 0; (inb(Status) & Outbusy); tries++){
		if(tries > 500)
			return -1;
		delay(2);
	}
	return 0;
}

/*
 *  wait for input
 */
static int
inready(void)
{
	int tries;

	for(tries = 0; !(inb(Status) & Inready); tries++){
		if(tries > 500)
			return -1;
		delay(2);
	}
	return 0;
}

/*
 * set keyboard's leds for lock states (scroll, numeric, caps).
 *
 * at least one keyboard (from Qtronics) also sets its numeric-lock
 * behaviour to match the led state, though it has no numeric keypad,
 * and some BIOSes bring the system up with numeric-lock set and no
 * setting to change that.  this combination steals the keys for these
 * characters and makes it impossible to generate them: uiolkjm&*().
 * thus we'd like to be able to force the numeric-lock led (and behaviour) off.
 */
static void
setleds(int leds)
{
	static int old = -1;

	if(!conf.keyboard || leds == old)
		return;
	leds &= 7;
	ilock(&i8042lock);
	for(;;){
		if(outready() < 0)
			break;
		outb(Data, 0xed);		/* `reset keyboard lock states' */
		if(outready() < 0)
			break;
		outb(Data, leds);
		if(outready() < 0)
			break;
		old = leds;
		break;
	}
	iunlock(&i8042lock);
}

/*
 *  keyboard interrupt
 */
static void
i8042intr(Ureg*, void*)
{
	extern void sgimouseputc(int);

	int s, c;
	uchar b;

	/*
	 *  get status
	 */
	ilock(&i8042lock);
	s = inb(Status);
	if(!(s&Inready)){
		iunlock(&i8042lock);
		return;
	}

	/*
	 *  get the character
	 */
	c = inb(Data);
	iunlock(&i8042lock);

	b = c & 0xff;

	/*
	 *  if it's the aux port...
	 */
	if(s & Minready){
		sgimouseputc(b);
		return;
	}

	qproduce(kbd.q, &b, 1);
}

static void
pollintr(void)
{
	i8042intr(nil, nil);
}

static Chan *
kbdattach(char *spec)
{
	return devattach(L'b', spec);
}

static Walkqid*
kbdwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, kbdtab, nelem(kbdtab), devgen);
}

static int
kbdstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, kbdtab, nelem(kbdtab), devgen);
}

static Chan*
kbdopen(Chan *c, int omode)
{
	if(!iseve())
		error(Eperm);
	if(c->qid.path == Qscancode){
		if(waserror()){
			decref(&kbd.ref);
			nexterror();
		}
		if(incref(&kbd.ref) != 1)
			error(Einuse);
		c = devopen(c, omode, kbdtab, nelem(kbdtab), devgen);
		poperror();
		return c;
	}
	return devopen(c, omode, kbdtab, nelem(kbdtab), devgen);
}

static void
kbdclose(Chan *c)
{
	if((c->flag & COPEN) && c->qid.path == Qscancode)
		decref(&kbd.ref);
}

static Block*
kbdbread(Chan *c, long n, ulong off)
{
	if(c->qid.path == Qscancode)
		return qbread(kbd.q, n);
	else
		return devbread(c, n, off);
}

static long
kbdread(Chan *c, void *a, long n, vlong)
{
	if(c->qid.path == Qscancode)
		return qread(kbd.q, a, n);
	if(c->qid.path == Qdir)
		return devdirread(c, a, n, kbdtab, nelem(kbdtab), devgen);

	error(Egreg);
}

static long
kbdwrite(Chan *c, void *a, long n, vlong)
{
	char tmp[8+1], *p;

	if(c->qid.path != Qleds)
		error(Egreg);

	p = tmp + n;
	if(n >= sizeof(tmp))
		p = tmp + sizeof(tmp)-1;
	memmove(tmp, a, p - tmp);
	*p = 0;

	setleds(atoi(tmp));

	return n;
}


static char *initfailed = "i8042: kbdinit failed\n";

static int
outbyte(int port, int c)
{
	outb(port, c);
	if(outready() < 0) {
		print(initfailed);
		return -1;
	}
	return 0;
}

static void
kbdinit(void)
{
	int c, try;

	kbd.io = IO(uchar, HPC3_KBDMS);
	kbd.q = qopen(1024, Qcoalesce, 0, 0);
	if(kbd.q == nil)
		panic("kbdinit: qopen");
	qnoblock(kbd.q, 1);

	/* wait for a quiescent controller */
	try = 1000;
	while(try-- > 0 && (c = inb(Status)) & (Outbusy | Inready)) {
		if(c & Inready)
			inb(Data);
		delay(1);
	}
	if (try <= 0) {
		print(initfailed);
		return;
	}

	/* get current controller command byte */
	outb(Cmd, 0x20);
	if(inready() < 0){
		print("i8042: kbdinit can't read ccc\n");
		ccc = 0;
	} else
		ccc = inb(Data);

	/* enable kbd xfers and interrupts */
	ccc &= ~Ckbddis;
	ccc |= Csf | Ckbdint | Cscs1;
	if(outready() < 0) {
		print(initfailed);
		return;
	}
	/* disable mouse */
	if (outbyte(Cmd, 0x60) < 0 || outbyte(Data, ccc) < 0){
		print("i8042: kbdinit mouse disable failed\n");
		return;
	}

	conf.keyboard = 1;
	addclock0link(pollintr, 5);
}

Dev kbddevtab = {
	L'b',
	"kbd",

	devreset,
	kbdinit,
	devshutdown,
	kbdattach,
	kbdwalk,
	kbdstat,
	kbdopen,
	devcreate,
	kbdclose,
	kbdread,
	kbdbread,
	kbdwrite,
	devbwrite,
	devremove,
	devwstat,
};
