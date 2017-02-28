#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <cursor.h>
#include <keyboard.h>
#include "galaxy.h"

Cursor crosscursor = {
	{-7, -7},
	{0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0,
	 0x03, 0xC0, 0x03, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xC0, 0x03, 0xC0,
	 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, },
	{0x00, 0x00, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80,
	 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x7F, 0xFE,
	 0x7F, 0xFE, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80,
	 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x00, 0x00, }
};

Cursor pausecursor={
	0, 0,
	0x01, 0x80, 0x03, 0xC0, 0x07, 0xE0, 0x07, 0xe0,
	0x07, 0xe0, 0x07, 0xe0, 0x03, 0xc0, 0x0F, 0xF0,
	0x1F, 0xF8, 0x1F, 0xF8, 0x1F, 0xF8, 0x1F, 0xF8,
	0x0F, 0xF0, 0x1F, 0xF8, 0x3F, 0xFC, 0x3F, 0xFC,

	0x01, 0x80, 0x03, 0xC0, 0x07, 0xE0, 0x04, 0x20,
	0x04, 0x20, 0x06, 0x60, 0x02, 0x40, 0x0C, 0x30,
	0x10, 0x08, 0x14, 0x08, 0x14, 0x28, 0x12, 0x28,
	0x0A, 0x50, 0x16, 0x68, 0x20, 0x04, 0x3F, 0xFC,
};

enum {
	STK = 8192,
	MOVE = 0,
	ZOOM,
	SPEED,
	GRAV,
	SAVE,
	LOAD,
	EXIT,
	MEND,
};

Cursor *cursor;
Mousectl *mc;
Keyboardctl kc;
double
	G = 1,
	θ = 1,
	scale = 30,
	ε = 500,
	dt = .2,
	LIM = 10,
	dt²;
char *file;
int showv, showa, throttle, moving;

char *menustr[] = {
	[SAVE]	"save",
	[LOAD]	"load",
	[ZOOM]	"zoom",
	[SPEED]	"speed",
	[GRAV]	"gravity",
	[MOVE]	"move",
	[EXIT]	"exit",
	[MEND]	nil
};
Menu menu = {
	.item menustr
};

Image*
randcol(void)
{
	static struct {
		ulong c;
		Image *i;
	} cols[] = {
		DWhite, nil,
		DRed, nil,
		DGreen, nil,
		DCyan, nil,
		DMagenta, nil,
		DYellow, nil,
		DPaleyellow, nil,
		DDarkyellow, nil,
		DDarkgreen, nil,
		DPalegreen, nil,
		DPalebluegreen, nil,
		DPaleblue, nil,
		DPalegreygreen, nil,
		DYellowgreen, nil,
		DGreyblue, nil,
		DPalegreyblue, nil,
	};
	int r;

	r = nrand(nelem(cols));
	if(cols[r].i == nil)
		cols[r].i = allocimage(display, Rect(0,0,1,1), screen->chan, 1, cols[r].c);
	return cols[r].i;
}

void
pause(int p, int id)
{
	static int paused, pid = -1;

	switch(p) {
	default:
		sysfatal("invalid pause value %d:", p);
		break;
	case 0:
		if(pid != -1 && pid != id)
			break;
		pid = id;
		if(paused)
			break;
		paused = 1;
		qlock(&glxy);
		break;
	case 1:
		if(!paused || pid != id)
			break;
		pid = -1;
		paused = 0;
		qunlock(&glxy);
		break;
	}
}

void
drawstats(void)
{
	Point p;
	static char buf[1024];

	snprint(buf, sizeof(buf), "Number of bodies: %d", glxy.l);
	p = addpt(screen->r.min, (Point){5, 3});
	string(screen, p, display->white, ZP, font, buf);

	snprint(buf, sizeof(buf), "Avg. calculations per body: %g", avgcalcs);
	p = addpt(p, (Point){0, font->height});
	string(screen, p, display->white, ZP, font, buf);

	snprint(buf, sizeof(buf), "Max depth of quad tree: %d", quaddepth);
	p = addpt(p, (Point){0, font->height});
	string(screen, p, display->white, ZP, font, buf);
}

void
drawglxy(void)
{
	Point pos, va;
	Body *b;
	int s;

	draw(screen, screen->r, display->black, 0, ZP);
	for(b = glxy.a; b < glxy.a + glxy.l; b++) {
		pos.x = b->x / scale + orig.x;
		pos.y = b->y / scale + orig.y;
		s = b->size/scale;
		fillellipse(screen, pos, s, s, b->col, ZP);
		if(showv) {
			va.x = b->v.x/scale;
			va.y = b->v.y/scale;
			if(va.x != 0 || va.y != 0)
				line(screen, pos, addpt(pos, va), Enddisc, Endarrow, 0, b->col, ZP);
		}
		if(showa) {
			va.x = b->a.x/scale*50;
			va.y = b->a.y/scale*50;
			if(va.x != 0 || va.y != 0)
				line(screen, pos, addpt(pos, va), Enddisc, Endarrow, 0, b->col, ZP);
		}
	}
	STATS(drawstats();)
	flushimage(display, 1);
}

void
setsize(Body *b)
{
	Point pos, d;
	double h;

	pos.x = b->x / scale + orig.x;
	pos.y = b->y / scale + orig.y;
	d = subpt(mc->xy, pos);
	h = hypot(d.x, d.y);
	b->size = h == 0 ? scale : h*scale;
	b->mass = b->size*b->size*b->size;
}

void
setvel(Body *b)
{
	Point pos, d;

	pos.x = b->x / scale + orig.x;
	pos.y = b->y / scale + orig.y;
	d = subpt(mc->xy, pos);
	b->v.x = (double)d.x*scale/10;
	b->v.y = (double)d.y*scale/10;
}

void
setpos(Body *b)
{
	b->x = (mc->xy.x - orig.x) * scale;
	b->y = (mc->xy.y - orig.y) * scale;
}

void
dosize(Body *b)
{
	Point p;

	p = mc->xy;
	for(;;) {
		setsize(b);
		drawglxy();
		drawbody(b);
		readmouse(mc);
		if(mc->buttons != 3)
			break;
	}
	moveto(mc, p);
}

void
dovel(Body *b)
{
	Point p;
	p = mc->xy;
	for(;;) {
		setvel(b);
		drawglxy();
		drawbody(b);
		readmouse(mc);
		if(mc->buttons != 5)
			break;
	}
	moveto(mc, p);
}

void
dobody(void)
{
	Vector gc;
	double f;
	Body *b;

	pause(0, 0);
	b = body();
	setpos(b);
	setvel(b);
	setsize(b);
	b->col = randcol();
	for(;;) {
		drawglxy();
		drawbody(b);
		readmouse(mc);
		if(!(mc->buttons & 1))
			break;
		if(mc->buttons == 3)
			dosize(b);
		else if(mc->buttons == 5)
			dovel(b);
		else
			setpos(b);
	}

	CHECKLIM(b, f);

	gc = center();
	orig.x += gc.x / scale;
	orig.y += gc.y / scale;

	pause(1, 0);
}

char*
getinput(char *info, char *sug)
{
	static char buf[1024];
	static Channel *rchan;
	char *input;
	int r;

	if(rchan == nil)
		rchan = chancreate(sizeof(Rune), 20);

	if(sug != nil)
		strecpy(buf, buf+1024, sug);
	else
		buf[0] = '\0';

	kc.c = rchan;
	r = enter(info, buf, sizeof(buf), mc, &kc, nil);
	kc.c = nil;
	if(r < 0)
		sysfatal("save: could not get filename: %r");

	input = strdup(buf);
	if(input == nil)
		sysfatal("getinput: could not save input: %r");
	return input;
}

void
move(void)
{
	Point od;
	setcursor(mc, &crosscursor);
	for(;;) {
		for(;;) {
			readmouse(mc);
			if(mc->buttons & 1)
				break;
			if(mc->buttons & 4) {
				moving = 0;
				setcursor(mc, cursor);
				return;
			}
		}
		moving = 1;
		od = subpt(orig, mc->xy);
		for(;;) {
			readmouse(mc);
			if(!(mc->buttons & 1))
				break;
			orig = addpt(od, mc->xy);
			drawglxy();
		}
	}
}

void
load(int fd)
{
	orig = divpt(subpt(screen->r.max, screen->r.min), 2);
	orig = addpt(orig, screen->r.min);
	readglxy(fd);
	center();
}

void
domenu(void)
{
	int fd;
	char *s;
	double z;

	pause(0, 0);
	switch(menuhit(3, mc, &menu, nil)) {
	case SAVE:
		s = getinput("Enter file:", file);
		if(s == nil || *s == '\0')
			break;
		free(file);
		file = s;
		fd = create(file, OWRITE, 0666);
		if(fd < 0)
			sysfatal("domenu: could not create file %s: %r", file);
		writeglxy(fd);
		close(fd);
		break;
	case LOAD:
		s = getinput("Enter file:", file);
		if(s == nil || *s == '\0')
			break;
		free(file);
		file = s;
		fd = open(file, OREAD);
		if(fd < 0)
			sysfatal("domenu: could not open file %s: %r", file);
		load(fd);
		close(fd);
		break;
	case ZOOM:
		s = getinput("Zoom multiplier:", nil);
		if(s == nil || *s == '\0')
			break;
		z = strtod(s, nil);
		free(s);
		if(z <= 0)
			break;
		scale /= z;
		break;
	case SPEED:
		s = getinput("Speed multiplier:", nil);
		if(s == nil || *s == '\0')
			break;
		z = strtod(s, nil);
		free(s);
		if(z <= 0)
			break;
		dt *= z;
		dt² = dt*dt;
		break;
	case GRAV:
		s = getinput("Gravity multiplier:", nil);
		if(s == nil || *s == '\0')
			break;
		z = strtod(s, nil);
		free(s);
		if(z <= 0)
			break;
		G *= z;
		break;
	case MOVE:
		move();
		break;
	case EXIT:
		threadexitsall(nil);
		break;
	}
	drawglxy();
	pause(1, 0);
}

void
mousethread(void*)
{
	threadsetname("mouse");
	for(;;) {
		readmouse(mc);
		switch(mc->buttons) {
		case 1:
			dobody();
			break;
		case 4:
			domenu();
			break;
		}
	}
}

void
resizethread(void*)
{
	threadsetname("resize");
	for(;;) {
		recv(mc->resizec, nil);
		pause(0, 0);
		if(getwindow(display, Refnone) < 0)
			sysfatal("resize failed: %r");
		drawglxy();
		pause(1, 0);
	}
}

void
kbdthread(void*)
{
	Keyboardctl *realkc;
	Rune r;
	static int paused;

	threadsetname("keyboard");
	realkc = initkeyboard(nil);
	if(realkc == nil)
		sysfatal("kbdthread: could not initkeyboard: %r");

	for(;;) {
		recv(realkc->c, &r);
		if(r == Kdel) {
			threadexitsall(nil);
		}
		if(kc.c != nil)
			send(kc.c, &r);
		else switch(r) {
		case 'q':
			threadexitsall(nil);
			break;
		case 's':
			stats ^= 1;
			break;
		case 'v':
			showv ^= 1;
			break;
		case 'a':
			showa ^= 1;
			break;
		case ' ':
			if(moving)
				break;
			paused ^= 1;
			if(paused) {
				cursor = &pausecursor;
				pause(0, 1);
			} else {
				cursor = nil;
				pause(1, 1);
			}
			setcursor(mc, cursor);
		}
	}
}

/* verlet barnes-hut */
void
simulate(void*)
{
	Body *b;
	double f;

	threadsetname("simulate");

	for(;;) {
		qlock(&glxy);

		if(throttle)
			sleep(throttle);

		drawglxy();

Again:
		space.t = EMPTY;
		quads.l = 0;
		STATS(quaddepth = 0;)
		for(b = glxy.a; b < glxy.a + glxy.l; b++) {
			if(quadins(b, LIM) == -1) {
				growquads();
				goto Again;
			}
		}

		STATS(avgcalcs = 0;)
		for(b = glxy.a; b < glxy.a + glxy.l; b++) {
			b->a.x = b->newa.x;
			b->a.y = b->newa.y;
			b->newa.x = b->newa.y = 0;
			STATS(calcs = 0;)
			quadcalc(space, b, LIM);
			STATS(avgcalcs += calcs;)
		}
		STATS(avgcalcs /= glxy.l;)

		for(b = glxy.a; b < glxy.a + glxy.l; b++) {
			b->x += dt*b->v.x + dt²*b->a.x/2;
			b->y += dt*b->v.y + dt²*b->a.y/2;
			b->v.x += dt*(b->a.x + b->newa.x)/2;
			b->v.y += dt*(b->a.y + b->newa.y)/2;
			CHECKLIM(b, f);
		}

		qunlock(&glxy);
	}
}

void
usage(void)
{
	fprint(2, "Usage: %s [-t throttle] [-G gravity] [-ε smooth] [-i] [file]\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	int doload;

	doload = 0;
	ARGBEGIN {
	default:
		usage();
		break;
	case 't':
		throttle = strtol(EARGF(usage()), nil, 0);
		break;
	case 'G':
		G = strtod(EARGF(usage()), nil);
		break;
	case L'ε':
		ε = strtod(EARGF(usage()), nil);
		break;
	case 'i':
		doload++;
		break;
	} ARGEND

	if(argc > 1)
		usage();

	fmtinstall('B', Bfmt);

	if(argc == 1) {
		if(doload++)
			usage();
		file = strdup(argv[0]);
		if(file == nil)
			sysfatal("threadmain: could not save file name: %r");
		close(0);
		if(open(file, OREAD) != 0)
			sysfatal("threadmain: could not open file: %r");
	}

	if(initdraw(nil, nil, "Galaxy") < 0)
		sysfatal("initdraw failed: %r");
	if(mc = initmouse(nil, screen), mc == nil)
		sysfatal("initmouse failed: %r");

	dt² = dt*dt;
	orig = divpt(subpt(screen->r.max, screen->r.min), 2);
	orig = addpt(orig, screen->r.min);
	glxyinit();
	quadsinit();
	if(doload)
		load(0);
	close(0);
	threadcreate(mousethread, nil, STK);
	threadcreate(resizethread, nil, STK);
	threadcreate(kbdthread, nil, STK);
	proccreate(simulate, nil, STK);
	threadexits(nil);
}
