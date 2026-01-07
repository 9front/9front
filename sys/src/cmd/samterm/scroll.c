#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include "flayer.h"
#include "samterm.h"

static Image *scrtmp;

void
scrtemps(void)
{
	int h;

	if(scrtmp)
		return;
	if(screensize(0, &h) == 0)
		h = 2048;
	scrtmp = allocimage(display, Rect(0, 0, 32, h), screen->chan, 0, 0);
	if(scrtmp==0)
		panic("scrtemps");
}

Rectangle
scrpos(Rectangle r, long p0, long p1, long tot)
{
	Rectangle q;
	int h;

	q = r;
	h = q.max.y-q.min.y;
	if(tot == 0)
		return q;
	if(tot > 1024L*1024L)
		tot>>=10, p0>>=10, p1>>=10;
	if(p0 > 0)
		q.min.y += h*p0/tot;
	if(p1 < tot)
		q.max.y -= h*(tot-p1)/tot;
	if(q.max.y < q.min.y+2){
		if(q.min.y+2 <= r.max.y)
			q.max.y = q.min.y+2;
		else
			q.min.y = q.max.y-2;
	}
	return q;
}

void
scrdraw(Flayer *l, long tot)
{
	Rectangle r, r1, r2;
	Image *b;

	scrtemps();
	if(l->f.b == 0)
		panic("scrdraw");
	r = l->scroll;
	r1 = r;
	if(l->visible == All){
		b = scrtmp;
		r1.min.x = 0;
		r1.max.x = Dx(r);
	}else
		b = l->f.b;
	r2 = scrpos(r1, l->origin, l->origin+l->f.nchars, tot);
	if(!eqrect(r2, l->lastsr)){
		l->lastsr = r2;
		draw(b, r1, l->f.cols[BORD], nil, ZP);
		draw(b, r2, l->f.cols[BACK], nil, r2.min);
		r2 = r1;
		r2.min.x = r2.max.x-1;
		draw(b, r2, l->f.cols[BORD], nil, ZP);
		if(b!=l->f.b)
			draw(l->f.b, r, b, nil, r1.min);
	}
}

void
scroll(Flayer *l, int but)
{
	Rectangle s;
	int my, n;
	long o, tot;
	int once;

	if(l->visible==None)
		return;

	once = 0;
	s = l->scroll;
	tot = scrtotal(l);
	do{
		my = mousep->xy.y;
		if(my < s.min.y)
			my = s.min.y;
		if(my > s.max.y)
			my = s.max.y;
		my -= s.min.y;
		if(but == 2){
			o = (tot / (s.max.y - s.min.y)) * my;
			n = 0;
			forcenter(l, o, n);
			if(readmouse(mousectl) < 0)
				panic("mouse");
		}else{
			o = l->origin;
			n = my/l->f.font->height;
			if(n == 0)
				n++;
			if(but == 1 || but == 4)
				n = -n;
			forcenter(l, o, n);
			if(!once){
				flushdisplay();
				if(but == 4 || but == 5)
					return;
				once++;
				sleep(175);
			}
			sleep(25);
			if(nbrecv(mousectl->c, mousectl) < 0)
				panic("mouse");
		}
	}while(mousectl->buttons & (1 << (but-1)));
}
