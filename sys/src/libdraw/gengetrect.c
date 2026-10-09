#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>

#define	W	Borderwidth

static Image *bordcol;

static Cursor sweep={
	{-7, -7},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x07,
	 0xE0, 0x07, 0xE0, 0x07, 0xE3, 0xF7, 0xE3, 0xF7,
	 0xE3, 0xE7, 0xE3, 0xF7, 0xE3, 0xFF, 0xE3, 0x7F,
	 0xE0, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,},
	{0x00, 0x00, 0x7F, 0xFE, 0x40, 0x02, 0x40, 0x02,
	 0x40, 0x02, 0x40, 0x02, 0x40, 0x02, 0x41, 0xE2,
	 0x41, 0xC2, 0x41, 0xE2, 0x41, 0x72, 0x40, 0x38,
	 0x40, 0x1C, 0x40, 0x0E, 0x7F, 0xE6, 0x00, 0x00,}
};

static int
max(int a, int b)
{
	return a > b? a : b;
}

static int
initcolors(void)
{
	static QLock lk;

	qlock(&lk);
	if(bordcol == nil){
		bordcol = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DRed);
		if(bordcol == nil){
			qunlock(&lk);
			drawerror(display, "getrect: allocimage failed");
			return 0;
		}
	}
	qunlock(&lk);
	return 1;
}

static void
freetmp(Image *t[4])
{
	freeimage(t[0]);
	freeimage(t[1]);
	freeimage(t[2]);
	freeimage(t[3]);
	t[0] = t[1] = t[2] = t[3] = nil;
}

static void
brects(Rectangle r, Rectangle rp[4])
{
	if(Dx(r) < 2*W)
		r.max.x = r.min.x+2*W;
	if(Dy(r) < 2*W)
		r.max.y = r.min.y+2*W;
	rp[0] = Rect(r.min.x, r.min.y, r.max.x, r.min.y+W);
	rp[1] = Rect(r.min.x, r.max.y-W, r.max.x, r.max.y);
	rp[2] = Rect(r.min.x, r.min.y+W, r.min.x+W, r.max.y-W);
	rp[3] = Rect(r.max.x-W, r.min.y+W, r.max.x, r.max.y-W);
}

static void
drawbrects(Image *b[4], Rectangle or, Rectangle nr)
{
	Rectangle r, rects[4];
	int i;

	brects(or, rects);
	if(b[0] != nil){
		for(i = 0; i < 4; i++)
			draw(screen, rects[i], b[i], nil, ZP);
		if(Dx(b[0]->r) < Dx(nr) || Dy(b[2]->r) < Dy(nr))
			freetmp(b);
	}
	if(b[0] == nil){
		r = Rect(0, 0, max(Dx(display->screenimage->r), Dx(nr)), W);
		b[0] = allocimage(display, r, screen->chan, 0, DNofill);
		b[1] = allocimage(display, r, screen->chan, 0, DNofill);
		r = Rect(0, 0, W, max(Dy(display->screenimage->r), Dy(nr)));
		b[2] = allocimage(display, r, screen->chan, 0, DNofill);
		b[3] = allocimage(display, r, screen->chan, 0, DNofill);
		if(b[0] == nil || b[1] == nil || b[2] == nil || b[3] == nil){
			freetmp(b);
			drawerror(display, "getrect: allocimage failed");
			return;
		}
	}
	brects(nr, rects);
	for(i=0; i<4; i++){
		draw(b[i], rectsubpt(rects[i], rects[i].min), screen, nil, rects[i].min);
		draw(screen, rects[i], bordcol, nil, ZP);
	}
}

static void
drawedge(Screen *scr, Image **bp, Rectangle r)
{
	Image *b = *bp;

	freeimage(b);
	b = allocwindow(scr, r, Refbackup, DNofill);
	if(b != nil)
		draw(b, r, bordcol, nil, ZP);
	*bp = b;
}

static void
drawborder(Screen *scr, Image *b[4], Rectangle r)
{
	Rectangle rects[4];

	brects(r, rects);
	drawedge(scr, &b[0], rects[0]);
	drawedge(scr, &b[1], rects[1]);
	drawedge(scr, &b[2], rects[2]);
	drawedge(scr, &b[3], rects[3]);
}

Rectangle
gengetrect(int but, Mouse *m, void (*_setcursor)(Mouse*, Cursor*), void (*_readmouse)(Mouse*), Screen *scr)
{
	Image *b[4];
	Rectangle r, cr, or;

	memset(b, 0, sizeof(b));
	but = 1<<(but-1);

	(*_setcursor)(m, &sweep);
	while(m->buttons)
		(*_readmouse)(m);
	while(!(m->buttons & but)){
		(*_readmouse)(m);
		if(m->buttons & (7^but))
			goto Return;
	}

	if(!initcolors())
		goto Return;
	r.min = m->xy;
	r.max = m->xy;
	do{
		cr = canonrect(r);
		if(scr != nil)
			drawborder(scr, b, cr);
		else{
			drawbrects(b, or, cr);
			or = cr;
		}
		(*_readmouse)(m);
		r.max = m->xy;
	}while(m->buttons == but);
	if(scr == nil)
		drawbrects(b, or, ZR);
	freetmp(b);

    Return:
	(*_setcursor)(m, nil);
	if(m->buttons & (7^but)){
		cr.min.x = cr.max.x = 0;
		cr.min.y = cr.max.y = 0;
		while(m->buttons)
			(*_readmouse)(m);
	}
	return cr;
}

void
drawgetrect(Rectangle cr, int up)
{
	static Image *tmp[4];

	if(!initcolors())
		return;
	if(up)
		drawbrects(tmp, ZR, cr);
	else
		drawbrects(tmp, cr, ZR);
}
