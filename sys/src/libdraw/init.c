#include <u.h>
#include <libc.h>
#include <draw.h>

Display	*display;
Font	*font;
Image	*screen;

static char deffontname[] = "*default*";
Screen	*_screen;

static void _closedisplay(Display*, int);

/* note handler */
static void
drawshutdown(void)
{
	Display *d;

	d = display;
	if(d != nil){
		display = nil;
		_closedisplay(d, 1);
	}
}

int
geninitdraw(char *devdir, void(*error)(Display*, char*), char *fontname, char *label, char *windir, int ref)
{
	int fd, n;
	Subfont *df;
	char buf[128];

	display = initdisplay(devdir, windir, error);
	if(display == nil)
		return -1;

	/*
	 * Set up default font
	 */
	df = getdefont(display);
	display->defaultsubfont = df;
	if(df == nil){
    Error:
		closedisplay(display);
		display = nil;
		return -1;
	}
	if(fontname == nil){
		fd = open("/env/font", OREAD|OCEXEC);
		if(fd >= 0){
			n = read(fd, buf, sizeof(buf));
			if(n>0 && n<sizeof buf-1){
				buf[n] = 0;
				fontname = buf;
			}
			close(fd);
		}
	}
	/*
	 * Build fonts with caches==depth of screen, for speed.
	 * If conversion were faster, we'd use 0 and save memory.
	 */
	if(fontname == nil){
		snprint(buf, sizeof buf, "%d %d\n0 %d\t%s\n", df->height, df->ascent,
			df->n-1, deffontname);
//BUG: Need something better for this	installsubfont("*default*", df);
		font = buildfont(display, buf, deffontname);
		if(font == nil)
			goto Error;
	}else{
		font = openfont(display, fontname);	/* BUG: grey fonts */
		if(font == nil)
			goto Error;
	}
	display->defaultfont = font;

	/*
	 * Write label; ignore errors (we might not be running under rio)
	 */
	if(label != nil){
		snprint(buf, sizeof buf, "%s/label", display->windir);
		fd = open(buf, OREAD|OCEXEC);
		if(fd >= 0){
			read(fd, display->oldlabel, (sizeof display->oldlabel)-1);
			close(fd);
			fd = create(buf, OWRITE|OCEXEC, 0666);
			if(fd >= 0){
				write(fd, label, strlen(label));
				close(fd);
			}
		}
	}

	snprint(buf, sizeof buf, "%s/winname", display->windir);
	if(gengetwindow(display, buf, &screen, &_screen, ref) < 0)
		goto Error;

	atexit(drawshutdown);

	return 1;
}

int
initdraw(void(*error)(Display*, char*), char *fontname, char *label)
{
	static char dev[] = "/dev";

	return geninitdraw(dev, error, fontname, label, dev, Refnone);
}

/*
 * Attach, or possibly reattach, to window.
 * If reattaching, maintain value of screen pointer.
 */
int
gengetwindow(Display *d, char *winname, Image **winp, Screen **scrp, int ref)
{
	int n, fd;
	char buf[64+1], obuf[64+1];
	Image *image;
	Rectangle r;

	obuf[0] = 0;
retry:
	fd = open(winname, OREAD|OCEXEC);
	if(fd<0 || (n=read(fd, buf, sizeof buf-1))<=0){
		if(fd >= 0) close(fd);
		strcpy(buf, "noborder");
		image = d->image;
	}else{
		close(fd);
		buf[n] = '\0';
		image = namedimage(d, buf);
		if(image == nil){
			/*
			 * theres a race where the winname can change after
			 * we read it, so keep trying as long as the name
			 * keeps changing.
			 */
			if(strcmp(buf, obuf) != 0){
				strcpy(obuf, buf);
				goto retry;
			}
		}
	}
	if(*winp != nil){
		_freeimage1(*winp);
		if((*scrp)->image != d->image)
			freeimage((*scrp)->image);
		freescreen(*scrp);
		*scrp = nil;
	}
	if(image == nil){
		*winp = nil;
		d->screenimage = nil;
		return -1;
	}
	d->screenimage = image;
	*scrp = allocscreen(image, d->white, 0);
	if(*scrp == nil){
		*winp = nil;
		d->screenimage = nil;
		if(image != d->image)
			freeimage(image);
		return -1;
	}
	r = image->r;
	if(strncmp(buf, "noborder", 8) != 0)
		r = insetrect(r, Borderwidth);
	*winp = _allocwindow(*winp, *scrp, r, ref, DWhite);
	if(*winp == nil){
		freescreen(*scrp);
		*scrp = nil;
		d->screenimage = nil;
		if(image != d->image)
			freeimage(image);
		return -1;
	}
	d->screenimage = *winp;
	return 1;
}

int
getwindow(Display *d, int ref)
{
	char winname[128];

	snprint(winname, sizeof winname, "%s/winname", d->windir);
	return gengetwindow(d, winname, &screen, &_screen, ref);
}

#define	NINFO	12*12

Display*
initdisplay(char *dev, char *win, void(*error)(Display*, char*))
{
	char buf[128], info[NINFO+1], *t;
	int n, datafd, ctlfd, reffd;
	Display *disp;
	Dir *dir;
	Image *image;

	fmtinstall('P', Pfmt);
	fmtinstall('R', Rfmt);
	if(dev == nil)
		dev = "/dev";
	if(win == nil)
		win = "/dev";
	if(strlen(dev)>sizeof buf-25 || strlen(win)>sizeof buf-25){
		werrstr("initdisplay: directory name too long");
		return nil;
	}
	t = strdup(win);
	if(t == nil)
		return nil;

	sprint(buf, "%s/draw/new", dev);
	ctlfd = open(buf, ORDWR|OCEXEC);
	if(ctlfd < 0){
    Error1:
		free(t);
		werrstr("initdisplay: %s: %r", buf);
		return nil;
	}
	if((n=read(ctlfd, info, sizeof info)) < 12){
    Error2:
		close(ctlfd);
		goto Error1;
	}
	if(n==NINFO+1)
		n = NINFO;
	info[n] = '\0';
	sprint(buf, "%s/draw/%d/data", dev, atoi(info+0*12));
	datafd = open(buf, ORDWR|OCEXEC);
	if(datafd < 0)
		goto Error2;
	sprint(buf, "%s/draw/%d/refresh", dev, atoi(info+0*12));
	reffd = open(buf, OREAD|OCEXEC);
	if(reffd < 0){
    Error3:
		close(datafd);
		goto Error2;
	}
	disp = mallocz(sizeof(Display), 1);
	if(disp == nil){
    Error4:
		close(reffd);
		goto Error3;
	}
	image = nil;
	if(0){
    Error5:
		free(image);
		free(disp);
		goto Error4;
	}
	if(n >= NINFO){
		image = mallocz(sizeof(Image), 1);
		if(image == nil)
			goto Error5;
		image->display = disp;
		image->id = 0;
		image->chan = strtochan(info+2*12);
		image->depth = chantodepth(image->chan);
		image->repl = atoi(info+3*12);
		image->r.min.x = atoi(info+4*12);
		image->r.min.y = atoi(info+5*12);
		image->r.max.x = atoi(info+6*12);
		image->r.max.y = atoi(info+7*12);
		image->clipr.min.x = atoi(info+8*12);
		image->clipr.min.y = atoi(info+9*12);
		image->clipr.max.x = atoi(info+10*12);
		image->clipr.max.y = atoi(info+11*12);
	}

	disp->bufsize = iounit(datafd);
	if(disp->bufsize <= 0)
		disp->bufsize = 8000;
	if(disp->bufsize < 512){
		werrstr("iounit %d too small", disp->bufsize);
		goto Error5;
	}
	disp->buf = malloc(disp->bufsize+1);	/* +1 for flush message */
	if(disp->buf == nil)
		goto Error5;

	disp->image = image;
	disp->dirno = atoi(info+0*12);
	disp->fd = datafd;
	disp->ctlfd = ctlfd;
	disp->reffd = reffd;
	disp->bufp = disp->buf;
	disp->error = error;
	disp->windir = t;
	disp->devdir = strdup(dev);
	wlock(&disp->usrlock);
	disp->white = allocimage(disp, Rect(0, 0, 1, 1), GREY1, 1, DWhite);
	disp->black = allocimage(disp, Rect(0, 0, 1, 1), GREY1, 1, DBlack);
	if(disp->white == nil || disp->black == nil){
		free(disp->devdir);
		free(disp->white);
		free(disp->black);
		goto Error5;
	}
	disp->opaque = disp->white;
	disp->transparent = disp->black;
	dir = dirfstat(ctlfd);
	if(dir!=nil && dir->type=='i'){
		disp->local = 1;
		disp->dataqid = dir->qid.path;
	}
	free(dir);

	return disp;
}

/*
 * Note that disp->defaultfont and defaultsubfont are not freed here.
 */
void
closedisplay(Display *disp)
{
	_closedisplay(disp, 0);
}

static void
_closedisplay(Display *disp, int isshutdown)
{
	int fd;
	char buf[128];

	if(disp == nil)
		return;
	if(disp == display)
		display = nil;
	if(disp->oldlabel[0]){
		snprint(buf, sizeof buf, "%s/label", disp->windir);
		fd = open(buf, OWRITE|OCEXEC);
		if(fd >= 0){
			write(fd, disp->oldlabel, strlen(disp->oldlabel));
			close(fd);
		}
	}

	/*
	 * if we're shutting down, don't free all the resources.
	 * if other procs are getting shot down by notes too,
	 * one might get shot down while holding the malloc lock.
	 * just let the kernel clean things up when we exit.
	 */
	if(isshutdown)
		return;

	free(disp->devdir);
	free(disp->windir);
	freeimage(disp->white);
	freeimage(disp->black);
	close(disp->fd);
	close(disp->ctlfd);
	/* should cause refresh slave to shut down */
	close(disp->reffd);
	free(disp);
}

void
lockdisplay(Display *disp)
{
	wlock(&disp->usrlock);
}

void
unlockdisplay(Display *disp)
{
	wunlock(&disp->usrlock);
}

void
rlockdisplay(Display *disp)
{
	rlock(&disp->usrlock);
}

void
runlockdisplay(Display *disp)
{
	runlock(&disp->usrlock);
}

void
_lockdisplay(Display *disp)
{
	qlock(&disp->qlock);
}

void
_unlockdisplay(Display *disp)
{
	qunlock(&disp->qlock);
}

void
drawerror(Display *d, char *s)
{
	char err[ERRMAX];

	if(d != nil && d->error != nil)
		(*d->error)(d, s);
	else{
		errstr(err, sizeof err);
		fprint(2, "draw: %s: %s\n", s, err);
		exits(s);
	}
}

static
int
doflush(Display *d)
{
	int n;

	n = d->bufp-d->buf;
	if(n <= 0)
		return 1;

	if(write(d->fd, d->buf, n) != n){
		d->bufp = d->buf;	/* might as well; chance of continuing */
		return -1;
	}
	d->bufp = d->buf;
	return 1;
}

int
flushimage(Display *d, int visible)
{
	int rc;

	if(d == nil)
		return 0;
	_lockdisplay(d);
	if(visible)
		*d->bufp++ = 'v';	/* one byte always reserved for this */
	rc = doflush(d);
	_unlockdisplay(d);
	return rc;
}

static uchar*
growcmdbuf(Display *d, uchar *e, int extra)
{
	uchar *b;

	if(e + extra <= d->buf + d->bufsize)
		return e;
	b = d->bufp;
	if(b == d->buf || (e - b) + extra > d->bufsize){
		werrstr("message exceeds display buffer capacity");
		return nil;
	}
	if(doflush(d) < 0){
		werrstr("could not flush display buffer: %r");
		return nil;
	}
	memmove(d->buf, b, e - b);
	return d->buf + (e - b);
}

static int
vdrawcmd(Display *d, char *fmt, va_list va)
{
	Rectangle *r;
	Point *p;
	Warp *w;
	uchar *a, *v;
	ushort s;
	ulong l;

	assert(!canqlock(&d->qlock));	/* display must be locked */

	a = d->bufp;
	while(*fmt != 0)
		switch(*fmt++){
		case 'b':
			if((a = growcmdbuf(d, a, 1)) == nil)
				return -1;
			*a++ = va_arg(va, uchar);
			break;
		case 's':
			if((a = growcmdbuf(d, a, 2)) == nil)
				return -1;
			s = va_arg(va, ushort);
			BPSHORT(a, s);
			a += 2;
			break;
		case 'l':
			if((a = growcmdbuf(d, a, 4)) == nil)
				return -1;
			l = va_arg(va, ulong);
			BPLONG(a, l);
			a += 4;
			break;
		case 'P':
			if((a = growcmdbuf(d, a, 2*4)) == nil)
				return -1;
			p = va_arg(va, Point*);
			BPLONG(a,   p->x);
			BPLONG(a+4, p->y);
			a += 2*4;
			break;
		case 'R':
			if((a = growcmdbuf(d, a, 4*4)) == nil)
				return -1;
			r = va_arg(va, Rectangle*);
			BPLONG(a,   r->min.x); BPLONG(a+4,  r->min.y);
			BPLONG(a+8, r->max.x); BPLONG(a+12, r->max.y);
			a += 4*4;
			break;
		case 'M':
			if((a = growcmdbuf(d, a, 3*3*4)) == nil)
				return -1;
			w = va_arg(va, Warp*);
			BPLONG(a,    w->m[0][0]); BPLONG(a+4,  w->m[0][1]); BPLONG(a+8,  w->m[0][2]);
			BPLONG(a+12, w->m[1][0]); BPLONG(a+16, w->m[1][1]); BPLONG(a+20, w->m[1][2]);
			BPLONG(a+24, w->m[2][0]); BPLONG(a+28, w->m[2][1]); BPLONG(a+32, w->m[2][2]);
			a += 3*3*4;
			break;
		case 'z':
			l = va_arg(va, uchar);
			if((a = growcmdbuf(d, a, 1+l)) == nil)
				return -1;
			v = va_arg(va, void*);
			*a++ = l;
			memmove(a, v, l);
			a += l;
			break;
		case '<':
			l = va_arg(va, ulong);
			if((a = growcmdbuf(d, a, l)) == nil)
				return -1;
			v = va_arg(va, void*);
			memmove(a, v, l);
			a += l;
			break;
		case 'O':
			l = va_arg(va, uchar);
			if(l == SoverD)
				break;
			if((a = growcmdbuf(d, a, 1+1)) == nil)
				return -1;
			*a++ = 'O';
			*a++ = l;
			break;
		default:
			werrstr("unknown draw cmd field format specifier");
			return -1;
		}

	d->bufp = a;	/* commit */
	return 0;
}

int
drawcmd(Display *d, char *fmt, ...)
{
	va_list va;
	int rc;

	va_start(va, fmt);
	rc = vdrawcmd(d, fmt, va);
	va_end(va);
	return rc;
}
