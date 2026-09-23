#include <u.h>
#include <libc.h>
#include <draw.h>

static int	screenid;

Screen*
allocscreen(Image *image, Image *fill, int public)
{
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
	_lockdisplay(d);
	if(!screenid)
		screenid = getpid();
	_unlockdisplay(d);
	for(try=0; try<25; try++){
		/* loop until we find a free id */
		_lockdisplay(d);
		id = ++screenid;
		if(drawcmd(d, "blllb", 'A', id, image->id, fill->id, public) < 0){
			_unlockdisplay(d);
			break;
		}
		if(_flushimage(d) != -1){
			_unlockdisplay(d);
			goto Found;
		}
		_unlockdisplay(d);
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
	Screen *s;

	s = malloc(sizeof(Screen));
	if(s == nil)
		return nil;
	_lockdisplay(d);
	if(drawcmd(d, "bll", 'S', id, chan) < 0){
    Error:
		_unlockdisplay(d);
		free(s);
		return nil;
	}
	if(_flushimage(d) < 0)
		goto Error;
	_unlockdisplay(d);

	s->display = d;
	s->id = id;
	s->image = nil;
	s->fill = nil;
	return s;
}

int
freescreen(Screen *s)
{
	Display *d;

	if(s == nil)
		return 0;
	d = s->display;
	_lockdisplay(d);
	if(drawcmd(d, "bl", 'F', s->id) < 0){
		_unlockdisplay(d);
		free(s);
		return -1;
	}
	_unlockdisplay(d);
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
	_lockdisplay(d);
	i->next = d->windows;
	d->windows = i;
	_unlockdisplay(d);
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

	b = malloc(4*n);
	if(b == nil){
		fprint(2, "top/bottom: malloc: %r\n");
		return;
	}
	for(i=0; i<n; i++)
		BPLONG(b+4*i, w[i]->id);

	_lockdisplay(d);
	drawcmd(d, "bbs<", 't', top, n, 4*n, b);
	_unlockdisplay(d);
	free(b);
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
	Point delta;

	_lockdisplay(w->display);
	if(drawcmd(w->display, "blPP", 'o', w->id, &log, &scr) < 0){
		_unlockdisplay(w->display);
		return 0;
	}
	_unlockdisplay(w->display);
	delta = subpt(log, w->r.min);
	w->r = rectaddpt(w->r, delta);
	w->clipr = rectaddpt(w->clipr, delta);
	return 1;
}
