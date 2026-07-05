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
static Timer *polltimer;
static int ccc;
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
	int tries, status;

	for(tries = 0; ((status = inb(Status)) & Outbusy); tries++){
		if(tries > 500)
			return -1;
		delay(2);
	}
	return status;
}

/*
 *  wait for input
 */
static int
inready(void)
{
	int tries, status;

	for(tries = 0; !((status = inb(Status)) & Inready); tries++){
		if(tries > 500)
			return -1;
		delay(2);
	}
	return status;
}

static int
ackdata(int status)
{
	if(status < 0 || (status & Inready) == 0)
		return -1;

	return inb(Data);
}

static int
indata(void)
{
	return ackdata(inready());
}

/*
 *  wait for a quiescent controller
 */
static int
flushdata(void)
{
	int tries, status;

	for(tries = 0; (status = inb(Status)) & (Outbusy | Inready); tries++){
		ackdata(status);
		if(tries > 1000)
			return -1;
		delay(1);
	}
	return 0;
}

static int
outdata(int data)
{
	if(outready() < 0)
		return -1;
	outb(Data, data);
	return 0;
}

static int
outcmd(int cmd, int data)
{
	if(outready() < 0)
		return -1;
	outb(Cmd, cmd);
	if(data >= 0 && outdata(data) < 0)
		return -1;
	if(outready() < 0)
		return -1;
	return 0;
}

static int
readccc(void)
{
	if(outcmd(0x20, -1) < 0)
		return -1;
	return indata();
}

static int
writeccc(int data)
{
	return outcmd(0x60, data);
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
	ilock(&i8042lock);
	outcmd(0xFE, -1);
	iunlock(&i8042lock);

	/*
	 *  Pulse it by hand (old somewhat reliable)
	 */
	x = 0xDF;
	for(i = 0; i < 5; i++){
		ilock(&i8042lock);
		x ^= 1;
		outcmd(0xD1, x);
		iunlock(&i8042lock);

		delay(100);
	}
}

int
i8042auxcmd(int cmd)
{
	int c, tries;

	c = 0;
	tries = 0;
	cmd &= 0xFF;

	if(nokbd)
		return -1;

	ilock(&i8042lock);
	do{
		if(tries++ > 2)
			break;
		if(outcmd(0xD4, cmd) < 0)
			break;
		if((c = indata()) < 0)
			break;
	} while(c == 0xFE || c == 0);
	iunlock(&i8042lock);

	if(c != 0xFA){
		print("i8042: %2.2x returned to the %2.2x command (pc=%#p)\n",
			c, cmd, getcallerpc(&cmd));
		return -1;
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

	if(nokbd || leds == old)
		return;

	leds &= 7;
	ilock(&i8042lock);
	for(;;){
		if(outdata(0xED) < 0)	/* `reset keyboard lock states' */
			break;
		if(outdata(leds) < 0)
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
	int tries, status;
	uchar c;

	for(tries=0; tries<16; tries++){
		ilock(&i8042lock);
		status = inb(Status);
		if(!(status&Inready)){
			iunlock(&i8042lock);
			break;
		}
		c = ackdata(status);
		iunlock(&i8042lock);

		/*
		 *  if it's the aux port...
		 */
		if(status & Minready){
			if(auxputc != nil)
				auxputc(c, 0);
		} else {
			qproduce(kbd.q, &c, 1);
		}
	}
}

void
i8042auxenable(void (*putc)(int, int))
{
	if(nokbd)
		error("no PS2 controller");

	ilock(&i8042lock);

	/* already enabled? */
	if((ccc & (Cauxdis|Cauxint)) == Cauxint){
		auxputc = putc;
		iunlock(&i8042lock);
		return;
	}

	/* enable kbd/aux xfers and interrupts */
	ccc &= ~Cauxdis;
	ccc |= Cauxint;
	if(writeccc(ccc) < 0){
		iunlock(&i8042lock);
		print("i8042: aux init failed: writing ccc\n");
		return;
	}
	if(outcmd(0xA8, -1) < 0){	/* auxiliary device enable */
		iunlock(&i8042lock);
		print("i8042: aux init failed: aux device enable\n");
		return;
	}
	auxputc = putc;
	iunlock(&i8042lock);

	if(polltimer != nil)
		return;

	intrenable(IrqAUX, i8042intr, 0, BUSUNKNOWN, "kbdaux");
}

static void
kbdpoll(void)
{
	if(nokbd || qlen(kbd.q) > 0)
		return;
	i8042intr(0, 0);
}

static void
kbdshutdown(void)
{
	if(nokbd)
		return;

	ilock(&i8042lock);

	/* disable kbd and aux xfers and interrupts */
	ccc &= ~(Ckbdint|Cauxint);
	ccc |= (Cauxdis|Ckbddis);
	writeccc(ccc);

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
	if(c->qid.path == Qscancode){
		kbdpoll();
		return qbread(kbd.q, n);
	}
	return devbread(c, n, off);
}

static long
kbdread(Chan *c, void *a, long n, vlong)
{
	if(c->qid.path == Qscancode){
		kbdpoll();
		return qread(kbd.q, a, n);
	}
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

static void
kbdreset(void)
{
	kbd.q = qopen(1024, Qcoalesce, 0, 0);
	if(kbd.q == nil)
		panic("kbdreset");
	qnoblock(kbd.q, 1);

	if(getconf("*nokbd"))
		return;

	ilock(&i8042lock);

	/* wait for a quiescent controller */
	if(flushdata() < 0){
		iunlock(&i8042lock);
		print("i8042: kbd init failed: flushdata timeout\n");
		return;
	}

	ccc = readccc();
	if(ccc < 0){
		print("i8042: could not read ccc\n");
		ccc = 0;
	}

	/* enable kbd xfers and interrupts */
	ccc &= ~Ckbddis;
	ccc |= Csf | Ckbdint | Cscs1;

	/* disable ps2 mouse */
	ccc &= ~Cauxint;
	ccc |= Cauxdis;

	if(writeccc(ccc) < 0){
		iunlock(&i8042lock);
		print("i8042: kbd init failed: writing ccc\n");
		return;
	}

	nokbd = 0;
	ioalloc(Cmd, 1, 0, "i8042.cs");
	ioalloc(Data, 1, 0, "i8042.data");
	iunlock(&i8042lock);

	if(getconf("*kbdpoll") != nil){
		polltimer = addclock0link(kbdpoll, 50);
		return;
	}
	intrenable(IrqKBD, i8042intr, 0, BUSUNKNOWN, "kbd");
}

Dev kbddevtab = {
	L'b',
	"kbd",

	kbdreset,
	devinit,
	kbdshutdown,
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

