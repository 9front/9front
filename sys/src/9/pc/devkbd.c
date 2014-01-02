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
	Data=		0x60,		/* data port */

	Status=		0x64,		/* status port */
	 Inready=	0x01,		/*  input character ready */
	 Outbusy=	0x02,		/*  output busy */
	 Sysflag=	0x04,		/*  system flag */
	 Cmddata=	0x08,		/*  cmd==0, data==1 */
	 Inhibit=	0x10,		/*  keyboard/mouse inhibited */
	 Minready=	0x20,		/*  mouse character ready */
	 Rtimeout=	0x40,		/*  general timeout */
	 Parity=	0x80,

	Cmd=		0x64,		/* command port (write only) */

	Spec=		0xF800,		/* Unicode private space */
	PF=		Spec|0x20,	/* num pad function key */
	View=		Spec|0x00,	/* view (shift window up) */
	KF=		0xF000,		/* function key (begin Unicode private space) */
	Shift=		Spec|0x60,
	Break=		Spec|0x61,
	Ctrl=		Spec|0x62,
	Latin=		Spec|0x63,
	Caps=		Spec|0x64,
	Num=		Spec|0x65,
	Middle=		Spec|0x66,
	Altgr=		Spec|0x67,
	Kmouse=		Spec|0x100,
	No=		0x00,		/* peter */

	Home=		KF|13,
	Up=		KF|14,
	Pgup=		KF|15,
	Print=		KF|16,
	Left=		KF|17,
	Right=		KF|18,
	End=		KF|24,
	Down=		View,
	Pgdown=		KF|19,
	Ins=		KF|20,
	Del=		0x7F,
	Scroll=		KF|21,

	Nscan=	128,

	Int=	0,			/* kbscans indices */
	Ext,
	Nscans,
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
static uchar ccc;
static void kbdputc(int);
static void (*auxputc)(int, int);
static int nokbd = 1;			/* flag: no PS/2 keyboard */

static struct {
	Ref ref;
	Queue *q;
} kbd;

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
 *  ask 8042 to reset the machine
 */
void
i8042reset(void)
{
	int i, x;

	if(nokbd)
		return;

	*((ushort*)KADDR(0x472)) = 0x1234;	/* BIOS warm-boot flag */

	/*
	 *  newer reset the machine command
	 */
	outready();
	outb(Cmd, 0xFE);
	outready();

	/*
	 *  Pulse it by hand (old somewhat reliable)
	 */
	x = 0xDF;
	for(i = 0; i < 5; i++){
		x ^= 1;
		outready();
		outb(Cmd, 0xD1);
		outready();
		outb(Data, x);	/* toggle reset */
		delay(100);
	}
}

int
i8042auxcmd(int cmd)
{
	unsigned int c;
	int tries;

	c = 0;
	tries = 0;

	ilock(&i8042lock);
	do{
		if(tries++ > 2)
			break;
		if(outready() < 0)
			break;
		outb(Cmd, 0xD4);
		if(outready() < 0)
			break;
		outb(Data, cmd);
		if(outready() < 0)
			break;
		if(inready() < 0)
			break;
		c = inb(Data);
	} while(c == 0xFE || c == 0);
	iunlock(&i8042lock);

	if(c != 0xFA){
		print("i8042: %2.2ux returned to the %2.2ux command\n", c, cmd);
		return -1;
	}
	return 0;
}

int
i8042auxcmds(uchar *cmd, int ncmd)
{
	int i;

	ilock(&i8042lock);
	for(i=0; i<ncmd; i++){
		if(outready() < 0)
			break;
		outb(Cmd, 0xD4);
		if(outready() < 0)
			break;
		outb(Data, cmd[i]);
	}
	iunlock(&i8042lock);
	return i;
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

	if(nokbd || leds == old)
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

	/*
	 *  if it's the aux port...
	 */
	if(s & Minready){
		if(auxputc != nil)
			auxputc(c, 0);
		return;
	}

	b = c & 0xff;
	qproduce(kbd.q, &b, 1);
}

void
i8042auxenable(void (*putc)(int, int))
{
	char *err = "i8042: aux init failed\n";

	/* enable kbd/aux xfers and interrupts */
	ccc &= ~Cauxdis;
	ccc |= Cauxint;

	ilock(&i8042lock);
	if(outready() < 0)
		print(err);
	outb(Cmd, 0x60);			/* write control register */
	if(outready() < 0)
		print(err);
	outb(Data, ccc);
	if(outready() < 0)
		print(err);
	outb(Cmd, 0xA8);			/* auxiliary device enable */
	if(outready() < 0){
		iunlock(&i8042lock);
		return;
	}
	auxputc = putc;
	intrenable(IrqAUX, i8042intr, 0, BUSUNKNOWN, "kbdaux");
	iunlock(&i8042lock);
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
	return 0;
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

Dev kbddevtab = {
	L'b',
	"kbd",

	devreset,
	devinit,
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

void
kbdenable(void)
{
	kbd.q = qopen(1024, Qcoalesce, 0, 0);
	if(kbd.q == nil)
		panic("kbdenable");
	qnoblock(kbd.q, 1);

	ioalloc(Data, 1, 0, "kbd");
	ioalloc(Cmd, 1, 0, "kbd");

	intrenable(IrqKBD, i8042intr, 0, BUSUNKNOWN, "kbd");
}

void
kbdinit(void)
{
	int c, try;

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

	nokbd = 0;

	/* disable mouse */
	if (outbyte(Cmd, 0x60) < 0 || outbyte(Data, ccc) < 0)
		print("i8042: kbdinit mouse disable failed\n");
}
