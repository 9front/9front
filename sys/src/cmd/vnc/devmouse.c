#include	<u.h>
#include	<libc.h>
#include	"compat.h"
#include	"error.h"

#define	Image	IMAGE
#include	<draw.h>
#include	<memdraw.h>
#include	<cursor.h>
#include	"screen.h"

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
	ulong	lastcounter;	/* value when /dev/mouse read */
	Rendez	r;
	Ref;
	int	resize;
	int	open;
	Mousestate	queue[16];	/* circular buffer of click events */
	ulong	ri;		/* read index into queue */
	ulong	wi;		/* write index into queue */
};

Mouseinfo	mouse;
Cursorinfo	cursor;
Cursor		curs;

void	Cursortocursor(Cursor*);
int	mousechanged(void*);

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
	"mousein",	{Qmousein},	0,			0222,
	"mousectl",	{Qmousectl},	0,			0222,
};

Cursor	arrow = {
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

extern Memimage* gscreen;
extern void mousewarpnote(Point);

static void
mousereset(void)
{
	curs = arrow;
	Cursortocursor(&arrow);
}

static void
mouseinit(void)
{
	curs = arrow;
	Cursortocursor(&arrow);
	cursoron();
}

static Chan*
mouseattach(char *spec)
{
	return devattach('m', spec);
}

static Walkqid*
mousewalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, mousedir, nelem(mousedir), devgen);
}

static int
mousestat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, mousedir, nelem(mousedir), devgen);
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
	case Qmousectl:
		error(Egreg);
		break;
	case Qmouse:
		if(_tas(&mouse.open) != 0)
			error(Einuse);
		mouse.lastcounter = mouse.counter;
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
	case Qmouse:
		mouse.open = 0;
		/* wet floor */
	case Qcursor:
		if(decref(&mouse) != 0)
			return;
		cursoroff();
		curs = arrow;
		Cursortocursor(&arrow);
		cursoron();
	}
}


static long
mouseread(Chan *c, void *va, long n, vlong off)
{
	char buf[1+4*12+1];
	uchar *p;
	ulong offset = off;
	Mousestate m;

	p = va;
	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, va, n, mousedir, nelem(mousedir), devgen);

	case Qcursor:
		if(offset != 0)
			return 0;
		if(n < 2*4+2*2*16)
			error(Eshort);
		n = 2*4+2*2*16;
		BPLONG(p+0, curs.offset.x);
		BPLONG(p+4, curs.offset.y);
		memmove(p+8, curs.clr, 2*16);
		memmove(p+40, curs.set, 2*16);
		return n;

	case Qmouse:
		while(mousechanged(0) == 0)
			rendsleep(&mouse.r, mousechanged, 0);

		lock(&mouse);
		if(mouse.ri != mouse.wi)
			m = mouse.queue[mouse.ri++ % nelem(mouse.queue)];
		else
			m = mouse.Mousestate;
		unlock(&mouse);

		sprint(buf, "m%11d %11d %11d %11ld ",
			m.xy.x, m.xy.y, m.buttons, m.msec);

		mouse.lastcounter = m.counter;
		if(mouse.resize){
			mouse.resize = 0;
			buf[0] = 'r';
		}

		if(n > 1+4*12)
			n = 1+4*12;
		memmove(va, buf, n);
		return n;
	}
	return 0;
}

static long
mousewrite(Chan *c, void *va, long n, vlong)
{
	char *p;
	Point pt;
	char buf[64];

	p = va;
	switch((ulong)c->qid.path){
	case Qdir:
		error(Eisdir);

	case Qcursor:
		cursoroff();
		if(n < 2*4+2*2*16){
			curs = arrow;
			Cursortocursor(&arrow);
		}else{
			n = 2*4+2*2*16;
			curs.offset.x = BGLONG(p+0);
			curs.offset.y = BGLONG(p+4);
			memmove(curs.clr, p+8, 2*16);
			memmove(curs.set, p+40, 2*16);
			Cursortocursor(&curs);
		}
		cursoron();
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
		absmousetrack(pt.x, pt.y, mouse.buttons, nsec()/(1000*1000LL));
		mousewarpnote(pt);
		return n;
	}

	error(Egreg);
	return -1;
}

Dev mousedevtab = {
	'm',
	"mouse",

	mousereset,
	mouseinit,
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

void
Cursortocursor(Cursor *c)
{
	lock(&cursor);
	memmove(&cursor.Cursor, c, sizeof(Cursor));
	setcursor(c);
	unlock(&cursor);
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


	lock(&mouse);
	mouse.xy = Pt(x, y);
	lastb = mouse.buttons;
	mouse.buttons = b;
	mouse.msec = msec;
	mouse.counter++;

	/*
	 * if the queue fills, don't queue any more events until a
	 * reader polls the mouse.
	 */
	if(b != lastb && (mouse.wi-mouse.ri) < nelem(mouse.queue))
		mouse.queue[mouse.wi++ % nelem(mouse.queue)] = mouse.Mousestate;
	unlock(&mouse);

	rendwakeup(&mouse.r);

	cursoron();
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

/*
 * notify reader that screen has been resized
 */
void
mouseresize(void)
{
	mouse.resize = 1;
	rendwakeup(&mouse.r);
}
