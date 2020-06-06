#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

static Memimage *conscol;
static Memimage *back;

static Point curpos;
static Rectangle window;
static int *xp;
static int xbuf[256];
Lock vgascreenlock;

void
vgaimageinit(ulong)
{
	conscol = memblack;
	back = memwhite;
}

static void
vgascroll(VGAscr* scr)
{
	int h, o;
	Point p;
	Rectangle r;

	h = scr->memdefont->height;
	o = 8*h;
	r = Rpt(window.min, Pt(window.max.x, window.max.y-o));
	p = Pt(window.min.x, window.min.y+o);
	memimagedraw(scr->gscreen, r, scr->gscreen, p, nil, p, S);
	r = Rpt(Pt(window.min.x, window.max.y-o), window.max);
	memimagedraw(scr->gscreen, r, back, ZP, nil, ZP, S);

	curpos.y -= o;
}

static void
vgascreenputc(VGAscr* scr, char* buf, Rectangle *flushr)
{
	Point p;
	int h, w, pos;
	Rectangle r;

	if(xp < xbuf || xp >= &xbuf[nelem(xbuf)])
		xp = xbuf;

	h = scr->memdefont->height;
	switch(buf[0]){
	case '\n':
		if(curpos.y+h >= window.max.y){
			vgascroll(scr);
			*flushr = window;
		}
		curpos.y += h;
		vgascreenputc(scr, "\r", flushr);
		break;

	case '\r':
		xp = xbuf;
		curpos.x = window.min.x;
		break;

	case '\t':
		p = memsubfontwidth(scr->memdefont, " ");
		w = p.x;
		if(curpos.x >= window.max.x-4*w)
			vgascreenputc(scr, "\n", flushr);

		pos = (curpos.x-window.min.x)/w;
		pos = 4-(pos%4);
		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x+pos*w, curpos.y + h);
		memimagedraw(scr->gscreen, r, back, ZP, nil, ZP, S);
		curpos.x += pos*w;
		break;

	case '\b':
		if(xp <= xbuf)
			break;
		xp--;
		r = Rect(*xp, curpos.y, curpos.x, curpos.y+h);
		memimagedraw(scr->gscreen, r, back, r.min, nil, ZP, S);
		combinerect(flushr, r);
		curpos.x = *xp;
		break;

	case '\0':
		break;

	default:
		p = memsubfontwidth(scr->memdefont, buf);
		w = p.x;

		if(curpos.x >= window.max.x-w)
			vgascreenputc(scr, "\n", flushr);

		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x+w, curpos.y+h);
		memimagedraw(scr->gscreen, r, back, r.min, nil, ZP, S);
		memimagestring(scr->gscreen, curpos, conscol, ZP, scr->memdefont, buf);
		combinerect(flushr, r);
		curpos.x += w;
	}
}

static void
vgascreenputs(char* s, int n)
{
	static char rb[UTFmax+1];
	static int nrb;
	char *e;
	int gotdraw;
	VGAscr *scr;
	Rectangle flushr;

	scr = &vgascreen[0];

	if(!islo()){
		/*
		 * Don't deadlock trying to
		 * print in an interrupt.
		 */
		if(!canlock(&vgascreenlock))
			return;
	}
	else
		lock(&vgascreenlock);

	/*
	 * Be nice to hold this, but not going to deadlock
	 * waiting for it.  Just try and see.
	 */
	gotdraw = canqlock(&drawlock);

	flushr = Rect(10000, 10000, -10000, -10000);

	e = s + n;
	while(s < e){
		rb[nrb++] = *s++;
		if(nrb >= UTFmax || fullrune(rb, nrb)){
			rb[nrb] = 0;
			vgascreenputc(scr, rb, &flushr);
			nrb = 0;
		}
	}
	flushmemscreen(flushr);

	if(gotdraw)
		qunlock(&drawlock);
	unlock(&vgascreenlock);
}

static Memimage*
mkcolor(Memimage *screen, ulong color)
{
	Memimage *i;

	if(i = allocmemimage(Rect(0,0,1,1), screen->chan)){
		i->flags |= Frepl;
		i->clipr = screen->r;
		memfillcolor(i, color);
	}
	return i;
}

void
vgascreenwin(VGAscr* scr)
{
	Memimage *i;
	Rectangle r;
	Point p;
	int h;

	qlock(&drawlock);
	
	h = scr->memdefont->height;
	r = scr->gscreen->r;

	if(i = mkcolor(scr->gscreen, 0x0D686BFF)){
		memimagedraw(scr->gscreen, r, i, ZP, nil, ZP, S);
		freememimage(i);
	}

	r = scr->gscreen->clipr;
	window = insetrect(r, 20);
	memimagedraw(scr->gscreen, window, conscol, ZP, memopaque, ZP, S);
	window = insetrect(window, 4);
	memimagedraw(scr->gscreen, window, back, ZP, memopaque, ZP, S);

	if(i = mkcolor(scr->gscreen, 0xAAAAAAFF)){
		memimagedraw(scr->gscreen, Rect(window.min.x, window.min.y,
			window.max.x, window.min.y+h+5+6), i, ZP, nil, ZP, S);
		freememimage(i);

		window = insetrect(window, 5);
		p = addpt(window.min, Pt(10, 0));
		memimagestring(scr->gscreen, p, memblack, ZP, scr->memdefont, " Plan 9 Console ");
		window.min.y += h+6;
	} else
		window = insetrect(window, 5);

	window.max.y = window.min.y+(Dy(window)/h)*h;
	curpos = window.min;

	flushmemscreen(r);

	qunlock(&drawlock);

	vgascreenputs(kmesg.buf, kmesg.n);

	screenputs = vgascreenputs;
}

/*
 * Supposedly this is the way to turn DPMS
 * monitors off using just the VGA registers.
 * Unfortunately, it seems to mess up the video mode
 * on the cards I've tried.
 */
void
vgablank(VGAscr*, int blank)
{
	uchar seq1, crtc17;

	if(blank) {
		seq1 = 0x00;
		crtc17 = 0x80;
	} else {
		seq1 = 0x20;
		crtc17 = 0x00;
	}

	outs(Seqx, 0x0100);			/* synchronous reset */
	seq1 |= vgaxi(Seqx, 1) & ~0x20;
	vgaxo(Seqx, 1, seq1);
	crtc17 |= vgaxi(Crtx, 0x17) & ~0x80;
	delay(10);
	vgaxo(Crtx, 0x17, crtc17);
	outs(Crtx, 0x0300);				/* end synchronous reset */
}

void
addvgaseg(char *name, uvlong pa, ulong size)
{
	Physseg seg;

	if((uintptr)pa != pa || size == 0 || -(uintptr)pa < size){
		print("addvgaseg %s: bad address %llux-%llux pc %#p\n",
			name, pa, pa+size, getcallerpc(&name));
		return;
	}
	memset(&seg, 0, sizeof seg);
	seg.attr = SG_PHYSICAL | SG_DEVICE | SG_NOEXEC;
	seg.name = name;
	seg.pa = (uintptr)pa;
	seg.size = size;
	addphysseg(&seg);
}
