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

int rotate = 0;
int viewgen = 0;
int pagegen = 0;
Point resize, pos;
Page *root, *current;
QLock pagelock;
int nullfd;

enum {
	NPROC = 4,
	NAHEAD = 2,
};

char *pagemenugen(int i);

char *menuitems[] = {
	"rotate 90",
	"rotate 180",
	"",
	"fit to width",
	"fit to height",
	"original size",
	"",
	"next",
	"prev",
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

void
setpage(Page *);

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
		setpage(p);
	return p;
}

int
createtmp(ulong id, char *pfx)
{
	char nam[64];
	sprint(nam, "/tmp/page%s%.12d%.8lux", pfx, getpid(), id ^ 0xcafebabe);
	return create(nam, OEXCL|ORCLOSE|ORDWR, 0600);
}

void
pipeline(int fd, char *fmt, ...)
{
	char buf[128], *argv[4];
	va_list arg;
	int pfd[2];

	if(pipe(pfd) < 0){
		fprint(2, "pipe: %r\n");
	Err:
		dup(nullfd, fd);
		return;
	}
	switch(rfork(RFPROC|RFFDG|RFNOWAIT)){
	case -1:
		fprint(2, "rfork: %r\n");
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
		exits(nil);
	}
	close(pfd[1]);
	dup(pfd[0], fd);
	close(pfd[0]);
}

int
popenconv(Page *p)
{
	int fd;

	if((fd = dup(p->fd, -1)) < 0){
		fprint(2, "popenconv: dup: %r\n");
		close(p->fd);
		p->fd = -1;
		return -1;
	}
	seek(fd, 0, 0);
	if(p->data)
		pipeline(fd, "%s", (char*)p->data);
	return fd;
}

int
popengs(Page *p)
{
	int n, i, ifd, ofd, pin[2], pout[2], pdat[2];
	char buf[8*1024], nam[32], *argv[12];

	ifd = p->fd;
	seek(ifd, 0, 0);
	p->fd = -1;
	p->text = strdup(p->label);
	if(p->data)
		pipeline(ifd, "%s", (char*)p->data);
	if(pipe(pin) < 0){
		fprint(2, "popengs: pipe: %r\n");
	Err0:
		close(ifd);
		return -1;
	}
	if(pipe(pout) < 0){
		fprint(2, "popengs: pipe: %r\n");
	Err1:
		close(pin[0]);
		close(pin[1]);
		goto Err0;
	}
	if(pipe(pdat) < 0){
		fprint(2, "popengs: pipe: %r\n");
	Err2:
		close(pdat[0]);
		close(pdat[1]);
		goto Err1;
	}
	switch(rfork(RFPROC|RFFDG)){
	case -1:
		fprint(2, "popengs: rfork: %r\n");
		goto Err2;
	case 0:
		if(dup(pin[1], 0)<0)
			exits("dup");
		if(dup(pout[1], 1)<0)
			exits("dup");
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

		argv[0] = "gs";
		argv[1] = "-q";
		argv[2] = "-sDEVICE=plan9";
		argv[3] = "-sOutputFile=/fd/3";
		argv[4] = "-dBATCH";
		argv[5] = "-dSAFER";
		argv[6] = "-dQUIET";
		argv[7] = "-dTextAlphaBits=4";
		argv[8] = "-dGraphicsAlphaBits=4";
		argv[9] = "-r100";
		argv[10] = "/fd/4";
		argv[11] = nil;
		exec("/bin/gs", argv);
		exits("exec");
	}

	close(pin[1]);
	close(pout[1]);
	close(pdat[1]);
	close(ifd);

	if(rfork(RFMEM|RFPROC) == 0){
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
				if((ofd = createtmp((ulong)p, nam)) < 0){
					fprint(2, "popengs: createtmp: %r\n");
					ofd = dup(nullfd, -1);
				}
			}
			if(write(ofd, buf, n) != n)
				fprint(2, "popengs: write tmp: %r\n");
		}
		if(ofd >= 0)
			close(ofd);
		close(pdat[0]);
		exits(nil);
	}

	for(;;){
		if((n = read(pout[0], buf, sizeof(buf)-1)) <= 0){
			if(n < 0)
				fprint(2, "popengs: read: %r\n");
			break;
		}
		buf[n] = 0;
		if(strstr(buf, "showpage") == 0)
			continue;
		if(write(pin[0], "\n", 1) != 1){
			fprint(2, "popengs: write: %r\n");
			break;
		}
	}
	close(pin[0]);
	close(pout[0]);

	waitpid();
	waitpid();

	return -1;
}

int
popenfile(Page *p)
{
	char tmp[8*1024], *file;
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

	memset(tmp, 0, 32+1);
	if((n = read(fd, tmp, 32)) <= 0)
		goto Err1;

	p->fd = fd;
	p->data = nil;
	p->open = popenconv;
	if(memcmp(tmp, "%PDF-", 5) == 0 || strstr(tmp, "%!"))
		p->open = popengs;
	else if(memcmp(tmp, "x T ", 4) == 0){
		p->data = "lp -dstdout";
		p->open = popengs;
	}
	else if(memcmp(tmp, "\xF7\x02\x01\x83\x92\xC0\x1C;", 8) == 0){
		p->data = "dvips -Pps -r0 -q1 -f1";
		p->open = popengs;
	}
	else if(memcmp(tmp, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0){
		p->data = "doc2ps";
		p->open = popengs;
	}
	else if(memcmp(tmp, "GIF", 3) == 0)
		p->data = "gif -t9";
	else if(memcmp(tmp, "\111\111\052\000", 4) == 0) 
		p->data = "fb/tiff2pic | fb/3to1 rgbv | fb/pcp -tplan9";
	else if(memcmp(tmp, "\115\115\000\052", 4) == 0)
		p->data = "fb/tiff2pic | fb/3to1 rgbv | fb/pcp -tplan9";
	else if(memcmp(tmp, "\377\330\377", 3) == 0)
		p->data = "jpg -t9";
	else if(memcmp(tmp, "\211PNG\r\n\032\n", 3) == 0)
		p->data = "png -t9";
	else if(memcmp(tmp, "compressed\n", 11) == 0)
		p->data = nil;
	else if(memcmp(tmp, "\0PC Research, Inc", 17) == 0)
		p->data = "aux/g3p9bit -g";
	else if(memcmp(tmp, "TYPE=ccitt-g31", 14) == 0)
		p->data = "aux/g3p9bit -g";
	else if(memcmp(tmp, "II*", 3) == 0)
		p->data = "aux/g3p9bit -g";
	else if(memcmp(tmp, "TYPE=", 5) == 0)
		p->data = "fb/3to1 rgbv |fb/pcp -tplan9";
	else if(tmp[0] == 'P' && '0' <= tmp[1] && tmp[1] <= '9')
		p->data = "ppm -t9";
	else if(memcmp(tmp, "BM", 2) == 0)
		p->data = "bmp -t9";
	else if(memcmp(tmp, "          ", 10) == 0 &&
		'0' <= tmp[10] && tmp[10] <= '9' &&
		tmp[11] == ' ')
		p->data = nil;
	else if(strtochan((char*)tmp) != 0)
		p->data = nil;
	else {
		werrstr("unknown image format");
		goto Err1;
	}

	if(seek(fd, 0, 0) < 0)
		goto Noseek;
	if((i = read(fd, tmp+n, n)) < 0)
		goto Err1;
	if(i != n || memcmp(tmp, tmp+n, i)){
		n += i;
	Noseek:
		if((tfd = createtmp((ulong)p, "spool")) < 0)
			goto Err1;
		while(n > 0){
			if(write(tfd, tmp, n) != n)
				goto Err2;
			if((n = read(fd, tmp, sizeof(tmp))) < 0)
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

void
loadpage(Page *p)
{
	int fd;

	if(p->open && p->image == nil && p->text == nil){
		if((fd = p->open(p)) < 0)
			p->open = nil;
		else {
			pagegen++;
			if(rotate)
				pipeline(fd, "rotate -r %d", rotate);
			if(resize.x && resize.y)
				pipeline(fd, "resize -x %d -y %d", resize.x, resize.y);
			else if(resize.x)
				pipeline(fd, "resize -x %d", resize.x);
			else if(resize.y)
				pipeline(fd, "resize -y %d", resize.y);
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
		if(!canqlock(p))
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

	unloadpages(NAHEAD*2);

	ahead++;	/* load at least one */
	for(i = 0; i < ahead && p; p = nextpage(p)){
		if(viewgen != oviewgen)
			break;
		if(canqlock(p)){
			loadpage(p);
			if(viewgen != oviewgen){
				unloadpage(p);
				qunlock(p);
				break;
			}
			qunlock(p);
			if(p == current)
				eresized(0);
			i++;
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

void eresized(int new)
{
	Rectangle r;
	Image *i;
	Page *p;

	if(new){
		lockdisplay(display);
		if(getwindow(display, Refnone) == -1) {
			fprint(2, "getwindow: %r\n");
			exits("getwindow");
		}
		unlockdisplay(display);
	}

	if((p = current) == nil)
		return;

	qlock(p);
	lockdisplay(display);
	if((i = p->image) == nil){
		char *s;
		if((s = p->text) == nil)
			goto Out;
		r.min = ZP;
		r.max = stringsize(font, p->text);
		if((i = allocimage(display, r, screen->chan, 0, DWhite)) == nil)
			goto Out;
		string(i, r.min, display->black, ZP, font, s);
		p->image = i;
	}
	r = rectaddpt(rectaddpt(Rpt(ZP, subpt(i->r.max, i->r.min)), screen->r.min), pos);
	draw(screen, r, i, nil, i->r.min);
	gendrawdiff(screen, screen->r, r, display->white, ZP, nil, ZP, S);
	border(screen, r, -4, display->black, ZP);
	flushimage(display, 1);
	esetcursor(nil);
Out:
	unlockdisplay(display);
	qunlock(p);
}

void translate(Page *p, Point d)
{
	Rectangle r, or, nr;
	Image *i;

	if(p == nil || d.x==0 && d.y==0)
		return;
	if(!canqlock(p))
		return;
	if(i = p->image){
		r = rectaddpt(rectaddpt(Rpt(ZP, subpt(i->r.max, i->r.min)), screen->r.min), pos);
		pos = addpt(pos, d);
		nr = rectaddpt(r, d);
		or = r;
		rectclip(&or, screen->r);
		draw(screen, rectaddpt(or, d), screen, nil, or.min);
		gendrawdiff(screen, nr, rectaddpt(or, d), i, i->r.min, nil, ZP, S);
		gendrawdiff(screen, screen->r, nr, display->white, ZP, nil, ZP, S);
		border(screen, nr, -4, display->black, ZP);
		flushimage(display, 1);
	}
	qunlock(p);
}

Point
pagesize(Page *p)
{
	Point t = ZP;
	if(p && canqlock(p)){
		if(p->image)
			t = subpt(p->image->r.max, p->image->r.min);
		qunlock(p);
	}
	return t;
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
setpage(Page *p)
{
	static int nproc;
	int oviewgen;

	if(p == nil)
		return;
	current = p;
	oviewgen = viewgen;
	esetcursor(&reading);
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
main(int argc, char *argv[])
{
	enum { Eplumb = 4 };
	Plumbmsg *pm;
	Point o;
	Mouse m;
	Event e;
	char *s;
	int i;

	/*
	 * so that we can stop all subprocesses with a note,
	 * and to isolate rendezvous from other processes
	 */
	rfork(RFNOTEG|RFNAMEG|RFREND);
	atexit(killcohort);

	ARGBEGIN {
	} ARGEND;

	nullfd = open("/dev/null", ORDWR);

	initdraw(drawerr, nil, "npage");
	display->locking = 1;
	unlockdisplay(display);
	einit(Ekeyboard|Emouse);
	eplumb(Eplumb, "image");

	current = root = addpage(nil, "root", nil, nil, -1);
	if(*argv == nil)
		addpage(root, "-", popenfile, strdup("/fd/0"), -1);
	for(; *argv; argv++)
		addpage(root, shortname(*argv), popenfile, strdup(*argv), -1);

	for(;;){
		i=event(&e);
		switch(i){
		case Emouse:
			lockdisplay(display);
			m = e.mouse;
			if(m.buttons & 1){
				for(;;) {
					o = m.xy;
					m = emouse();
					if((m.buttons & 1) == 0)
						break;
					translate(current, subpt(m.xy, o));
				}
				unlockdisplay(display);
				continue;
			}
			if(m.buttons & 2){
				i = emenuhit(2, &m, &menu);
				unlockdisplay(display);

				if(i < 0 || i >= nelem(menuitems) || menuitems[i]==nil)
					continue;
				s = menuitems[i];
				if(strncmp(s, "rotate ", 7)==0){
					rotate += atoi(s+7);
					rotate %= 360;
					goto Unload;
				}
				if(strcmp(s, "fit to width")==0){
					pos = ZP;
					resize = subpt(screen->r.max, screen->r.min);
					resize.y = 0;
					goto Unload;
				}
				if(strcmp(s, "fit to height")==0){
					pos = ZP;
					resize = subpt(screen->r.max, screen->r.min);
					resize.x = 0;
					goto Unload;
				}
				if(strcmp(s, "original size")==0){
					pos = ZP;
					resize = ZP;
					rotate = 0;
				Unload:
					viewgen++;
					unloadpages(0);
					setpage(current);
					continue;
				}
				if(strcmp(s, "next")==0)
					setpage(nextpage(current));
				if(strcmp(s, "prev")==0)
					setpage(prevpage(current));
				if(strcmp(s, "quit")==0)
					exits(0);
				continue;
			}
			if(m.buttons & 4){
				pagemenu.lasthit = pageindex(current);
				i = emenuhit(3, &m, &pagemenu);
				unlockdisplay(display);

				if(i != -1)
					setpage(pageat(i));
				continue;
			}
			unlockdisplay(display);
			break;
		case Ekeyboard:
			switch(e.kbdc){
			case 'q':
			case Kdel:
				exits(0);
			case Kup:
				lockdisplay(display);
				if(pos.y < 0){
					translate(current, Pt(0, Dy(screen->r)/2));
					unlockdisplay(display);
					continue;
				}
				if(prevpage(current))
					pos.y = 0;
				unlockdisplay(display);
			case Kleft:
				setpage(prevpage(current));
				break;
			case Kdown:
				lockdisplay(display);
				o = addpt(pos, pagesize(current));
				if(o.y >= Dy(screen->r)){
					translate(current, Pt(0, -Dy(screen->r)/2));
					unlockdisplay(display);
					continue;
				}
				if(nextpage(current))
					pos.y = 0;
				unlockdisplay(display);
			case ' ':
			case Kright:
				setpage(nextpage(current));
				break;
			}
			break;
		case Eplumb:
			pm = e.v;
			if(pm && pm->ndata > 0){
				int fd;

				fd = -1;
				s = plumblookup(pm->attr, "action");
				if(s && strcmp(s, "showdata")==0){
					static ulong plumbid;

					if((fd = createtmp(plumbid++, "plumb")) < 0){
						fprint(2, "plumb: createtmp: %r\n");
						goto Plumbfree;
					}
					s = mallocz(1024, 1);
					fd2path(fd, s, 1024);
					write(fd, pm->data, pm->ndata);
				}else if(pm->data[0] == '/'){
					s = strdup(pm->data);
				}else{
					s = malloc(strlen(pm->wdir)+1+pm->ndata+1);
					sprint(s, "%s/%s", pm->wdir, pm->data);
					cleanname(s);
				}
				setpage(addpage(root, shortname(s), popenfile, s, fd));
			}
		Plumbfree:
			plumbfree(pm);
			break;
		}
	}
}
