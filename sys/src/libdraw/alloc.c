#include <u.h>
#include <libc.h>
#include <draw.h>

Image*
allocimage(Display *d, Rectangle r, ulong chan, int repl, ulong col)
{
	Image *i;

	i = _allocimage(nil, d, r, chan, repl, col, 0, 0);
	if(i != nil)
		setmalloctag(i, getcallerpc(&d));
	return i;
}

Image*
_allocimage(Image *ai, Display *d, Rectangle r, ulong chan, int repl, ulong col, int screenid, int refresh)
{
	Image *i;
	Rectangle clipr;
	int id;
	int depth;

	if(badrect(r)){
		werrstr("bad rectangle");
    Error:
		werrstr("allocimage: %r");
		return nil;
	}
	if(chan == 0 || (depth = chantodepth(chan)) == 0){
		werrstr("bad channel descriptor");
		goto Error;
	}

	if(repl)
		/* huge but not infinite, so various offsets will leave it huge, not overflow */
		clipr = Rect(-0x3FFFFFFF, -0x3FFFFFFF, 0x3FFFFFFF, 0x3FFFFFFF);
	else
		clipr = r;

	_lockdisplay(d);
	id = ++d->imageid;
	if(drawcmd(d, "bllblbRRl", 'b', id, screenid, refresh, chan, repl, &r, &clipr, col) < 0){
		_unlockdisplay(d);
		goto Error;
	}
	_unlockdisplay(d);

	if(ai != nil)
		i = ai;
	else{
		i = malloc(sizeof(Image));
		if(i == nil){
			_lockdisplay(d);
			if(drawcmd(d, "bl", 'f', id) == 0)
				_flushimage(d);
			_unlockdisplay(d);
			goto Error;
		}
	}
	i->display = d;
	i->id = id;
	i->depth = depth;
	i->chan = chan;
	i->r = r;
	i->clipr = clipr;
	i->repl = repl;
	i->screen = nil;
	i->next = nil;
	return i;
}

Image*
namedimage(Display *d, char *name)
{
	char buf[12*12+1];
	Image *i;
	int id, n;
	ulong chan;

	if(name == nil || name[0] == '\0'){
		werrstr("empty name");
    Error1:
		werrstr("namedimage: %r");
		return nil;
	}

	n = strlen(name);
	if(n > 255){
		werrstr("name too long");
		goto Error1;
	}

	_lockdisplay(d);
	/* flush pending data so we don't get error allocating the image */
	_flushimage(d);

	id = ++d->imageid;
	if(drawcmd(d, "blz", 'n', id, n, name) < 0){
    Error2:
		_unlockdisplay(d);
		goto Error1;
	}
	if(_flushimage(d) < 0)
		goto Error2;

	if(pread(d->ctlfd, buf, sizeof buf, 0) < 12*12)
		goto Error2;
	buf[12*12] = '\0';
	_unlockdisplay(d);

	if((chan = strtochan(buf+2*12)) == 0){
		werrstr("bad channel '%.12s' from devdraw", buf+2*12);
		goto Error1;
	}
	i = mallocz(sizeof(Image), 1);
	if(i == nil){
		_lockdisplay(d);
		if(drawcmd(d, "bl", 'f', id) == 0)
			_flushimage(d);
		goto Error2;
	}
	i->display = d;
	i->id = id;
	i->chan = chan;
	i->depth = chantodepth(chan);
	i->repl = atoi(buf+3*12);
	i->r.min.x = atoi(buf+4*12);
	i->r.min.y = atoi(buf+5*12);
	i->r.max.x = atoi(buf+6*12);
	i->r.max.y = atoi(buf+7*12);
	i->clipr.min.x = atoi(buf+8*12);
	i->clipr.min.y = atoi(buf+9*12);
	i->clipr.max.x = atoi(buf+10*12);
	i->clipr.max.y = atoi(buf+11*12);
	i->screen = nil;
	i->next = nil;
	return i;
}

int
nameimage(Image *i, char *name, int in)
{
	int n;

	if(name == nil || name[0] == '\0'){
		werrstr("empty name");
    Error:
		werrstr("nameimage: %r");
		return 0;
	}

	n = strlen(name);
	if(n > 255){
		werrstr("name too long");
		goto Error;
	}

	_lockdisplay(i->display);
	if(drawcmd(i->display, "blbz", 'N', i->id, in, n, name) < 0){
		_unlockdisplay(i->display);
		goto Error;
	}
	if(_flushimage(i->display) < 0){
		_unlockdisplay(i->display);
		/*
		 * XXX rio(1) used to depend on the errstr returned here to
		 * correspond with the error message received from draw(3),
		 * without any tracing prefixes.  this has been fixed on
		 * rio's side, but let's try not to break compatibility with
		 * rio derivatives that expect the same behavior.
		 */
		return 0;
	}
	_unlockdisplay(i->display);
	return 1;
}

int
_freeimage1(Image *i)
{
	Display *d;
	Image **w;

	if(i == nil || i->display == nil)
		return 0;

	d = i->display;
	_lockdisplay(d);
	if(drawcmd(d, "bl", 'f', i->id) < 0){
		_unlockdisplay(d);
		return -1;
	}

	if(i->screen != nil){
		w = &d->windows;
		while(*w != nil){
			if(*w == i){
				*w = i->next;
				break;
			}
			w = &(*w)->next;
		}
	}
	_unlockdisplay(d);
	return 0;
}

int
freeimage(Image *i)
{
	int ret;

	ret = _freeimage1(i);
	free(i);
	return ret;
}
