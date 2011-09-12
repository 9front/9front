#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <cursor.h>
#include <keyboard.h>
#include <plumb.h>

typedef struct Page Page;
struct Page {
	char	*label;

	QLock;
	void	*data;
	int	(*open)(Page *);

	char	*text;
	Image	*image;
	int	fd;
	int	gen;

	Page	*up;
	Page	*next;
	Page	*down;
	Page	*tail;
};

int zoom = 1;
int ppi = 100;
int imode;
int newwin;
int rotate;
int viewgen;
int pagegen;
Point resize, pos;
Page *root, *current;
QLock pagelock;
int nullfd;

char pagespool[] = "/tmp/pagespool.";

enum {
	NPROC = 4,
	NAHEAD = 2,
	NBUF = 8*1024,
	NPATH = 1024,
};

char *pagemenugen(int i);

char *menuitems[] = {
	"orig size",
	"rotate 90",
	"upside down",
	"",
	"fit width",
	"fit height",
	"",
	"zoom in",
	"zoom out",
	"",
	"next",
	"prev",
	"zerox",
	"",
	"quit",
	nil
};

Menu pagemenu = {
	nil,
	pagemenugen,
	-1,
};

Menu menu = {
	menuitems,
	nil,
	-1,
};

Cursor reading = {
	{-1, -1},
	{0xff, 0x80, 0xff, 0x80, 0xff, 0x00, 0xfe, 0x00, 
	 0xff, 0x00, 0xff, 0x80, 0xff, 0xc0, 0xef, 0xe0, 
	 0xc7, 0xf0, 0x03, 0xf0, 0x01, 0xe0, 0x00, 0xc0, 
	 0x03, 0xff, 0x03, 0xff, 0x03, 0xff, 0x03, 0xff, },
	{0x00, 0x00, 0x7f, 0x00, 0x7e, 0x00, 0x7c, 0x00, 
	 0x7e, 0x00, 0x7f, 0x00, 0x6f, 0x80, 0x47, 0xc0, 
	 0x03, 0xe0, 0x01, 0xf0, 0x00, 0xe0, 0x00, 0x40, 
	 0x00, 0x00, 0x01, 0xb6, 0x01, 0xb6, 0x00, 0x00, }
};

void showpage(Page *);
void drawpage(Page *);
Point pagesize(Page *);

Page*
addpage(Page *up, char *label, int (*popen)(Page *), void *pdata, int fd)
{
	Page *p;

	p = mallocz(sizeof(*p), 1);
	p->label = strdup(label);
	p->gen = pagegen;
	p->text = nil;
	p->image = nil;
	p->data = pdata;
	p->open = popen;
	p->fd = fd;

	p->down = nil;
	p->tail = nil;
	p->next = nil;

	qlock(&pagelock);
	if(p->up = up){
		if(up->tail == nil)
			up->down = up->tail = p;
		else {
			up->tail->next = p;
			up->tail = p;
		}
	}
	qunlock(&pagelock);

	if(up && current == up)
		showpage(p);
	return p;
}

void
resizewin(Point size)
{
	int wctl;

	if((wctl = open("/dev/wctl", OWRITE)) < 0)
		return;
	/* add rio border */
	size = addpt(size, Pt(Borderwidth*2, Borderwidth*2));
	fprint(wctl, "resize -dx %d -dy %d\n", size.x, size.y);
	close(wctl);
}

int
createtmp(ulong id, char *pfx)
{
	char nam[64];

	snprint(nam, sizeof nam, "%s%s%.12d%.8lux", pagespool, pfx, getpid(), id ^ 0xcafebabe);
	return create(nam, OEXCL|ORCLOSE|ORDWR, 0600);
}

void
pipeline(int fd, char *fmt, ...)
{
	char buf[128], *argv[4];
	va_list arg;
	int pfd[2];

	if(pipe(pfd) < 0){
	Err:
		dup(nullfd, fd);
		return;
	}
	switch(rfork(RFPROC|RFFDG|RFREND|RFNOWAIT)){
	case -1:
		close(pfd[0]);
		close(pfd[1]);
		goto Err;
	case 0:
		if(dup(fd, 0)<0)
			exits("dup");
		if(dup(pfd[1], 1)<0)
			exits("dup");
		close(fd);
		close(pfd[1]);
		close(pfd[0]);
		va_start(arg, fmt);
		vsnprint(buf, sizeof buf, fmt, arg);
		va_end(arg);
		argv[0] = "rc";
		argv[1] = "-c";
		argv[2] = buf;
		argv[3] = nil;
		exec("/bin/rc", argv);
		sysfatal("exec: %r");
	}
	close(pfd[1]);
	dup(pfd[0], fd);
	close(pfd[0]);
}

int
popenfile(Page*);

int
popenconv(Page *p)
{
	char nam[NPATH];
	int fd;

	if((fd = dup(p->fd, -1)) < 0){
		close(p->fd);
		p->fd = -1;
		return -1;
	}

	seek(fd, 0, 0);
	if(p->data)
		pipeline(fd, "%s", (char*)p->data);

	/*
	 * dont keep the file descriptor arround if it can simply
	 * be reopened.
	 */
	fd2path(p->fd, nam, sizeof(nam));
	if(strncmp(nam, pagespool, strlen(pagespool))){
		close(p->fd);
		p->fd = -1;
		p->data = strdup(nam);
		p->open = popenfile;
	}

	return fd;
}

typedef struct Ghost Ghost;
struct Ghost
{
	QLock;

	int	pin;
	int	pout;
	int	pdat;
};

int
popenpdf(Page *p)
{
	char buf[NBUF];
	int n, pfd[2];
	Ghost *gs;

	if(pipe(pfd) < 0)
		return -1;
	switch(rfork(RFFDG|RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		close(pfd[0]);
		close(pfd[1]);
		return -1;
	case 0:
		close(pfd[0]);
		gs = p->data;
		qlock(gs);
		fprint(gs->pin, "%s DoPDFPage\n"
			"(/fd/3) (w) file "
			"dup flushfile "
			"dup (THIS IS NOT AN INFERNO BITMAP\\n) writestring "
			"flushfile\n", p->label);
		while((n = read(gs->pdat, buf, sizeof buf)) > 0){
			if(memcmp(buf, "THIS IS NOT AN INFERNO BITMAP\n", 30) == 0)
				break;
			write(pfd[1], buf, n);
		}
		qunlock(gs);
		exits(nil);
	}
	close(pfd[1]);
	return pfd[0];
}

int
popengs(Page *p)
{
	int n, i, pdf, ifd, ofd, pin[2], pout[2], pdat[2];
	char buf[NBUF], nam[32], *argv[12];

	pdf = 0;
	ifd = p->fd;
	p->fd = -1;
	seek(ifd, 0, 0);
	if(read(ifd, buf, 5) != 5)
		goto Err0;
	seek(ifd, 0, 0);
	if(memcmp(buf, "%PDF-", 5) == 0)
		pdf = 1;
	p->text = strdup(p->label);
	if(pipe(pin) < 0){
	Err0:
		close(ifd);
		return -1;
	}
	if(pipe(pout) < 0){
	Err1:
		close(pin[0]);
		close(pin[1]);
		goto Err0;
	}
	if(pipe(pdat) < 0){
	Err2:
		close(pdat[0]);
		close(pdat[1]);
		goto Err1;
	}

	switch(rfork(RFREND|RFPROC|RFFDG|RFNOWAIT)){
	case -1:
		goto Err2;
	case 0:
		if(pdf){
			if(dup(pin[1], 0)<0)
				exits("dup");
			if(dup(pout[1], 1)<0)
				exits("dup");
		} else {
			if(dup(nullfd, 0)<0)
				exits("dup");
			if(dup(nullfd, 1)<0)
				exits("dup");
		}
		if(dup(nullfd, 2)<0)
			exits("dup");
		if(dup(pdat[1], 3)<0)
			exits("dup");
		if(dup(ifd, 4)<0)
			exits("dup");

		close(pin[0]);
		close(pin[1]);
		close(pout[0]);
		close(pout[1]);
		close(pdat[0]);
		close(pdat[1]);
		close(ifd);

		if(p->data)
			pipeline(4, "%s", (char*)p->data);

		argv[0] = "gs";
		argv[1] = "-q";
		argv[2] = "-sDEVICE=plan9";
		argv[3] = "-sOutputFile=/fd/3";
		argv[4] = "-dBATCH";
		argv[5] = pdf ? "-dDELAYSAFER" : "-dSAFER";
		argv[6] = "-dQUIET";
		argv[7] = "-dTextAlphaBits=4";
		argv[8] = "-dGraphicsAlphaBits=4";
		snprint(buf, sizeof buf, "-r%d", ppi);
		argv[9] = buf;
		argv[10] = pdf ? "-" : "/fd/4";
		argv[11] = nil;
		exec("/bin/gs", argv);
		sysfatal("exec: %r");
	}

	close(pin[1]);
	close(pout[1]);
	close(pdat[1]);
	close(ifd);

	if(pdf){
		Ghost *gs;
		char *prolog =
			"/PAGEOUT (/fd/1) (w) file def\n"
			"/PAGE== { PAGEOUT exch write==only PAGEOUT (\\n) writestring PAGEOUT flushfile } def\n"
			"\n"
			"/Page null def\n"
			"/Page# 0 def\n"
			"/PDFSave null def\n"
			"/DSCPageCount 0 def\n"
			"/DoPDFPage {dup /Page# exch store pdfgetpage pdfshowpage } def\n"
			"\n"
			"GS_PDF_ProcSet begin\n"
			"pdfdict begin\n"
			"(/fd/4) (r) file { DELAYSAFER { .setsafe } if } stopped pop pdfopen begin\n"
			"\n"
			"pdfpagecount PAGE==\n";

		n = strlen(prolog);
		if(write(pin[0], prolog, n) != n)
			goto Out;
		if((n = read(pout[0], buf, sizeof(buf)-1)) < 0)
			goto Out;
		buf[n] = 0;
		n = atoi(buf);
		if(n <= 0){
			werrstr("no pages");
			goto Out;
		}
		gs = mallocz(sizeof(*gs), 1);
		gs->pin = pin[0];
		gs->pout = pout[0];
		gs->pdat = pdat[0];
		for(i=1; i<=n; i++){
			snprint(nam, sizeof nam, "%d", i);
			addpage(p, nam, popenpdf, gs, -1);
		}

		/* keep ghostscript arround */
		return -1;
	} else {
		i = 0;
		ofd = -1;
		while((n = read(pdat[0], buf, sizeof(buf))) >= 0){
			if(ofd >= 0 && (n <= 0 || memcmp(buf, "compressed\n", 11) == 0)){
				snprint(nam, sizeof nam, "%d", i);
				addpage(p, nam, popenconv, nil, ofd);
				ofd = -1;
			}
			if(n <= 0)
				break;
			if(ofd < 0){
				snprint(nam, sizeof nam, "%.4d", ++i);
				if((ofd = createtmp((ulong)p, nam)) < 0)
					ofd = dup(nullfd, -1);
			}
			write(ofd, buf, n);
		}
		if(ofd >= 0)
			close(ofd);
	}
Out:
	close(pin[0]);
	close(pout[0]);
	close(pdat[0]);
	return -1;
}

int
popenfile(Page *p)
{
	char buf[NBUF], *file;
	int i, n, fd, tfd;
	Dir *d;

	fd = p->fd;
	p->fd = -1;
	file = p->data;
	if(fd < 0){
		if((fd = open(file, OREAD)) < 0){
		Err0:
			p->data = nil;
			free(file);
			return -1;
		}
	}
	seek(fd, 0, 0);
	if((d = dirfstat(fd)) == nil){
	Err1:
		close(fd);
		goto Err0;
	}
	if(d->mode & DMDIR){
		free(d);
		d = nil;
		if((n = dirreadall(fd, &d)) < 0)
			goto Err1;
		for(i = 0; i<n; i++)
			addpage(p, d[i].name, popenfile, smprint("%s/%s", file, d[i].name), -1);
		free(d);
		p->text = strdup(p->label);
		goto Err1;
	}
	free(d);

	memset(buf, 0, 32+1);
	if((n = read(fd, buf, 32)) <= 0)
		goto Err1;

	p->fd = fd;
	p->data = nil;
	p->open = popenconv;
	if(memcmp(buf, "%PDF-", 5) == 0 || strstr(buf, "%!"))
		p->open = popengs;
	else if(memcmp(buf, "x T ", 4) == 0){
		p->data = "lp -dstdout";
		p->open = popengs;
	}
	else if(memcmp(buf, "\xF7\x02\x01\x83\x92\xC0\x1C;", 8) == 0){
		p->data = "dvips -Pps -r0 -q1 -f1";
		p->open = popengs;
	}
	else if(memcmp(buf, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0){
		p->data = "doc2ps";
		p->open = popengs;
	}
	else if(memcmp(buf, "GIF", 3) == 0)
		p->data = "gif -t9";
	else if(memcmp(buf, "\111\111\052\000", 4) == 0) 
		p->data = "fb/tiff2pic | fb/3to1 rgbv | fb/pcp -tplan9";
	else if(memcmp(buf, "\115\115\000\052", 4) == 0)
		p->data = "fb/tiff2pic | fb/3to1 rgbv | fb/pcp -tplan9";
	else if(memcmp(buf, "\377\330\377", 3) == 0)
		p->data = "jpg -t9";
	else if(memcmp(buf, "\211PNG\r\n\032\n", 3) == 0)
		p->data = "png -t9";
	else if(memcmp(buf, "compressed\n", 11) == 0)
		p->data = nil;
	else if(memcmp(buf, "\0PC Research, Inc", 17) == 0)
		p->data = "aux/g3p9bit -g";
	else if(memcmp(buf, "TYPE=ccitt-g31", 14) == 0)
		p->data = "aux/g3p9bit -g";
	else if(memcmp(buf, "II*", 3) == 0)
		p->data = "aux/g3p9bit -g";
	else if(memcmp(buf, "TYPE=", 5) == 0)
		p->data = "fb/3to1 rgbv |fb/pcp -tplan9";
	else if(buf[0] == 'P' && '0' <= buf[1] && buf[1] <= '9')
		p->data = "ppm -t9";
	else if(memcmp(buf, "BM", 2) == 0)
		p->data = "bmp -t9";
	else if(memcmp(buf, "          ", 10) == 0 &&
		'0' <= buf[10] && buf[10] <= '9' &&
		buf[11] == ' ')
		p->data = nil;
	else if(strtochan((char*)buf) != 0)
		p->data = nil;
	else {
		werrstr("unknown image format");
		goto Err1;
	}

	if(seek(fd, 0, 0) < 0)
		goto Noseek;
	if((i = read(fd, buf+n, n)) < 0)
		goto Err1;
	if(i != n || memcmp(buf, buf+n, i)){
		n += i;
	Noseek:
		if((tfd = createtmp((ulong)p, "file")) < 0)
			goto Err1;
		while(n > 0){
			if(write(tfd, buf, n) != n)
				goto Err2;
			if((n = read(fd, buf, sizeof(buf))) < 0)
				goto Err2;
		}
		if(dup(tfd, fd) < 0){
		Err2:
			close(tfd);
			goto Err1;
		}
		close(tfd);
	}
	free(file);
	return p->open(p);
}

Page*
nextpage(Page *p)
{
	if(p){
		if(p->down)
			return p->down;
		if(p->next)
			return p->next;
		if(p->up)
			return p->up->next;
	}
	return nil;
}

Page*
prevpage(Page *x)
{
	Page *p, *t;

	if(x){
		for(p = root->down; p; p = t)
			if((t = nextpage(p)) == x)
				return p;
	}
	return nil;
}

int
openpage(Page *p)
{
	int fd;

	fd = -1;
	if(p->open == nil || (fd = p->open(p)) < 0)
		p->open = nil;
	else {
		if(rotate)
			pipeline(fd, "rotate -r %d", rotate);
		if(resize.x)
			pipeline(fd, "resize -x %d", resize.x);
		else if(resize.y)
			pipeline(fd, "resize -y %d", resize.y);
	}
	return fd;
}

void
loadpage(Page *p)
{
	int fd;

	if(p->open && p->image == nil && p->text == nil){
		if((fd = openpage(p)) >= 0){
			pagegen++;
			p->image = readimage(display, fd, 1);
			close(fd);
		}
		if(p->image == nil && p->text == nil)
			p->text = smprint("error: %r");
	}
	p->gen = pagegen;
}

void
unloadpage(Page *p)
{
	if(p->open){
		if(p->text)
			free(p->text);
		p->text = nil;
		if(p->image){
			lockdisplay(display);
			freeimage(p->image);
			unlockdisplay(display);
		}
		p->image = nil;
	}
}

void
unloadpages(int age)
{
	Page *p;

	for(p = root->down; p; p = nextpage(p)){
		if(age == 0)	/* synchronous flush */
			qlock(p);
		else if(!canqlock(p))
			continue;
		if((pagegen - p->gen) >= age)
			unloadpage(p);
		qunlock(p);
	}
}

void
loadpages(Page *p, int ahead, int oviewgen)
{
	int i;

	ahead++;	/* load at least one */
	unloadpages(ahead*2);
	for(i = 0; i < ahead && p; p = nextpage(p), i++){
		if(viewgen != oviewgen)
			break;
		if(canqlock(p)){
			loadpage(p);
			if(viewgen != oviewgen){
				unloadpage(p);
				qunlock(p);
				break;
			}
			if(p == current){
				Point size;

				esetcursor(nil);
				size = pagesize(p);
				if(size.x && size.y && newwin){
					newwin = 0;
					resizewin(size);
				}
				lockdisplay(display);
				drawpage(p);
				unlockdisplay(display);
			}
			qunlock(p);
		}
	}
}

/*
 * A draw operation that touches only the area contained in bot but not in top.
 * mp and sp get aligned with bot.min.
 */
static void
gendrawdiff(Image *dst, Rectangle bot, Rectangle top, 
	Image *src, Point sp, Image *mask, Point mp, int op)
{
	Rectangle r;
	Point origin;
	Point delta;

	USED(op);

	if(Dx(bot)*Dy(bot) == 0)
		return;

	/* no points in bot - top */
	if(rectinrect(bot, top))
		return;

	/* bot - top ≡ bot */
	if(Dx(top)*Dy(top)==0 || rectXrect(bot, top)==0){
		gendrawop(dst, bot, src, sp, mask, mp, op);
		return;
	}

	origin = bot.min;
	/* split bot into rectangles that don't intersect top */
	/* left side */
	if(bot.min.x < top.min.x){
		r = Rect(bot.min.x, bot.min.y, top.min.x, bot.max.y);
		delta = subpt(r.min, origin);
		gendrawop(dst, r, src, addpt(sp, delta), mask, addpt(mp, delta), op);
		bot.min.x = top.min.x;
	}

	/* right side */
	if(bot.max.x > top.max.x){
		r = Rect(top.max.x, bot.min.y, bot.max.x, bot.max.y);
		delta = subpt(r.min, origin);
		gendrawop(dst, r, src, addpt(sp, delta), mask, addpt(mp, delta), op);
		bot.max.x = top.max.x;
	}

	/* top */
	if(bot.min.y < top.min.y){
		r = Rect(bot.min.x, bot.min.y, bot.max.x, top.min.y);
		delta = subpt(r.min, origin);
		gendrawop(dst, r, src, addpt(sp, delta), mask, addpt(mp, delta), op);
		bot.min.y = top.min.y;
	}

	/* bottom */
	if(bot.max.y > top.max.y){
		r = Rect(bot.min.x, top.max.y, bot.max.x, bot.max.y);
		delta = subpt(r.min, origin);
		gendrawop(dst, r, src, addpt(sp, delta), mask, addpt(mp, delta), op);
		bot.max.y = top.max.y;
	}
}

void
zoomdraw(Image *d, Rectangle r, Rectangle top, Image *s, Point sp, int f)
{
	int w, x, y;
	Image *t;
	Point a;

	if(f <= 1){
		gendrawdiff(d, r, top, s, sp, nil, ZP, S);
		return;
	}
	a = ZP;
	if(r.min.x < d->r.min.x){
		sp.x += (d->r.min.x - r.min.x)/f;
		a.x = (d->r.min.x - r.min.x)%f;
		r.min.x = d->r.min.x;
	}
	if(r.min.y < d->r.min.y){
		sp.y += (d->r.min.y - r.min.y)/f;
		a.y = (d->r.min.y - r.min.y)%f;
		r.min.y = d->r.min.y;
	}
	rectclip(&r, d->r);
	w = s->r.max.x - sp.x;
	if(w > Dx(r))
		w = Dx(r);
	t = allocimage(display, Rect(r.min.x, r.min.y, r.min.x+w, r.max.y), s->chan, 0, DNofill);
	if(t == nil)
		return;
	for(y=r.min.y; y<r.max.y; y++){
		draw(t, Rect(r.min.x, y, r.min.x+w, y+1), s, nil, sp);
		if(++a.y == zoom){
			a.y = 0;
			sp.y++;
		}
	}
	sp = t->r.min;
	for(x=r.min.x; x<r.max.x; x++){
		gendrawdiff(d, Rect(x, r.min.y, x+1, r.max.y), top, t, sp, nil, ZP, S);
		if(++a.x == f){
			a.x = 0;
			sp.x++;
		}
	}
	freeimage(t);
}

Point
pagesize(Page *p)
{
	return p->image ? mulpt(subpt(p->image->r.max, p->image->r.min), zoom) : ZP;
}

void
drawpage(Page *p)
{
	Rectangle r;
	Image *i;

	if((i = p->image) == nil){
		char *s;

		if((s = p->text) == nil)
			s = "...";
		r.min = ZP;
		r.max = stringsize(font, p->text);
		r = rectaddpt(r, addpt(subpt(divpt(subpt(screen->r.max, screen->r.min), 2), divpt(r.max, 2)),
			screen->r.min));
		draw(screen, r, display->white, nil, ZP);
		string(screen, r.min, display->black, ZP, font, s);
	} else {
		r = rectaddpt(Rpt(ZP, pagesize(p)), addpt(pos, screen->r.min));
		zoomdraw(screen, r, ZR, i, i->r.min, zoom);
	}
	gendrawdiff(screen, screen->r, r, display->white, ZP, nil, ZP, S);
	border(screen, r, -Borderwidth, display->black, ZP);
	flushimage(display, 1);
}

void
translate(Page *p, Point d)
{
	Rectangle r, or, nr;
	Image *i;

	i = p->image;
	if((i==0) || (d.x==0 && d.y==0))
		return;
	r = rectaddpt(Rpt(ZP, pagesize(p)), addpt(pos, screen->r.min));
	pos = addpt(pos, d);
	nr = rectaddpt(r, d);
	or = r;
	rectclip(&or, screen->r);
	draw(screen, rectaddpt(or, d), screen, nil, or.min);
	zoomdraw(screen, nr, rectaddpt(or, d), i, i->r.min, zoom);
	gendrawdiff(screen, screen->r, nr, display->white, ZP, nil, ZP, S);
	border(screen, nr, -Borderwidth, display->black, ZP);
	flushimage(display, 1);
}

Page*
pageat(int i)
{
	Page *p;

	for(p = root->down; i > 0 && p; p = nextpage(p))
		i--;
	return i ? nil : p;
}

int
pageindex(Page *x)
{
	Page *p;
	int i;

	for(i = 0, p = root->down; p && p != x; p = nextpage(p))
		i++;
	return (p == x) ? i : -1;
}

char*
pagemenugen(int i)
{
	Page *p;
	if(p = pageat(i))
		return p->label;
	return nil;
}

void
showpage(Page *p)
{
	static int nproc;
	int oviewgen;

	if(p == nil)
		return;
	esetcursor(&reading);
	current = p;
	oviewgen = viewgen;
	if(++nproc > NPROC)
		if(waitpid() > 0)
			nproc--;
	switch(rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		loadpages(p, NAHEAD, oviewgen);
		exits(nil);
	}
}

void
zerox(Page *p)
{
	char nam[64], *argv[4];
	int fd;

	if(p == nil)
		return;
	esetcursor(&reading);
	qlock(p);
	if((fd = openpage(p)) < 0)
		goto Out;
	if(rfork(RFREND|RFFDG|RFPROC|RFENVG|RFNOTEG|RFNOWAIT) == 0){
		dup(fd, 0);
		close(fd);

		snprint(nam, sizeof nam, "/bin/%s", argv0);
		argv[0] = argv0;
		argv[1] = "-w";
		argv[2] = nil;
		exec(nam, argv);
		sysfatal("exec: %r");
	}
	close(fd);
Out:
	qunlock(p);
	esetcursor(nil);
}

void
eresized(int new)
{
	Page *p;

	lockdisplay(display);
	if(new && getwindow(display, Refnone) == -1)
		sysfatal("getwindow: %r");
	if(p = current){
		if(canqlock(p)){
			drawpage(p);
			qunlock(p);
		}
	}
	unlockdisplay(display);
}

void killcohort(void)
{
	int i;
	for(i=0;i!=3;i++){	/* It's a long way to the kitchen */
		postnote(PNGROUP, getpid(), "kill");
		sleep(1);
	}
}

void drawerr(Display *, char *msg)
{
	sysfatal("draw: %s", msg);
}

char*
shortname(char *s)
{
	char *x;
	if(x = strrchr(s, '/'))
		if(x[1] != 0)
			return x+1;
	return s;
}

void
usage(void)
{
	fprint(2, "usage: %s [ -iRw ] [ -p ppi ] [ file ... ]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	enum { Eplumb = 4 };
	Plumbmsg *pm;
	Point o;
	Mouse m;
	Event e;
	char *s;
	int i;

	ARGBEGIN {
	case 'a':
	case 'v':
	case 'V':
	case 'P':
		break;
	case 'R':
		newwin = -1;
		break;
	case 'w':
		newwin = 1;
		break;
	case 'i':
		imode = 1;
		break;
	case 'p':
		ppi = atoi(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND;

	/*
	 * so that we can stop all subprocesses with a note,
	 * and to isolate rendezvous from other processes
	 */
	rfork(RFNOTEG|RFNAMEG|RFREND);
	atexit(killcohort);
	if(newwin > 0){
		s = smprint("-pid %d", getpid());
		if(newwindow(s) < 0)
			sysfatal("newwindow: %r");
		free(s);
	}
	initdraw(drawerr, nil, argv0);
	display->locking = 1;
	unlockdisplay(display);
	einit(Ekeyboard|Emouse);
	eplumb(Eplumb, "image");
	nullfd = open("/dev/null", ORDWR);
	current = root = addpage(nil, "root", nil, nil, -1);

	if(*argv == nil && !imode)
		addpage(root, "stdin", popenfile, strdup("/fd/0"), -1);
	for(; *argv; argv++)
		addpage(root, shortname(*argv), popenfile, strdup(*argv), -1);

	for(;;){
		i=event(&e);
		switch(i){
		case Emouse:
			lockdisplay(display);
			m = e.mouse;
			if(m.buttons & 1){
				if(current == nil || !canqlock(current))
					goto Unlock;
				for(;;) {
					o = m.xy;
					m = emouse();
					if((m.buttons & 1) == 0)
						break;
					translate(current, subpt(m.xy, o));
				}
				qunlock(current);
				goto Unlock;
			}
			if(m.buttons & 2){
				i = emenuhit(2, &m, &menu);
				if(i < 0 || i >= nelem(menuitems) || menuitems[i]==nil)
					goto Unlock;
				s = menuitems[i];
				if(strcmp(s, "orig size")==0){
					pos = ZP;
					zoom = 1;
					resize = ZP;
					rotate = 0;
				Unload:
					viewgen++;
					unlockdisplay(display);
					esetcursor(&reading);
					unloadpages(0);
					showpage(current);
					continue;
				}
				if(strncmp(s, "rotate ", 7)==0){
					rotate += atoi(s+7);
					rotate %= 360;
					goto Unload;
				}
				if(strcmp(s, "upside down")==0){
					rotate += 180;
					goto Unload;
				}
				if(strcmp(s, "fit width")==0){
					pos = ZP;
					zoom = 1;
					resize = subpt(screen->r.max, screen->r.min);
					resize.y = 0;
					goto Unload;
				}
				if(strcmp(s, "fit height")==0){
					pos = ZP;
					zoom = 1;
					resize = subpt(screen->r.max, screen->r.min);
					resize.x = 0;
					goto Unload;
				}
				if(strncmp(s, "zoom", 4)==0){
					if(current && canqlock(current)){
						o = subpt(m.xy, screen->r.min);
						if(strstr(s, "in")){
							if(zoom < 0x40000000){
								zoom *= 2;
								pos =  addpt(mulpt(subpt(pos, o), 2), o);
							}
						}else{
							if(zoom > 1){
								zoom /= 2;
								pos =  addpt(divpt(subpt(pos, o), 2), o);
							}
						}
						drawpage(current);
						qunlock(current);
					}
				}
				unlockdisplay(display);
				if(strcmp(s, "next")==0)
					showpage(nextpage(current));
				if(strcmp(s, "prev")==0)
					showpage(prevpage(current));
				if(strcmp(s, "zerox")==0)
					zerox(current);
				if(strcmp(s, "quit")==0)
					exits(0);
				continue;
			}
			if(m.buttons & 4){
				if(root->down == nil)
					goto Unlock;
				pagemenu.lasthit = pageindex(current);
				i = emenuhit(3, &m, &pagemenu);
				unlockdisplay(display);
				if(i != -1)
					showpage(pageat(i));
				continue;
			}
		Unlock:
			unlockdisplay(display);
			break;
		case Ekeyboard:
			switch(e.kbdc){
			case 'q':
			case Kdel:
			case Keof:
				exits(0);
			case Kup:
				if(current == nil || !canqlock(current))
					break;
				lockdisplay(display);
				if(pos.y < 0){
					translate(current, Pt(0, Dy(screen->r)/2));
					unlockdisplay(display);
					qunlock(current);
					continue;
				}
				unlockdisplay(display);
				qunlock(current);
				if(prevpage(current))
					pos.y = 0;
			case Kleft:
				showpage(prevpage(current));
				break;
			case Kdown:
				if(current == nil || !canqlock(current))
					break;
				o = addpt(pos, pagesize(current));
				lockdisplay(display);
				if(o.y > Dy(screen->r)){
					translate(current, Pt(0, -Dy(screen->r)/2));
					unlockdisplay(display);
					qunlock(current);
					continue;
				}
				unlockdisplay(display);
				qunlock(current);
				if(nextpage(current))
					pos.y = 0;
			case ' ':
			case Kright:
				showpage(nextpage(current));
				break;
			}
			break;
		case Eplumb:
			pm = e.v;
			if(pm && pm->ndata > 0){
				int fd;

				fd = -1;
				s = plumblookup(pm->attr, "action");
				if(s && strcmp(s, "quit")==0)
					exits(0);
				if(s && strcmp(s, "showdata")==0){
					static ulong plumbid;

					if((fd = createtmp(plumbid++, "plumb")) < 0){
						fprint(2, "plumb: createtmp: %r\n");
						goto Plumbfree;
					}
					s = malloc(NPATH);
					if(fd2path(fd, s, NPATH) < 0){
						close(fd);
						goto Plumbfree;
					}
					write(fd, pm->data, pm->ndata);
				}else if(pm->data[0] == '/'){
					s = strdup(pm->data);
				}else{
					s = malloc(strlen(pm->wdir)+1+pm->ndata+1);
					sprint(s, "%s/%s", pm->wdir, pm->data);
					cleanname(s);
				}
				showpage(addpage(root, shortname(s), popenfile, s, fd));
			}
		Plumbfree:
			plumbfree(pm);
			break;
		}
	}
}
