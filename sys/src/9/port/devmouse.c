#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#define	Image	IMAGE
#include	<draw.h>
#include	<memdraw.h>
#include	<cursor.h>
#include	"screen.h"

enum {
	ScrollUp = 0x08,
	ScrollDown = 0x10,
	ScrollLeft = 0x20,
	ScrollRight = 0x40,
};

typedef struct Mouseinfo	Mouseinfo;
typedef struct Mousestate	Mousestate;

struct Mousestate
{
	Point	xy;		/* mouse.xy */
	int	buttons;	/* mouse.buttons */
	ulong	counter;	/* increments every update */
	ulong	msec;		/* time of last event */
};

struct Mouseinfo
{
	Lock;
	Mousestate;
	int	inbuttons;	/* buttons from /dev/mousein */
	int	redraw;		/* update cursor on screen */
	Rendez	redrawr;	/* wait for cursor screen updates */
	ulong	lastcounter;	/* value when /dev/mouse read */
	int	resize;		/* generate resize event */
	Rendez	r;
	Ref;
	int	open;
	int	acceleration;
	int	maxacc;
	Mousestate	queue[16];	/* circular buffer of click events */
	ulong	ri;		/* read index into queue */
	ulong	wi;		/* write index into queue */
};

enum
{
	CMbuttonmap,
	CMscrollswap,
	CMswap,
	CMblank,
	CMblanktime,
	CMtwitch,
	CMwildcard,
};

static Cmdtab mousectlmsg[] =
{
	CMbuttonmap,	"buttonmap",	0,
	CMscrollswap,	"scrollswap",	0,
	CMswap,		"swap",		1,
	CMblank,	"blank",	1,
	CMblanktime,	"blanktime",	2,
	CMtwitch,	"twitch",	1,
	CMwildcard,	"*",		0,
};

Mouseinfo	mouse;

void	mouseblankscreen(int);
int	mousechanged(void*);
void	mouseredraw(void);

enum{
	Qdir,
	Qcursor,
	Qmouse,
	Qmousein,
	Qmousectl,
};

static Dirtab mousedir[]={
	".",	{Qdir, 0, QTDIR},	0,			DMDIR|0555,
	"cursor",	{Qcursor},	0,			0666,
	"mouse",	{Qmouse},	0,			0666,
	"mousein",	{Qmousein},	0,			0660,
	"mousectl",	{Qmousectl},	0,			0220,
};

static uchar buttonmap[8] = {
	0, 1, 2, 3, 4, 5, 6, 7,
};
static int mouseswap;
static int scrollswap;
static ulong mousetime;
static ulong blanktime;

extern Memimage* gscreen;

Cursor arrow = {
	{ -1, -1 },
	{ 0xFF, 0xFF, 0x80, 0x01, 0x80, 0x02, 0x80, 0x0C,
	  0x80, 0x10, 0x80, 0x10, 0x80, 0x08, 0x80, 0x04,
	  0x80, 0x02, 0x80, 0x01, 0x80, 0x02, 0x8C, 0x04,
	  0x92, 0x08, 0x91, 0x10, 0xA0, 0xA0, 0xC0, 0x40,
	},
	{ 0x00, 0x00, 0x7F, 0xFE, 0x7F, 0xFC, 0x7F, 0xF0,
	  0x7F, 0xE0, 0x7F, 0xE0, 0x7F, 0xF0, 0x7F, 0xF8,
	  0x7F, 0xFC, 0x7F, 0xFE, 0x7F, 0xFC, 0x73, 0xF8,
	  0x61, 0xF0, 0x60, 0xE0, 0x40, 0x40, 0x00, 0x00,
	},
};
Cursor cursor;

static void Cursortocursor(Cursor*);

static int
mousedevgen(Chan *c, char *name, Dirtab *tab, int ntab, int i, Dir *dp)
{
	int rc;

	rc = devgen(c, name, tab, ntab, i, dp);
	if(rc != -1)
		dp->atime = mousetime;
	return rc;
}

static void mouseproc(void*);

static void
mouseinit(void)
{
	if(!conf.monitor)
		return;
	mousetime = seconds();
	Cursortocursor(&arrow);
	kproc("mouse", mouseproc, 0);
}

static Chan*
mouseattach(char *spec)
{
	if(!conf.monitor)
		error(Egreg);
	return devattach('m', spec);
}

static Walkqid*
mousewalk(Chan *c, Chan *nc, char **name, int nname)
{
	/*
	 * We use devgen() and not mousedevgen() here
	 * see "Ugly problem" in dev.c/devwalk()
	 */
	return devwalk(c, nc, name, nname, mousedir, nelem(mousedir), devgen);
}

static int
mousestat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, mousedir, nelem(mousedir), mousedevgen);
}

static Chan*
mouseopen(Chan *c, int omode)
{
	int mode;

	mode = openmode(omode);
	switch((ulong)c->qid.path){
	case Qdir:
		if(omode != OREAD)
			error(Eperm);
		break;
	case Qmousein:
		if(!iseve())
			error(Eperm);
		c->aux = malloc(sizeof(Mousestate));
		if(c->aux == nil)
			error(Enomem);
		break;
	case Qmouse:
		if(tas(&mouse.open) != 0)
			error(Einuse);
		mouse.lastcounter = mouse.counter;
		mouse.resize = 0;
		mousetime = seconds();
		/* wet floor */
	case Qcursor:
		incref(&mouse);
	}
	c->mode = mode;
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
mouseclose(Chan *c)
{
	if((c->qid.type&QTDIR)!=0 || (c->flag&COPEN)==0)
		return;
	switch((ulong)c->qid.path){
	case Qmousein:
		ilock(&mouse);
		mouse.inbuttons &= ~((Mousestate*)c->aux)->buttons;
		iunlock(&mouse);
		free(c->aux);	/* Mousestate */
		c->aux = nil;
		return;
	case Qmouse:
		mouse.open = 0;
		mouseblankscreen(0);
		/* wet floor */
	case Qcursor:
		if(decref(&mouse) != 0)
			return;
		Cursortocursor(&arrow);
	}
}


static long
mouseread(Chan *c, void *va, long n, vlong off)
{
	char buf[1+4*12+1];
	uchar *p;
	ulong offset = off;
	Cursor curs;
	Mousestate m;
	int b;
	char t;

	p = va;
	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, va, n, mousedir, nelem(mousedir), mousedevgen);

	case Qcursor:
		if(offset != 0)
			return 0;
		if(n < 2*4+2*2*16)
			error(Eshort);

		qlock(&drawlock);
		memmove(&curs, &cursor, sizeof(Cursor));
		qunlock(&drawlock);

		n = 2*4+2*2*16;
		BPLONG(p+0, curs.offset.x);
		BPLONG(p+4, curs.offset.y);
		memmove(p+8, curs.clr, 2*16);
		memmove(p+40, curs.set, 2*16);
		return n;

	case Qmouse:
		while(!mousechanged(nil)){
			tsleep(&mouse.r, mousechanged, nil, 30*1000);
			if(blanktime && !mousechanged(nil) &&
			   (seconds() - mousetime) >= blanktime*60)
				mouseblankscreen(1);
		}
		mousetime = seconds();
		mouseblankscreen(0);

		ilock(&mouse);
		if(mouse.ri != mouse.wi)
			m = mouse.queue[mouse.ri++ % nelem(mouse.queue)];
		else
			m = mouse.Mousestate;
		iunlock(&mouse);

		if(0){
	case Qmousein:
			if(offset != 0)
				return 0;

			ilock(&mouse);
			m = mouse.Mousestate;
			iunlock(&mouse);

			t = 'm';
		} else {
			/* Qmouse */
			mouse.lastcounter = m.counter;
			if(mouse.resize){
				mouse.resize = 0;
				t = 'r';
			} else {
				t = 'm';
			}
		}

		b = buttonmap[m.buttons&7];
		/* put buttons 4 and 5 back in */
		b |= m.buttons & (3<<3);

		if (scrollswap)
			if (b == 8)
				b = 16;
			else if (b == 16)
				b = 8;

		snprint(buf, sizeof(buf), "%c%11d %11d %11d %11ld ",
			t, m.xy.x, m.xy.y, b, m.msec);
		if(n > 1+4*12)
			n = 1+4*12;
		memmove(va, buf, n);
		return n;
	}
	return 0;
}

static void
setbuttonmap(char* map)
{
	int i, x, one, two, three;

	one = two = three = 0;
	for(i = 0; i < 3; i++){
		if(map[i] == '1'){
			if(one)
				error(Ebadarg);
			one = 1<<i;
		}
		else if(map[i] == '2'){
			if(two)
				error(Ebadarg);
			two = 1<<i;
		}
		else if(map[i] == '3'){
			if(three)
				error(Ebadarg);
			three = 1<<i;
		}
		else
			error(Ebadarg);
	}
	if(map[i])
		error(Ebadarg);

	memset(buttonmap, 0, 8);
	for(i = 0; i < 8; i++){
		x = 0;
		if(i & 1)
			x |= one;
		if(i & 2)
			x |= two;
		if(i & 4)
			x |= three;
		buttonmap[x] = i;
	}
}

void scmousetrack(int, int, int, ulong);

static long
mousewrite(Chan *c, void *va, long n, vlong)
{
	char *p;
	Point pt;
	Cmdbuf *cb;
	Cmdtab *ct;
	Cursor curs;
	char buf[64];
	int b, z, msec;
	Mousestate *m;

	p = va;
	switch((ulong)c->qid.path){
	case Qdir:
		error(Eisdir);

	case Qcursor:
		if(n < 2*4+2*2*16){
			Cursortocursor(&arrow);
			return n;
		}
		n = 2*4+2*2*16;
		curs.offset.x = BGLONG(p+0);
		curs.offset.y = BGLONG(p+4);
		memmove(curs.clr, p+8, 2*16);
		memmove(curs.set, p+40, 2*16);
		Cursortocursor(&curs);
		return n;

	case Qmousectl:
		cb = parsecmd(va, n);
		if(waserror()){
			free(cb);
			nexterror();
		}

		ct = lookupcmd(cb, mousectlmsg, nelem(mousectlmsg));

		switch(ct->index){
		case CMswap:
			if(mouseswap)
				setbuttonmap("123");
			else
				setbuttonmap("321");
			mouseswap ^= 1;
			break;

		case CMscrollswap:
			scrollswap ^= 1;
			break;

		case CMbuttonmap:
			if(cb->nf == 1)
				setbuttonmap("123");
			else
				setbuttonmap(cb->f[1]);
			break;

		case CMblank:
			mouseblankscreen(1);
			break;

		case CMblanktime:
			blanktime = strtoul(cb->f[1], 0, 0);
			/* wet floor */
		case CMtwitch:
			mousetime = seconds();
			mouseblankscreen(0);
			break;

		case CMwildcard:
			mousectl(cb);
			break;
		}

		free(cb);
		poperror();
		return n;

	case Qmousein:
		if(n > sizeof buf-1)
			n = sizeof buf -1;
		memmove(buf, va, n);
		buf[n] = 0;

		pt.x = strtol(buf+1, &p, 0);
		if(*p == 0)
			error(Eshort);
		pt.y = strtol(p, &p, 0);
		if(*p == 0)
			error(Eshort);
		b = strtol(p, &p, 0);
		msec = (ulong)strtoll(p, 0, 0);
		if(msec == 0)
			msec = TK2MS(MACHP(0)->ticks);

		/* exclude wheel */
		z = b & (8|16);
		b ^= z;

		m = (Mousestate*)c->aux;
		m->xy = pt;
		m->msec = msec;
		b ^= m->buttons;
		m->buttons ^= b;

		ilock(&mouse);
		mouse.inbuttons = (m->buttons & b) | (mouse.inbuttons & ~b);
		b = mouse.buttons & ~b;
		iunlock(&mouse);

		/* include wheel */
		b &= ~(8|16);
		b ^= z;

		if(buf[0] == 'A')
			absmousetrack(pt.x, pt.y, b, msec);
		else if(buf[0] == 'a')
			scmousetrack(pt.x, pt.y, b, msec);
		else
			mousetrack(pt.x, pt.y, b, msec);
		return n;

	case Qmouse:
		if(n > sizeof buf-1)
			n = sizeof buf -1;
		memmove(buf, va, n);
		buf[n] = 0;

		pt.x = strtol(buf+1, &p, 0);
		if(*p == 0)
			error(Eshort);
		pt.y = strtol(p, 0, 0);
		absmousetrack(pt.x, pt.y, mouse.buttons, TK2MS(MACHP(0)->ticks));
		return n;
	}

	error(Egreg);
}

Dev mousedevtab = {
	'm',
	"mouse",

	devreset,
	mouseinit,
	devshutdown,
	mouseattach,
	mousewalk,
	mousestat,
	mouseopen,
	devcreate,
	mouseclose,
	mouseread,
	devbread,
	mousewrite,
	devbwrite,
	devremove,
	devwstat,
};

static void
Cursortocursor(Cursor *c)
{
	qlock(&drawlock);
	cursoroff();
	memmove(&cursor, c, sizeof(Cursor));
	setcursor(c);
	cursoron();
	qunlock(&drawlock);
}

void
mouseblankscreen(int blank)
{
	static int blanked;

	if(blank == blanked)
		return;
	qlock(&drawlock);
	if(blanked != blank){
		blankscreen(blank);
		blanked = blank;
	}
	qunlock(&drawlock);
}

static int
shouldredraw(void*)
{
	return mouse.redraw != 0;
}

/*
 * process that redraws the cursor
 */
static void
mouseproc(void*)
{
	while(waserror())
		;
	for(;;){
		sleep(&mouse.redrawr, shouldredraw, nil);
		mouse.redraw = 0;

		qlock(&drawlock);
		cursoroff();
		cursoron();
		qunlock(&drawlock);
	}
}

static int
scale(int x)
{
	int sign = 1;

	if(x < 0){
		sign = -1;
		x = -x;
	}
	switch(x){
	case 0:
	case 1:
	case 2:
	case 3:
		break;
	case 4:
		x = 6 + (mouse.acceleration>>2);
		break;
	case 5:
		x = 9 + (mouse.acceleration>>1);
		break;
	default:
		x *= mouse.maxacc;
		break;
	}
	return sign*x;
}

/*
 *  called at interrupt level to update the structure and
 *  awaken any waiting procs.
 */
void
mousetrack(int dx, int dy, int b, ulong msec)
{
	if(mouse.acceleration){
		dx = scale(dx);
		dy = scale(dy);
	}
	absmousetrack(mouse.xy.x + dx, mouse.xy.y + dy, b, msec);
}

void
absmousetrack(int x, int y, int b, ulong msec)
{
	int lastb;

	if(gscreen==nil)
		return;

	if(x < gscreen->clipr.min.x)
		x = gscreen->clipr.min.x;
	if(x >= gscreen->clipr.max.x)
		x = gscreen->clipr.max.x-1;
	if(y < gscreen->clipr.min.y)
		y = gscreen->clipr.min.y;
	if(y >= gscreen->clipr.max.y)
		y = gscreen->clipr.max.y-1;


	ilock(&mouse);
	mouse.xy = Pt(x, y);
	lastb = mouse.buttons;
	b |= mouse.inbuttons;
	mouse.buttons = b;
	mouse.msec = msec;
	mouse.counter++;

	/*
	 * if the queue fills, don't queue any more events until a
	 * reader polls the mouse.
	 */
	if(b != lastb && (mouse.wi-mouse.ri) < nelem(mouse.queue))
		mouse.queue[mouse.wi++ % nelem(mouse.queue)] = mouse.Mousestate;
	iunlock(&mouse);

	wakeup(&mouse.r);

	mouseredraw();
}

void
scmousetrack(int x, int y, int b, ulong msec)
{
	vlong vx, vy;

	if(gscreen==nil)
		return;
	
	vx = (vlong)(uint)x * (gscreen->clipr.max.x - gscreen->clipr.min.x);
	x = (vx + (1<<30) - (~vx>>31&1) >> 31) + gscreen->clipr.min.x;
	vy = (vlong)(uint)y * (gscreen->clipr.max.y - gscreen->clipr.min.y);
	y = (vy + (1<<30) - (~vy>>31&1) >> 31) + gscreen->clipr.min.y;
	
	absmousetrack(x, y, b, msec);
}

static ulong
lastms(void)
{
	static ulong lasttick;
	ulong t, d;

	t = MACHP(0)->ticks;
	d = t - lasttick;
	lasttick = t;
	return TK2MS(d);
}

/*
 *  microsoft 3 button, 7 bit bytes
 *
 *	byte 0 -	1  L  R Y7 Y6 X7 X6
 *	byte 1 -	0 X5 X4 X3 X2 X1 X0
 *	byte 2 -	0 Y5 Y4 Y3 Y2 Y1 Y0
 *	byte 3 -	0  M  x  x  x  x  x	(optional)
 *
 *  shift & right button is the same as middle button (for 2 button mice)
 */
int
m3mouseputc(Queue*, int c)
{
	static uchar msg[3];
	static int nb;
	static int middle;
	static uchar b[] = { 0, 4, 1, 5, 0, 2, 1, 3 };
	short x;
	int dx, dy, newbuttons;

	if(lastms() > 500)
		nb = 0;
	if(nb == 3){
		nb = 0;
		/*
		 * an extra byte comes for middle button motion.
		 * only two possible values for the extra byte.
		 */
		if(c == 0x00 || c == 0x20){
			/* an extra byte gets sent for the middle button */
			middle = (c&0x20) ? 2 : 0;
			newbuttons = (mouse.buttons & ~2) | middle;
			mousetrack(0, 0, newbuttons, TK2MS(MACHP(0)->ticks));
			return 0;
		}
	}
	msg[nb] = c;
	if(++nb == 3){
		newbuttons = middle | b[(msg[0]>>4)&3];
		x = (msg[0]&0x3)<<14;
		dx = (x>>8) | msg[1];
		x = (msg[0]&0xc)<<12;
		dy = (x>>8) | msg[2];
		mousetrack(dx, dy, newbuttons, TK2MS(MACHP(0)->ticks));
	}
	return 0;
}

/*
 * microsoft intellimouse 3 buttons + scroll
 * 	byte 0 -	1  L  R Y7 Y6 X7 X6
 *	byte 1 -	0 X5 X4 X3 X2 X1 X0
 *	byte 2 -	0 Y5 Y4 Y3 Y2 Y1 Y0
 *	byte 3 -	0  0  M  %  %  %  %
 *
 *	%: 0xf => U , 0x1 => D
 *
 *	L: left
 *	R: right
 *	U: up
 *	D: down
 */
int
m5mouseputc(Queue*, int c)
{
	static uchar msg[4];
	static int nb;

	if(lastms() > 500)
		nb = 0;
	msg[nb] = c & 0x7f;
	if(++nb == 4){
		nb = 0;
		schar dx,dy,newbuttons;
		dx = msg[1] | (msg[0] & 0x3) << 6;
		dy = msg[2] | (msg[0] & 0xc) << 4;
		newbuttons =
			(msg[0] & 0x10) >> 2
			| (msg[0] & 0x20) >> 5
			| ( msg[3] == 0x10 ? 0x02 :
			    msg[3] == 0x0f ? ScrollUp :
			    msg[3] == 0x01 ? ScrollDown : 0 );
		mousetrack(dx, dy, newbuttons, TK2MS(MACHP(0)->ticks));
	}
	return 0;
}

/*
 *  Logitech 5 byte packed binary mouse format, 8 bit bytes
 *
 *  shift & right button is the same as middle button (for 2 button mice)
 */
int
mouseputc(Queue*, int c)
{
	static short msg[5];
	static int nb;
	static uchar b[] = {0, 4, 2, 6, 1, 5, 3, 7, 0, 2, 2, 6, 1, 3, 3, 7};
	int dx, dy, newbuttons;

	if(lastms() > 500 || (c&0xF0) == 0x80)
		nb = 0;
	msg[nb] = c;
	if(c & 0x80)
		msg[nb] |= ~0xFF;	/* sign extend */
	if(++nb == 5){
		nb = 0;
		newbuttons = b[((msg[0]&7)^7)];
		dx = msg[1]+msg[3];
		dy = -(msg[2]+msg[4]);
		mousetrack(dx, dy, newbuttons, TK2MS(MACHP(0)->ticks));
	}
	return 0;
}

int
mousechanged(void*)
{
	return mouse.lastcounter != mouse.counter || mouse.resize != 0;
}

Point
mousexy(void)
{
	return mouse.xy;
}

void
mouseaccelerate(int x)
{
	mouse.acceleration = x;
	if(mouse.acceleration < 3)
		mouse.maxacc = 2;
	else
		mouse.maxacc = mouse.acceleration;
}

/*
 * notify reader that screen has been resized
 */
void
mouseresize(void)
{
	mouse.resize = 1;
	wakeup(&mouse.r);
}

void
mouseredraw(void)
{
	mouse.redraw = 1;
	wakeup(&mouse.redrawr);
}
