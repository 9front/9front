/*
 * rotzoomer - an affinewarp demo
 */
#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>

typedef struct Strobos Strobos;
typedef struct Backlight Backlight;

struct Strobos
{
	Point p;
	Point txtsz;
	Image *c;
	Image *back;
	Image *txtim;
	char *txt;
	Warp txtwp;
};

struct Backlight
{
	Image *c;	/* bag of colors */
	Image *b;	/* chosen one */
	Warp w;		/* roulette */
};

Rectangle UR = {0,0,1,1};

Image *bg;
Backlight *bl;
Image *sprite;
Image *screenb;
Mousectl *mctl;
Keyboardctl *kctl;
Channel *drawc;
Warp warp;
int nframes;
Strobos **strobostab;
int nstrobos;
int smooth;

void *
emalloc(ulong size)
{
	void *p;

	p = malloc(size);
	if(p == nil)
		sysfatal("malloc: %r");
	setmalloctag(p, getcallerpc(&size));
	return p;
}

Image *
eallocimage(Display *disp, Rectangle r, ulong chan, int repl, ulong col)
{
	Image *i;

	i = allocimage(disp, r, chan, repl, col);
	if(i == nil)
		sysfatal("allocimage: %r");
	setmalloctag(i, getcallerpc(&disp));
	return i;
}

Image *
allocbasketweave(void)
{
	static uchar pattern[] = {
		0x07, 0x8B, 0xDD, 0xB8,
		0x70, 0xE8, 0xDD, 0x8E,
	};
	Image *i;

	i = eallocimage(display, Rect(0,0,8,8), GREY1, 1, DNofill);
	if(loadimage(i, i->r, pattern, 8) != 8)
		sysfatal("loadimage: %r");
	return i;
}

Backlight *
allocbacklight(void)
{
	static ulong pattern[] = {
		0x888888, 0xDDDDDD,
		0xBBBBBB, 0xFFFFFF,
	};
	Backlight *b;

	b = emalloc(sizeof(Backlight));
	b->c = eallocimage(display, Rect(0,0,2,2), XRGB32, 1, DNofill);
	b->b = eallocimage(display, UR, XRGB32, 1, DNofill);
	if(loadimage(b->c, b->c->r, (uchar*)pattern, 2*2*4) != 2*2*4)
		sysfatal("loadimage: %r");
	return b;
}

void
drawstats(void)
{
	static Point sp = {10,10};
	char s[128];

	snprint(s, sizeof s, "frames %d", nframes);
	stringbg(screenb, sp, display->white, ZP, font, s, display->black, ZP);
}

Strobos *
allocstrobos(char *txt, ulong col)
{
	Strobos *s;

	s = emalloc(sizeof(Strobos));
	s->c = eallocimage(display, UR, RGBA32, 1, col);
	s->txt = txt;
	s->txtsz = stringsize(font, txt);
	s->txtim = eallocimage(display, Rpt(ZP, s->txtsz), RGBA32, 0, DTransparent);
	string(s->txtim, ZP, s->c, ZP, font, txt);
	s->back = eallocimage(display, Rpt(ZP, mulpt(s->txtsz, 4)), RGBA32, 0, DTransparent);
	return s;
}

void
initstrobos(void)
{
	static struct {
		char *lbl;
		ulong col;
	} tab[] = {
		"MAKE ME AN OFFER", DPaleyellow,
		"ONE HUNDRED BILLION DOLLARS", DYellow,
		"THE PRICEMASTER HAS SPOKEN", DDarkyellow,
	};
	int i;

	nstrobos = nelem(tab);
	strobostab = emalloc(nstrobos*sizeof(Strobos *));
	for(i = 0; i < nstrobos; i++)
		strobostab[i] = allocstrobos(tab[i].lbl, setalpha(tab[i].col, 0x7F));
}

void
drawstrobos(Strobos *s)
{
	double ss, θ;

	if(nframes % 75 == 0){	/* 2½ seconds */
		s->p.x = 50 + frand()*(Dx(screenb->r)-50-50 - Dx(s->back->r));
		s->p.y = 50 + frand()*(Dy(screenb->r)-50-50 - Dy(s->back->r));
	}

	if((nframes & 1) == 0){	/* 1/15 second */
		ss = (frand() + 0.2)*(4/1.2);
		θ = (frand()*10 - 10/2)*DEG;
		Matrix S = {
			ss, 0, 0,
			0, ss, 0,
			0, 0, 1,
		}, R = {
			cos(θ), -sin(θ), 0,
			sin(θ), cos(θ), 0,
			0, 0, 1,
		}, T₀ = {
			1, 0, -s->txtsz.x/2,
			0, 1, -s->txtsz.y/2,
			0, 0, 1,
		}, T₁ = {
			1, 0, s->txtsz.x/2,
			0, 1, s->txtsz.y/2,
			0, 0, 1,
		};
		mulm(R, T₀);
		mulm(T₁, R);
		mulm(S, T₁);
		s->txtwp = mkwarp(S);
	}

	affinewarpop(s->back, s->back->r, s->txtim, nil, s->txtim->r.min, &s->txtwp, smooth, S);
	draw(screenb, rectaddpt(s->back->r, addpt(screenb->r.min, s->p)), s->back, nil, ZP);
}

void
redraw(void)
{
	int i;

	rlockdisplay(display);
	affinewarp(bl->b, bl->b->r, bl->c, nil, ZP, &bl->w, 0);
	draw(screenb, screenb->r, bl->b, bg, ZP);
	affinewarp(screenb, insetrect(screenb->r, 50), sprite, nil, sprite->r.min, &warp, smooth);
	drawstats();
	for(i = 0; i < nstrobos; i++)
		drawstrobos(strobostab[i]);
	draw(screen, screen->r, screenb, nil, ZP);
	flushimage(display, 1);
	runlockdisplay(display);
}

void
drawproc(void*)
{
	threadsetname("drawproc");

	for(;;){
		recv(drawc, nil);
		redraw();
		nframes++;
	}
}

void
update(double f)
{
	Point t;
	double c, s, ss;

	c = cos(f);
	s = sin(f);
	ss = sin(f*2 + 10);
	t.x = s*Dx(sprite->r)/2;
	t.y = 0;
	Matrix R = {
		c, -s, 0,
		s,  c, 0,
		0,  0, 1,
	}, S = {
		ss,  0, 0,
		 0, ss, 0,
		 0,  0, 1,
	}, T₀ = {
		1, 0, -Dx(sprite->r)/2,
		0, 1, -Dy(sprite->r)/2,
		0, 0, 1,
	}, T₁ = {
		1, 0, Dx(sprite->r)/2,
		0, 1, Dy(sprite->r)/2,
		0, 0, 1,
	}, T = {
		1, 0, t.x,
		0, 1, t.y,
		0, 0, 1,
	};
	mulm(S, T₀);
	mulm(R, S);
	mulm(T₁, R);
	mulm(T, T₁);
	warp = mkwarp(T);

	/* spin it! */
	t.x = Dx(bl->b->r)/2;
	t.y = Dy(bl->b->r)/2;
	Matrix R₁ = {
		c, -s, 0,
		s,  c, 0,
		0,  0, 1,
	};
	T₀[0][2] = -t.x;
	T₀[1][2] = -t.y;
	mulm(R₁, T₀);
	bl->w = mkwarp(R₁);
}
QLock pauselk;
void
clkproc(void*)
{
	uvlong t0, Δt;

	threadsetname("clkproc");

	for(;;){
		qlock(&pauselk);
		t0 = nsec();
		update(t0/1e9);
		nbsend(drawc, nil);
		qunlock(&pauselk);
		Δt = nsec() - t0;
		if(Δt < 33*1000*1000)
			sleep(33 - Δt/1000000);
	}
}

void
mouse(void)
{
}

void
key(Rune r)
{
	static int paused;
	switch(r){
	case Kdel:
	case 'q':
		threadexitsall(nil);
	case ' ':
		if(paused) qunlock(&pauselk);
		else qlock(&pauselk);
		paused ^= 1;
		break;
	}
}

void
resize(void)
{
	static Point lastsz;
	Point cursz;

	lockdisplay(display);
	if(getwindow(display, Refnone) < 0)
		fprint(2, "can't reattach to window\n");
	cursz = subpt(screen->r.max, screen->r.min);
	if(!eqpt(cursz, lastsz)){
		freeimage(screenb);
		screenb = eallocimage(display, Rpt(ZP, cursz), screen->chan, 0, DBlack);
		lastsz = cursz;
	}
	unlockdisplay(display);
	nbsend(drawc, nil);
}

void
usage(void)
{
	fprint(2, "usage: %s [-s]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Rune r;

	ARGBEGIN{
	case 's':
		smooth++;
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 0)
		usage();

	setfcr(getfcr() &~ FPINVAL);

	if(initdraw(nil, nil, "rotzoomer") < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	bl = allocbacklight();
	bg = allocbasketweave();
	sprite = display->image;
	screenb = eallocimage(display, rectsubpt(screen->r, screen->r.min), screen->chan, 0, DBlack);
	initstrobos();

	unlockdisplay(display);
	drawc = chancreate(sizeof(void*), 1);
	if(drawc == nil)
		sysfatal("chancreate: %r");
	proccreate(drawproc, nil, mainstacksize);
	proccreate(clkproc, nil, mainstacksize);

	enum {MOUSE, RESIZE, KEY};
	Alt a[] = {
		{mctl->c, &mctl->Mouse, CHANRCV},
		{mctl->resizec, nil, CHANRCV},
		{kctl->c, &r, CHANRCV},
		{nil, nil, CHANEND}
	};
	for(;;)
		switch(alt(a)){
		case MOUSE: mouse(); break;
		case RESIZE: resize(); break;
		case KEY: key(r); break;
		}
}
