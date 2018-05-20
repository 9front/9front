#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <bio.h>
#include <mouse.h>
#include <keyboard.h>

Screen *scr;
Image *disp;
Mousectl *mc;
Keyboardctl *kc;
int ctlfd;
char *videoname;

typedef struct Control Control;
struct Control {
	char *unit, *ctrl;
	char *value;
	char *info;
	Control *next;
};
Control *ctls;

Image *bg;

void *
emalloc(ulong n)
{
	void *v;
	
	v = malloc(n);
	if(v == nil) sysfatal("malloc: %r");
	memset(v, 0, n);
	setmalloctag(v, getcallerpc(&n));
	return v;
}

void
screeninit(void)
{
	freescreen(scr);
	scr = allocscreen(screen, bg, 0);
	freeimage(disp);
	disp = allocwindow(scr, screen->r, 0, 0xCCCCCCFF);
	draw(screen, screen->r, bg, nil, ZP);
	flushimage(display, 1);
}

void
usage(void)
{
	fprint(2, "usage: %s cam-device\n", argv0);
	threadexitsall("usage");
}

void
readctls(void)
{
	char *s;
	char *f[5];
	int nf;
	static Biobuf *bp;
	Control *c, **cp;
	
	if(bp == nil)
		bp = Bfdopen(ctlfd, OREAD);
	Bflush(bp);
	Bseek(bp, 0, 0);
	assert(bp != nil);
	cp = &ctls;
	for(; s = Brdstr(bp, '\n', 1), s != nil; free(s)){
		nf = tokenize(s, f, nelem(f));
		if(nf < 3){
			fprint(2, "don't know how to interpret ctl line: %s\n", s);
			continue;
		}
		c = emalloc(sizeof(Control));
		c->unit = strdup(f[0]);
		c->ctrl = strdup(f[1]);
		c->value = strdup(f[2]);
		if(nf >= 4) c->info = strdup(f[3]);
		*cp = c;
		cp = &c->next;
	}
}

void
freectls(void)
{
	Control *c, *d;
	
	for(c = ctls; c != nil; c = d){
		d = c->next;
		free(c->unit);
		free(c->ctrl);
		free(c->value);
		free(c->info);
		free(c);
	}
	ctls = nil;
}

void
opencamera(char *dir)
{
	char *s;
	
	s = smprint("%s/ctl", dir);
	ctlfd = open(s, ORDWR);
	if(ctlfd < 0) sysfatal("open: %r");
	free(s);
	readctls();
	videoname = smprint("%s/video", dir);
}

void
resizethread(void *)
{
	ulong dummy;

	while(recv(mc->resizec, &dummy) > 0){
		lockdisplay(display);
		if(getwindow(display, Refnone) < 0)
			sysfatal("resize failed: %r");
		screeninit();
		unlockdisplay(display);
	}
}

void
rmb(void)
{
	enum {
		QUIT,
	};
	static char *items[] = {
		[QUIT] "quit",
		nil,
	};
	static Menu menu = { .item = items };
	switch(menuhit(3, mc, &menu, scr)){
	case QUIT:
		threadexitsall(nil);
	}
}

char *
ctlgen(int n)
{
	Control *c;
	static char buf[512];
	
	for(c = ctls; n-- > 0 && c != nil; c = c->next)
		;
	if(c == nil)
		return nil;
	snprint(buf, sizeof(buf), "%s(%s) = %s", c->ctrl, c->unit, c->value);
	return buf;
	
}

void
mmb(void)
{
	static char buf[512];
	static char nval[512];
	int n;
	Control *c;
	Menu menu = { .gen = ctlgen };
	
	n = menuhit(2, mc, &menu, scr);
	if(n < 0) return;
	for(c = ctls; n-- > 0 && c != nil; c = c->next)
		;
	assert(c != nil);
	snprint(buf, sizeof(buf), "%s(%s) = %s%c(%s)", c->ctrl, c->unit, c->value, c->info != nil ? ' ' : 0, c->info);
	nval[0] = 0;
	if(enter(buf, nval, sizeof(nval), mc, kc, scr) <= 0) return;
	if(fprint(ctlfd, "%q %q %q", c->unit, c->ctrl, nval) < 0){
		fprint(2, "fprint: %r\n");
		return;
	}
	freectls();
	readctls();
}

void
videoproc(void *)
{
	int fd;
	Image *i;
	Point p, q;
	Rectangle r;

restart:	
	fd = open(videoname, OREAD);
	if(fd < 0) sysfatal("open: %r");
	for(;;){
		i = readimage(display, fd, 1);
		if(i == nil) break;
		p = divpt(addpt(screen->r.min, screen->r.max), 2);
		q = divpt(subpt(i->r.max, i->r.min), 2);
		r = (Rectangle){subpt(p, q), addpt(p, q)};
		lockdisplay(display);
		draw(disp, r, i, nil, i->r.min);
		freeimage(i);
		flushimage(display, 1);
		unlockdisplay(display);
	}
	fprint(2, "readimage: %r\n");
	close(fd);
	goto restart;
}

void
threadmain(int argc, char **argv)
{
	ARGBEGIN {
	default: usage();
	} ARGEND;
	
	quotefmtinstall();
	if(argc != 1) usage();
	opencamera(argv[0]);
	
	if(initdraw(nil, nil, "camv") < 0)
		sysfatal("initdraw: %r");
	bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	screeninit();
	kc = initkeyboard(nil);
	if(kc == nil) sysfatal("initkeyboard: %r");
	mc = initmouse(nil, screen);
	if(mc == nil) sysfatal("initmouse: %r");
	threadcreate(resizethread, nil, mainstacksize);
	proccreate(videoproc, nil, mainstacksize);
	display->locking = 1;
	flushimage(display, 1);
	unlockdisplay(display);
	while(recv(mc->c, &mc->Mouse) >= 0){
		if(mc->buttons == 0)
			continue;
		lockdisplay(display);
		if((mc->buttons & 4) != 0)
			rmb();
		else if((mc->buttons & 2) != 0)
			mmb();
		flushimage(display, 1);
		unlockdisplay(display);
	}
}
