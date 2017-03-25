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

Cursor zoomcursor = {
	{-7, -7},
	{0x1F, 0xF8, 0x3F, 0xFC, 0x7F, 0xFE, 0xFB, 0xDF,
	 0xF3, 0xCF, 0xE3, 0xC7, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0xE3, 0xC7, 0xF3, 0xCF,
	 0x7B, 0xDF, 0x7F, 0xFE, 0x3F, 0xFC, 0x1F, 0xF8, },
	{0x00, 0x00, 0x0F, 0xF0, 0x31, 0x8C, 0x21, 0x84,
	 0x41, 0x82, 0x41, 0x82, 0x41, 0x82, 0x7F, 0xFE,
	 0x7F, 0xFE, 0x41, 0x82, 0x41, 0x82, 0x41, 0x82,
	 0x21, 0x84, 0x31, 0x8C, 0x0F, 0xF0, 0x00, 0x00, }
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
	DOBODY = 0,
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
int showv, showa, paused;

char *menustr[] = {
	[DOBODY]	"new body",
	[SAVE]	"save",
	[LOAD]	"load",
	[SPEED]	"speed",
	[GRAV]	"gravity",
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
	static int pid = -1;

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

	snprint(buf, sizeof(buf), "Number of bodies: %d", glxy.nb);
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
	for(b = glxy.a; b < glxy.a + glxy.nb; b++) {
		pos = topoint(b->Vector);
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

	pos = topoint(b->Vector);
	d = subpt(mc->xy, pos);
	h = hypot(d.x, d.y);
	b->size = h == 0 ? scale : h*scale;
	b->mass = b->size*b->size*b->size;
}

void
setvel(Body *b)
{
	Point pos, d;

	pos = topoint(b->Vector);
	d = subpt(mc->xy, pos);
	b->v.x = d.x*scale/10;
	b->v.y = d.y*scale/10;
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

	for(;;) {
		readmouse(mc);
		if(mc->buttons == 0)
			continue;
		if(mc->buttons == 1)
			break;
		return;
	}

	b = body();
	b->Vector = tovector(mc->xy);
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
			b->Vector = tovector(mc->xy);
	}

	CHECKLIM(b, f);

	gc = center();
	orig.x += gc.x / scale;
	orig.y += gc.y / scale;
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
domove(void)
{
	Point oldp, off;

	setcursor(mc, &crosscursor);
	oldp = mc->xy;
	for(;;) {
		readmouse(mc);
		if(mc->buttons != 1)
			break;
		off = subpt(mc->xy, oldp);
		oldp = mc->xy;
		pause(0, 0);
		orig = addpt(orig, off);
		drawglxy();
		pause(1, 0);
	}
	setcursor(mc, cursor);
}

Point
screencenter(void)
{
	Point sc;

	sc = divpt(subpt(screen->r.max, screen->r.min), 2);
	return addpt(screen->r.min, sc);
}

void
dozoom(void)
{
	Point oxy, d, sc, off;
	Vector gsc;
	double z, oscale;

	setcursor(mc, &zoomcursor);
	oxy = mc->xy;
	oscale = scale;
	sc = screencenter();
	for(;;) {
		readmouse(mc);
		if(mc->buttons != 2)
			break;
		d = subpt(mc->xy, oxy);
		z = tanh((double)d.y/200) + 1;
		gsc = tovector(sc);
		pause(0, 0);
		scale = z*oscale;
		off = subpt(topoint(gsc), sc);
		orig = subpt(orig, off);
		drawglxy();
		pause(1, 0);
	}
	setcursor(mc, cursor);
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
	case DOBODY:
		dobody();
		break;
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
	case EXIT:
		quit(nil);
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
			domove();
			break;
		case 2:
			dozoom();
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

	threadsetname("keyboard");
	realkc = initkeyboard(nil);
	if(realkc == nil)
		sysfatal("kbdthread: could not initkeyboard: %r");

	for(;;) {
		recv(realkc->c, &r);
		if(r == Kdel)
			quit(nil);

		if(kc.c != nil)
			send(kc.c, &r);
		else switch(r) {
		case 'q':
			quit(nil);
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
			if(paused) {
				cursor = nil;
				pause(1, 1);
			} else {
				cursor = &pausecursor;
				pause(0, 1);
			}
			setcursor(mc, cursor);
		}
		drawglxy();
	}
}

Vector
tovector(Point p)
{
	Vector v;

	v.x = (p.x-orig.x) * scale;
	v.y = (p.y-orig.y) * scale;
	return v;
}

void
quit(char *e)
{
	pause(0, 0);
	threadexitsall(e);
}

void
usage(void)
{
	fprint(2, "Usage: %s [-t throttle] [-G gravity] [-ε smooth] [-p extraproc] [-i] [file]\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	char* nproc;
	int doload;

	doload = 0;
	ARGBEGIN {
	default:
		usage();
		break;
	case 'p':
		extraproc = strtol(EARGF(usage()), nil, 0);
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

	if(extraproc < 0) {
		nproc = getenv("NPROC");
		if(nproc == nil)
			extraproc = 0;
		else
			extraproc = strtol(nproc, nil, 10) - 1;
		if(extraproc < 0)
			extraproc = 0;
	}

	if(initdraw(nil, nil, "Galaxy") < 0)
		sysfatal("initdraw failed: %r");
	if(mc = initmouse(nil, screen), mc == nil)
		sysfatal("initmouse failed: %r");

	dt² = dt*dt;
	orig = screencenter();
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
