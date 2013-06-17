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
	char	*ext;
	void	*data;
	int	(*open)(Page *);

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

Image *frame, *paper, *ground;

char pagespool[] = "/tmp/pagespool.";

enum {
	NPROC = 4,
	NAHEAD = 2,
	NBUF = 8*1024,
	NPATH = 1024,
};

enum {
	Corigsize,
	Czoomin,
	Czoomout,
	Cfitwidth,
	Cfitheight,
	Crotate90,
	Cupsidedown,
	Cdummy1,
	Cnext,
	Cprev,
	Czerox,
	Cwrite,
	Cext,
	Cdummy2,
	Cquit,
};

struct {
	char	*m;
	Rune	k1;
	Rune	k2;
	Rune	k3;
} cmds[] = {
	[Corigsize]	"orig size",	'o', Kesc, 0,
	[Czoomin]	"zoom in",	'+', 0, 0,
	[Czoomout]	"zoom out",	'-', 0, 0,
	[Cfitwidth]	"fit width",	'f', 0, 0,
	[Cfitheight]	"fit height",	'h', 0, 0,
	[Crotate90]	"rotate 90",	'r', 0, 0,
	[Cupsidedown]	"upside down",	'u', 0, 0,
	[Cdummy1]	"",		0, 0, 0,
	[Cnext]		"next",		Kright, ' ', '\n', 
	[Cprev]		"prev",		Kleft, Kbs, 0,
	[Czerox]	"zerox",	'z', 0, 0,
	[Cwrite]	"write",	'w', 0, 0,
	[Cext]		"ext",		'x', 0, 0,
	[Cdummy2]	"",		0, 0, 0,
	[Cquit]		"quit",		'q', Kdel, Keof,
};

char *pagemenugen(int i);
char *cmdmenugen(int i);

Menu pagemenu = {
	nil,
	pagemenugen,
	-1,
};

Menu cmdmenu = {
	nil,
	cmdmenugen,
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

void showpage1(Page *);
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
		showpage1(p);
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
	if(display->image){
		Point dsize = subpt(display->image->r.max, display->image->r.min);
		if(size.x > dsize.x)
			size.x = dsize.x;
		if(size.y > dsize.y)
			size.y = dsize.y;
		/* can't just conver whole display */
		if(eqpt(size, dsize))
			size.y--;
	}
	fprint(wctl, "resize -dx %d -dy %d\n", size.x, size.y);
	close(wctl);
}

int
createtmp(char *pfx)
{
	static ulong id = 1;
	char nam[64];
	snprint(nam, sizeof nam, "%s%s%.12d%.8lux", pagespool, pfx, getpid(), id++);
	return create(nam, OEXCL|ORCLOSE|ORDWR, 0600);
}

int
catchnote(void *, char *msg)
{
	if(strstr(msg, "sys: write on closed pipe"))
		return 1;
	if(strstr(msg, "hangup"))
		return 1;
	if(strstr(msg, "alarm"))
		return 1;
	if(strstr(msg, "interrupt"))
		return 1;
	if(strstr(msg, "kill"))
		exits("killed");
	return 0;
}

void
dupfds(int fd, ...)
{
	int mfd, n, i;
	va_list arg;
	Dir *dir;

	va_start(arg, fd);
	for(mfd = 0; fd >= 0; fd = va_arg(arg, int), mfd++)
		if(fd != mfd)
			if(dup(fd, mfd) < 0)
				sysfatal("dup: %r");
	va_end(arg);
	if((fd = open("/fd", OREAD)) < 0)
		sysfatal("open: %r");
	n = dirreadall(fd, &dir);
	for(i=0; i<n; i++){
		if(strstr(dir[i].name, "ctl"))
			continue;
		fd = atoi(dir[i].name);
		if(fd >= mfd)
			close(fd);
	}
	free(dir);
}

void
pipeline(int fd, char *fmt, ...)
{
	char buf[NPATH], *argv[4];
	va_list arg;
	int pfd[2];

	if(pipe(pfd) < 0){
	Err:
		dup(nullfd, fd);
		return;
	}
	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	switch(rfork(RFPROC|RFMEM|RFFDG|RFREND|RFNOWAIT)){
	case -1:
		close(pfd[0]);
		close(pfd[1]);
		goto Err;
	case 0:
		dupfds(fd, pfd[1], 2, -1);
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

char*
shortname(char *s)
{
	char *x;

	while(strlen(s) > 20){
		if((x = strchr(s, '/')) == nil)
			break;
		if(x[1] == 0)
			break;
		s = x+1;
	}
	return s;
}

int
popenfile(Page*);

int
popenimg(Page *p)
{
	char nam[NPATH];
	int fd;

	if((fd = dup(p->fd, -1)) < 0){
		close(p->fd);
		p->fd = -1;
		return -1;
	}

	seek(fd, 0, 0);
	if(p->data){
		p->ext = p->data;
		if(strcmp(p->ext, "ico") == 0)
			snprint(nam, sizeof(nam), "%s -c", p->ext);
		else
			snprint(nam, sizeof(nam), "%s -t9", p->ext);
		pipeline(fd, "%s", nam);
	}

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

int
popenfilter(Page *p)
{
	seek(p->fd, 0, 0);
	if(p->data){
		pipeline(p->fd, "%s", (char*)p->data);
		p->data = nil;
	}
	p->open = popenfile;
	return p->open(p);
}

int
popentape(Page *p)
{
	char mnt[32], cmd[64], *argv[4];

	seek(p->fd, 0, 0);
	snprint(mnt, sizeof(mnt), "/n/tapefs.%.12d%.8lux", getpid(), (ulong)p);
	snprint(cmd, sizeof(cmd), "%s -m %s /fd/0", (char*)p->data, mnt);
	switch(rfork(RFPROC|RFMEM|RFFDG|RFREND)){
	case -1:
		close(p->fd);
		p->fd = -1;
		return -1;
	case 0:
		dupfds(p->fd, 1, 2, -1);
		argv[0] = "rc";
		argv[1] = "-c";
		argv[2] = cmd;
		argv[3] = nil;
		exec("/bin/rc", argv);
		sysfatal("exec: %r");
	}
	close(p->fd);
	waitpid();
	p->fd = -1;
	p->data = strdup(mnt);
	p->open = popenfile;
	return p->open(p);
}

int
popenepub(Page *p)
{
	char buf[NPATH], *s, *e;
	int n, fd;

	fd = p->fd;
	p->fd = -1;
	s = buf;
	e = buf+sizeof(buf)-1;
	s += snprint(s, e-s, "%s/", (char*)p->data);
	free(p->data);
	p->data = nil;
	pipeline(fd, "awk '/\\<rootfile/{"
		"if(match($0, /full\\-path\\=\\\"([^\\\"]+)\\\"/)){"
		"print substr($0, RSTART+11,RLENGTH-12);exit}}'");
	n = read(fd, s, e - s);
	close(fd);
	if(n <= 0)
		return -1;
	while(n > 0 && s[n-1] == '\n')
		n--;
	s += n;
	*s = 0;
	if((fd = open(buf, OREAD)) < 0)
		return -1;
	pipeline(fd, "awk '/\\<item/{"
		"if(match($0, /id\\=\\\"([^\\\"]+)\\\"/)){"
		"id=substr($0, RSTART+4, RLENGTH-5);"
		"if(match($0, /href\\=\\\"([^\\\"]+)\\\"/)){"
		"item[id]=substr($0, RSTART+6, RLENGTH-7)}}};"
		"/\\<itemref/{"
		"if(match($0, /idref\\=\\\"([^\\\"]+)\\\"/)){"
		"ref=substr($0, RSTART+7, RLENGTH-8);"
		"print item[ref]; fflush}}'");
	s = strrchr(buf, '/')+1;
	while((n = read(fd, s, e-s)) > 0){
		while(n > 0 && s[n-1] == '\n')
			n--;
		s[n] = 0;
		addpage(p, shortname(buf), popenfile, strdup(buf), -1);
	}
	close(fd);
	return -1;
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
	switch(rfork(RFPROC|RFMEM|RFFDG|RFNOWAIT)){
	case -1:
		close(pfd[0]);
		close(pfd[1]);
		return -1;
	case 0:
		gs = p->data;
		qlock(gs);
		dupfds(gs->pdat, gs->pin, pfd[1], -1);
		fprint(1, "%s DoPDFPage\n"
			"(/fd/3) (w) file "
			"dup flushfile "
			"dup (THIS IS NOT AN INFERNO BITMAP\\n) writestring "
			"flushfile\n", p->label);
		while((n = read(0, buf, sizeof buf)) > 0){
			if(memcmp(buf, "THIS IS NOT AN INFERNO BITMAP\n", 30) == 0)
				break;
			write(2, buf, n);
		}
		qunlock(gs);
		exits(nil);
	}
	close(pfd[1]);
	return pfd[0];
}

int
infernobithdr(char *buf, int n)
{
	if(n >= 11){
		if(memcmp(buf, "compressed\n", 11) == 0)
			return 1;
		if(strtochan((char*)buf))
			return 1;
		if(memcmp(buf, "          ", 10) == 0 && 
			'0' <= buf[10] && buf[10] <= '9' &&
			buf[11] == ' ')
			return 1;
	}
	return 0;
}

int
popengs(Page *p)
{
	int n, i, pdf, ifd, ofd, pin[2], pout[2], pdat[2];
	char buf[NBUF], nam[32], *argv[16];

	pdf = 0;
	ifd = p->fd;
	p->fd = -1;
	seek(ifd, 0, 0);
	if(read(ifd, buf, 5) != 5)
		goto Err0;
	seek(ifd, 0, 0);
	if(memcmp(buf, "%PDF-", 5) == 0)
		pdf = 1;
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

	argv[0] = (char*)p->data;
	switch(rfork(RFPROC|RFMEM|RFFDG|RFREND|RFNOWAIT)){
	case -1:
		goto Err2;
	case 0:
		if(pdf)
			dupfds(pin[1], pout[1], 2, pdat[1], ifd, -1);
		else
			dupfds(nullfd, nullfd, 2, pdat[1], ifd, -1);
		if(argv[0])
			pipeline(4, "%s", argv[0]);
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
		argv[10] = "-dDOINTERPOLATE";
		argv[11] = pdf ? "-" : "/fd/4";
		argv[12] = nil;
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
			if(ofd >= 0 && (n <= 0 || infernobithdr(buf, n))){
				snprint(nam, sizeof nam, "%d", i);
				addpage(p, nam, popenimg, nil, ofd);
				ofd = -1;
			}
			if(n <= 0)
				break;
			if(ofd < 0){
				snprint(nam, sizeof nam, "%.4d", ++i);
				if((ofd = createtmp(nam)) < 0)
					ofd = dup(nullfd, -1);
			}
			if(write(ofd, buf, n) != n)
				break;
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
filetype(char *buf, int nbuf, char *typ, int ntyp)
{
	int n, ifd[2], ofd[2];
	char *argv[3];

	if(infernobithdr(buf, nbuf)){
		strncpy(typ, "image/p9bit", ntyp);
		return 0;
	}

	typ[0] = 0;
	if(pipe(ifd) < 0)
		return -1;
	if(pipe(ofd) < 0){
		close(ifd[0]);
		close(ifd[1]);
		return -1;
	}
	if(rfork(RFPROC|RFMEM|RFFDG|RFREND|RFNOWAIT) == 0){
		dupfds(ifd[1], ofd[1], 2, -1);
		argv[0] = "file";
		argv[1] = "-m";
		argv[2] = 0;
		exec("/bin/file", argv);
	}
	close(ifd[1]);
	close(ofd[1]);
	if(rfork(RFPROC|RFMEM|RFFDG|RFNOWAIT) == 0){
		dupfds(ifd[0], -1);
		write(0, buf, nbuf);
		exits(nil);
	}
	close(ifd[0]);
	if((n = readn(ofd[0], typ, ntyp-1)) < 0)
		n = 0;
	close(ofd[0]);
	while(n > 0 && typ[n-1] == '\n')
		n--;
	typ[n] = 0;
	return 0;
}

int
dircmp(void *p1, void *p2)
{
	Dir *d1, *d2;

	d1 = p1;
	d2 = p2;

	return strcmp(d1->name, d2->name);
}

int
popenfile(Page *p)
{
	static struct {
		char	*typ;
		void	*open;
		void	*data;
	} tab[] = {
	"application/pdf",		popengs,	nil,
	"application/postscript",	popengs,	nil,
	"application/troff",		popengs,	"lp -dstdout",
	"text/plain",			popengs,	"lp -dstdout",
	"text/html",			popengs,	"uhtml | html2ms | tbl | troff -ms | lp -dstdout",
	"application/dvi",		popengs,	"dvips -Pps -r0 -q1 -f1",
	"application/doc",		popengs,	"doc2ps",
	"application/zip",		popentape,	"fs/zipfs",
	"application/x-tar",		popentape,	"fs/tarfs",
	"application/x-ustar",		popentape,	"fs/tarfs",
	"application/x-compress",	popenfilter,	"uncompress",
	"application/x-gzip",		popenfilter,	"gunzip",
	"application/x-bzip2",		popenfilter,	"bunzip2",
	"image/gif",			popenimg,	"gif",
	"image/jpeg",			popenimg,	"jpg",
	"image/png",			popenimg,	"png",
	"image/ppm",			popenimg,	"ppm",
	"image/bmp",			popenimg,	"bmp",
	"image/tga",			popenimg,	"tga",
	"image/x-icon",			popenimg,	"ico",
	"image/p9bit",			popenimg,	nil,
	};

	char buf[NBUF], typ[128], *file;
	int i, n, fd, tfd;
	Dir *d;

	fd = p->fd;
	p->fd = -1;
	p->ext = nil;
	file = p->data;
	p->data = nil;
	if(fd < 0){
		if((fd = open(file, OREAD)) < 0){
		Err0:
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

		snprint(buf, sizeof(buf), "%s/META-INF/container.xml", file);
		if((tfd = open(buf, OREAD)) >= 0){
			close(fd);
			p->fd = tfd;
			p->data = file;
			p->open = popenepub;
			return p->open(p);
		}

		if((n = dirreadall(fd, &d)) < 0)
			goto Err1;
		qsort(d, n, sizeof d[0], dircmp);
		for(i = 0; i<n; i++)
			addpage(p, d[i].name, popenfile, smprint("%s/%s", file, d[i].name), -1);
		free(d);
		goto Err1;
	}
	free(d);

	memset(buf, 0, NBUF/2);
	if((n = readn(fd, buf, NBUF/2)) <= 0)
		goto Err1;
	filetype(buf, n, typ, sizeof(typ));
	for(i=0; i<nelem(tab); i++)
		if(strncmp(typ, tab[i].typ, strlen(tab[i].typ)) == 0)
			break;
	if(i == nelem(tab)){
		werrstr("unknown image format: %s", typ);
		goto Err1;
	}
	p->fd = fd;
	p->data = tab[i].data;
	p->open = tab[i].open;
	if(seek(fd, 0, 0) < 0)
		goto Noseek;
	if((i = readn(fd, buf+n, n)) < 0)
		goto Err1;
	if(i != n || memcmp(buf, buf+n, i)){
		n += i;
	Noseek:
		if((tfd = createtmp("file")) < 0)
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
	if(p && p->down)
		return p->down;
	while(p){
		if(p->next)
			return p->next;
		p = p->up;
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

	if(p->open && p->image == nil){
		fd = openpage(p);
		if(fd >= 0){
			pagegen++;
			if((p->image = readimage(display, fd, 1)) == nil)
				fprint(2, "readimage: %r\n");
			close(fd);
		}
		if(p->image == nil)
			p->open = nil;
	}
	p->gen = pagegen;
}

void
unloadpage(Page *p)
{
	if(p->open == nil || p->image == nil)
		return;
	lockdisplay(display);
	freeimage(p->image);
	unlockdisplay(display);
	p->image = nil;
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

int
alphachan(ulong chan)
{
	for(; chan; chan >>= 8)
		if(TYPE(chan) == CAlpha)
			return 1;
	return 0;
}

void
zoomdraw(Image *d, Rectangle r, Rectangle top, Image *b, Image *s, Point sp, int f)
{
	Rectangle dr;
	Image *t;
	Point a;
	int w;

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
	dr = r;
	dr.max.x = dr.min.x+w;
	if(!alphachan(s->chan))
		b = nil;
	if(f <= 1){
		if(b) gendrawdiff(d, dr, top, b, sp, nil, ZP, SoverD);
		gendrawdiff(d, dr, top, s, sp, nil, ZP, SoverD);
		return;
	}
	if((t = allocimage(display, dr, s->chan, 0, 0)) == nil)
		return;
	for(; dr.min.y < r.max.y; dr.min.y++){
		dr.max.y = dr.min.y+1;
		draw(t, dr, s, nil, sp);
		if(++a.y == f){
			a.y = 0;
			sp.y++;
		}
	}
	dr = r;
	for(sp=dr.min; dr.min.x < r.max.x; sp.x++){
		dr.max.x = dr.min.x+1;
		if(b) gendrawdiff(d, dr, top, b, sp, nil, ZP, SoverD);
		gendrawdiff(d, dr, top, t, sp, nil, ZP, SoverD);
		for(dr.min.x++; ++a.x < f && dr.min.x < r.max.x; dr.min.x++){
			dr.max.x = dr.min.x+1;
			gendrawdiff(d, dr, top, d, Pt(dr.min.x-1, dr.min.y), nil, ZP, SoverD);
		}
		a.x = 0;
	}
	freeimage(t);
}

Point
pagesize(Page *p)
{
	return p->image ? mulpt(subpt(p->image->r.max, p->image->r.min), zoom) : ZP;
}

void
drawframe(Rectangle r)
{
	border(screen, r, -Borderwidth, frame, ZP);
	gendrawdiff(screen, screen->r, insetrect(r, -Borderwidth), ground, ZP, nil, ZP, SoverD);
	flushimage(display, 1);
}

void
drawpage(Page *p)
{
	Rectangle r;
	Image *i;

	if(i = p->image){
		r = rectaddpt(Rpt(ZP, pagesize(p)), addpt(pos, screen->r.min));
		zoomdraw(screen, r, ZR, paper, i, i->r.min, zoom);
	} else {
		r = Rpt(ZP, stringsize(font, p->label));
		r = rectaddpt(r, addpt(subpt(divpt(subpt(screen->r.max, screen->r.min), 2),
			divpt(r.max, 2)), screen->r.min));
		draw(screen, r, paper, nil, ZP);
		string(screen, r.min, display->black, ZP, font, p->label);
	}
	drawframe(r);
}

void
translate(Page *p, Point d)
{
	Rectangle r, nr;
	Image *i;

	i = p->image;
	if(i==0 || d.x==0 && d.y==0)
		return;
	r = rectaddpt(Rpt(ZP, pagesize(p)), addpt(pos, screen->r.min));
	pos = addpt(pos, d);
	nr = rectaddpt(r, d);
	rectclip(&r, screen->r);
	draw(screen, rectaddpt(r, d), screen, nil, r.min);
	zoomdraw(screen, nr, rectaddpt(r, d), paper, i, i->r.min, zoom);
	drawframe(nr);
}

Page*
findpage(char *name)
{
	Page *p;
	int n;

	n = strlen(name);
	/* look in current document first */
	if(current && current->up){
		for(p = current->up->down; p; p = p->next)
			if(cistrncmp(p->label, name, n) == 0)
				return p;
	}
	/* look everywhere */
	for(p = root->down; p; p = nextpage(p))
		if(cistrncmp(p->label, name, n) == 0)
			return p;
	return nil;
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

char*
cmdmenugen(int i)
{
	if(i < 0 || i >= nelem(cmds))
		return nil;
	return cmds[i].m;
}

/*
 * spawn new proc to load a run of pages starting with p
 * the display should *not* be locked as it gets called
 * from recursive page load.
 */
void
showpage1(Page *p)
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

/* recursive display lock, called from main proc only */
void
drawlock(int dolock){
	static int ref = 0;
	if(dolock){
		if(ref++ == 0)
			lockdisplay(display);
	} else {
		if(--ref == 0)
			unlockdisplay(display);
	}
}


void
showpage(Page *p)
{
	drawlock(0);
	showpage1(p);
	drawlock(1);
}

void
shownext(void)
{
	Page *p;

	for(p = nextpage(current); p; p = nextpage(p))
		if(p->image || p->open)
			break;
	showpage(p);
}

void
showprev(void)
{
	Page *p;

	for(p = prevpage(current); p; p = prevpage(p))
		if(p->image || p->open)
			break;
	showpage(p);
}

void
zerox(Page *p)
{
	char nam[64], *argv[4];
	int fd;

	if(p == nil)
		return;
	drawlock(0);
	qlock(p);
	if((fd = openpage(p)) < 0)
		goto Out;
	if(rfork(RFPROC|RFMEM|RFFDG|RFENVG|RFNOTEG|RFNOWAIT) == 0){
		dupfds(fd, 1, 2, -1);
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
	drawlock(1);
}

void
showext(Page *p)
{
	char buf[128], label[64], *argv[4];
	Point ps;
	int fd;

	if(p->ext == nil)
		return;
	snprint(label, sizeof(label), "%s %s", p->ext, p->label);
	ps = Pt(0, 0);
	if(p->image)
		ps = addpt(subpt(p->image->r.max, p->image->r.min), Pt(24, 24));
	drawlock(0);
	if((fd = p->fd) < 0){
		if(p->open != popenfile)
			return;
		fd = open((char*)p->data, OREAD);
	} else {
		fd = dup(fd, -1);
		seek(fd, 0, 0);
	}
	if(rfork(RFPROC|RFMEM|RFFDG|RFNOTEG|RFNAMEG|RFNOWAIT) == 0){
		snprint(buf, sizeof(buf), "-pid %d", getpid());
		if(newwindow(buf) != -1){
			dupfds(fd, 1, 2, -1);
			if((fd = open("/dev/label", OWRITE)) >= 0){
				write(fd, label, strlen(label));
				close(fd);
			}
			if(ps.x && ps.y)
				resizewin(ps);
			argv[0] = "rc";
			argv[1] = "-c";
			argv[2] = p->ext;
			argv[3] = nil;
			exec("/bin/rc", argv);
		}
		exits(0);
	}
	close(fd);
	drawlock(1);
}


void
eresized(int new)
{
	Page *p;

	drawlock(1);
	if(new && getwindow(display, Refnone) == -1)
		sysfatal("getwindow: %r");
	if(p = current){
		if(canqlock(p)){
			drawpage(p);
			qunlock(p);
		}
	}
	drawlock(0);
}

int cohort = -1;
void killcohort(void)
{
	int i;
	for(i=0;i!=3;i++){	/* It's a long way to the kitchen */
		postnote(PNGROUP, cohort, "kill");
		sleep(1);
	}
}

void drawerr(Display *, char *msg)
{
	sysfatal("draw: %s", msg);
}

void
usage(void)
{
	fprint(2, "usage: %s [ -iRw ] [ -p ppi ] [ file ... ]\n", argv0);
	exits("usage");
}

void
docmd(int i, Mouse *m)
{
	char buf[NPATH], *s;
	Point o;
	int fd;

	switch(i){
	case Corigsize:
		pos = ZP;
		zoom = 1;
		resize = ZP;
		rotate = 0;
	Unload:
		viewgen++;
		drawlock(0);
		unloadpages(0);
		showpage1(current);
		drawlock(1);
		break;
	case Cupsidedown:
		rotate += 90;
	case Crotate90:
		rotate += 90;
		rotate %= 360;
		goto Unload;
	case Cfitwidth:
		pos = ZP;
		zoom = 1;
		resize = subpt(screen->r.max, screen->r.min);
		resize.y = 0;
		goto Unload;
	case Cfitheight:
		pos = ZP;
		zoom = 1;
		resize = subpt(screen->r.max, screen->r.min);
		resize.x = 0;
		goto Unload;
	case Czoomin:
	case Czoomout:
		if(current == nil || !canqlock(current))
			break;
		o = subpt(m->xy, screen->r.min);
		if(i == Czoomin){
			if(zoom < 0x1000){
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
		break;
	case Cwrite:
		if(current == nil || !canqlock(current))
			break;
		if(current->image){
			s = nil;
			if(current->up && current->up != root)
				s = current->up->label;
			snprint(buf, sizeof(buf), "%s%s%s.bit",
				s ? s : "",
				s ? "." : "",
				current->label);
			if(eenter("Write", buf, sizeof(buf), m) > 0){
				if((fd = create(buf, OWRITE, 0666)) < 0){
					errstr(buf, sizeof(buf));
					eenter(buf, 0, 0, m);
				} else {
					esetcursor(&reading);
					writeimage(fd, current->image, 0);
					close(fd);
					esetcursor(nil);
				}
			}
		}
		qunlock(current);
		break;
	case Cext:
		if(current == nil || !canqlock(current))
			break;
		showext(current);
		qunlock(current);
		break;
	case Cnext:
		shownext();
		break;
	case Cprev:
		showprev();
		break;
	case Czerox:
		zerox(current);
		break;
	case Cquit:
		exits(0);
	}
}

void
main(int argc, char *argv[])
{
	enum { Eplumb = 4 };
	char buf[NPATH];
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
	atnotify(catchnote, 1);
	if(cohort = rfork(RFPROC|RFNOTEG|RFNAMEG|RFREND)){
		atexit(killcohort);
		waitpid();
		exits(0);
	}
	cohort = getpid();
	atexit(killcohort);

	if(newwin > 0){
		s = smprint("-pid %d", getpid());
		if(newwindow(s) < 0)
			sysfatal("newwindow: %r");
		free(s);
	}
	if(initdraw(drawerr, nil, argv0) < 0)
		sysfatal("initdraw: %r");
	paper = display->white;
	frame = display->black;
	ground = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x777777FF);
	display->locking = 1;
	unlockdisplay(display);
	drawlock(1);

	einit(Ekeyboard|Emouse);
	eplumb(Eplumb, "image");
	memset(&m, 0, sizeof(m));
	if((nullfd = open("/dev/null", ORDWR)) < 0)
		sysfatal("open: %r");
	current = root = addpage(nil, "", nil, nil, -1);
	if(*argv == nil && !imode)
		addpage(root, "stdin", popenfile, strdup("/fd/0"), -1);
	for(; *argv; argv++)
		addpage(root, shortname(*argv), popenfile, strdup(*argv), -1);

	for(;;){
		drawlock(0);
		i=event(&e);
		drawlock(1);

		switch(i){
		case Emouse:
			m = e.mouse;
			if(m.buttons & 1){
				if(current &&  canqlock(current)){
					for(;;) {
						o = m.xy;
						m = emouse();
						if((m.buttons & 1) == 0)
							break;
						translate(current, subpt(m.xy, o));
					}
					qunlock(current);
				}
			} else if(m.buttons & 2){
				o = m.xy;
				i = emenuhit(2, &m, &cmdmenu);
				m.xy = o;
				docmd(i, &m);
			} else if(m.buttons & 4){
				if(root->down){
					Page *x;

					qlock(&pagelock);
					pagemenu.lasthit = pageindex(current);
					x = pageat(emenuhit(3, &m, &pagemenu));
					qunlock(&pagelock);
					showpage(x);
				}
			}
			break;
		case Ekeyboard:
			switch(e.kbdc){
			case Kup:
				if(current == nil || !canqlock(current))
					break;
				if(pos.y < 0){
					translate(current, Pt(0, Dy(screen->r)/2));
					qunlock(current);
					break;
				}
				if(prevpage(current))
					pos.y = 0;
				qunlock(current);
				docmd(Cprev, &m);
				break;
			case Kdown:
				if(current == nil || !canqlock(current))
					break;
				o = addpt(pos, pagesize(current));
				if(o.y > Dy(screen->r)){
					translate(current, Pt(0, -Dy(screen->r)/2));
					qunlock(current);
					break;
				}
				if(nextpage(current))
					pos.y = 0;
				qunlock(current);
				docmd(Cnext, &m);
				break;
			default:
				for(i = 0; i<nelem(cmds); i++)
					if((cmds[i].k1 == e.kbdc) ||
					   (cmds[i].k2 == e.kbdc) ||
					   (cmds[i].k3 == e.kbdc))
						break;
				if(i < nelem(cmds)){
					docmd(i, &m);
					break;
				}
				if((e.kbdc < 0x20) || 
				   (e.kbdc & 0xFF00) == KF || 
				   (e.kbdc & 0xFF00) == Spec)
					break;
				snprint(buf, sizeof(buf), "%C", (Rune)e.kbdc);
				if(eenter("Go to", buf, sizeof(buf), &m) > 0)
					showpage(findpage(buf));
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
					if((fd = createtmp("plumb")) < 0){
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
