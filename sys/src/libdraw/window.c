#include <u.h>
#include <libc.h>
#include <draw.h>

typedef struct Memimage Memimage;

static int	screenid;

Screen*
allocscreen(Image *image, Image *fill, int public)
{
	uchar *a;
	Screen *s;
	int id, try;
	Display *d;

	d = image->display;
	if(d != fill->display){
		werrstr("allocscreen: image and fill on different displays");
		return nil;
	}
	s = malloc(sizeof(Screen));
	if(s == nil)
		return nil;
	if(!screenid)
		screenid = getpid();
	for(try=0; try<25; try++){
		/* loop until find a free id */
		a = bufimage(d, 1+4+4+4+1);
		if(a == nil)
			break;
		id = ++screenid & 0xffff;	/* old devdraw bug */
		a[0] = 'A';
		BPLONG(a+1, id);
		BPLONG(a+5, image->id);
		BPLONG(a+9, fill->id);
		a[13] = public;
		if(flushimage(d, 0) != -1)
			goto Found;
	}
	free(s);
	return nil;

    Found:
	s->display = d;
	s->id = id;
	s->image = image;
	assert(s->image != nil && s->image->chan != 0);

	s->fill = fill;
	return s;
}

Screen*
publicscreen(Display *d, int id, ulong chan)
{
	uchar *a;
	Screen *s;

	s = malloc(sizeof(Screen));
	if(s == nil)
		return nil;
	a = bufimage(d, 1+4+4);
	if(a == nil){
Error:
		free(s);
		return nil;
	}
	a[0] = 'S';
	BPLONG(a+1, id);
	BPLONG(a+5, chan);
	if(flushimage(d, 0) < 0)
		goto Error;

	s->display = d;
	s->id = id;
	s->image = nil;
	s->fill = nil;
	return s;
}

int
freescreen(Screen *s)
{
	uchar *a;
	Display *d;

	if(s == nil)
		return 0;
	d = s->display;
	a = bufimage(d, 1+4);
	if(a == nil){
		free(s);
		return -1;
	}
	a[0] = 'F';
	BPLONG(a+1, s->id);
	free(s);
	return 1;
}

Image*
allocwindow(Screen *s, Rectangle r, int ref, ulong col)
{
	return _allocwindow(nil, s, r, ref, col);
}

Image*
_allocwindow(Image *i, Screen *s, Rectangle r, int ref, ulong col)
{
	Display *d;

	d = s->display;
	i = _allocimage(i, d, r, d->screenimage->chan, 0, col, s->id, ref);
	if(i == nil)
		return nil;
	i->screen = s;
	i->next = s->display->windows;
	s->display->windows = i;
	return i;
}

static
void
topbottom(Image **w, int n, int top)
{
	int i;
	uchar *b;
	Display *d;

	if(n < 0){
    Ridiculous:
		fprint(2, "top/bottom: ridiculous number of windows\n");
		return;
	}
	if(n == 0)
		return;
	if(n > (w[0]->display->bufsize-100)/4)
		goto Ridiculous;
	/*
	 * this used to check that all images were on the same screen.
	 * we don't know the screen associated with images we acquired
	 * by name.  instead, check that all images are on the same display.
	 * the display will check that they are all on the same screen.
	 */
	d = w[0]->display;
	for(i=1; i<n; i++)
		if(w[i]->display != d){
			fprint(2, "top/bottom: windows not on same screen\n");
			return;
		}

	if(n==0)
		return;
	b = bufimage(d, 1+1+2+4*n);
	if(b == nil)
		return;
	b[0] = 't';
	b[1] = top;
	BPSHORT(b+2, n);
	for(i=0; i<n; i++)
		BPLONG(b+4+4*i, w[i]->id);
}

void
bottomwindow(Image *w)
{
	if(w->screen != nil)
		topbottom(&w, 1, 0);
}

void
topwindow(Image *w)
{
	if(w->screen != nil)
		topbottom(&w, 1, 1);
}

void
bottomnwindows(Image **w, int n)
{
	topbottom(w, n, 0);
}

void
topnwindows(Image **w, int n)
{
	topbottom(w, n, 1);
}

int
originwindow(Image *w, Point log, Point scr)
{
	uchar *b;
	Point delta;

	b = bufimage(w->display, 1+4+2*4+2*4);
	if(b == nil)
		return 0;
	b[0] = 'o';
	BPLONG(b+1, w->id);
	BPLONG(b+5, log.x);
	BPLONG(b+9, log.y);
	BPLONG(b+13, scr.x);
	BPLONG(b+17, scr.y);
	delta = subpt(log, w->r.min);
	w->r = rectaddpt(w->r, delta);
	w->clipr = rectaddpt(w->clipr, delta);
	return 1;
}
