#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>
#include <bio.h>
#include "cons.h"

char	*menutext2[] = {
	"backup",
	"forward",
	"reset",
	"clear",
	"paste",
	"page",
	0
};

char	*menutext3[] = {
	"24x80",
	"crnl",
	"nl",
	"raw",
	"exit",
	0
};

/* variables associated with the screen */

int	x, y;	/* character positions */
Rune	*backp;
int	backc;
int	atend;
int	nbacklines;
int	xmax, ymax;
int	blocked;
int	resize_flag;
int	pagemode;
int	olines;
int	peekc;
int	cursoron = 1;
Menu	menu2;
Menu	menu3;
Rune	*histp;
Rune	hist[HISTSIZ];
Rune	*onscreenrbuf;
uchar	*onscreenabuf;
uchar	*onscreencbuf;
int	yscrmin, yscrmax;
int	attr, defattr;

Image	*bordercol;
Image	*colors[8];
Image	*hicolors[8];
Image	*red;
Image	*green;
Image	*fgcolor;
Image	*bgcolor;
Image	*highlight;

uint rgbacolors[8] = {
	0x000000FF,	/* black */
	0xAA0000FF,	/* red */
	0x00AA00FF,	/* green */
	0xFF5500FF,	/* brown */
	0x0000FFFF,	/* blue */
	0xAA00AAFF,	/* purple */
	0x00AAAAFF,	/* cyan */
	0x7F7F7FFF,	/* white */
};

ulong rgbahicolors[8] = {
	0x555555FF,	/* light black aka grey */
	0xFF5555FF,	/* light red */
	0x55FF55FF,	/* light green */
	0xFFFF55FF,	/* light brown aka yellow */
	0x5555FFFF,	/* light blue */
	0xFF55FFFF,	/* light purple */
	0x55FFFFFF,	/* light cyan */
	0xFFFFFFFF,	/* light grey aka white */
};

/* terminal control */
struct ttystate ttystate[2] = { {0, 1}, {0, 1} };

int	NS;
int	CW;
int	XMARGIN;
int	YMARGIN;

Rune	kbdchar;

#define	button1()	((mc->buttons & 07)==1)
#define	button2()	((mc->buttons & 07)==2)
#define	button3()	((mc->buttons & 07)==4)

Mousectl	*mc;
Keyboardctl	*kc;
Channel		*hc;
Consstate	*cs;

int	nocolor;
int	logfd = -1;
int	hostfd = -1;
int	hostpid;
Biobuf	*snarffp = 0;
Rune	*hostbuf, *hostbufp;
char	echo_input[BSIZE];
char	*echop = echo_input;		/* characters to echo, after canon */
char	sendbuf[BSIZE];	/* hope you can't type ahead more than BSIZE chars */
char	*sendbufp = sendbuf;

char *term;
struct funckey *fk;

/* functions */
void	initialize(int, char **);
int	waitchar(void);
void	waitio(void);
int	rcvchar(void);
void	bigscroll(void);
void	readmenu(void);
void	selection(void);
void	resize(void);
void	send_interrupt(void);
int	alnum(int);
void	escapedump(int,uchar *,int);

int
start_host(void)
{
	switch((hostpid = rfork(RFPROC|RFNAMEG|RFFDG|RFNOTEG))) {
	case 0:
		close(0);
		open("/dev/cons", OREAD);
		close(1);
		open("/dev/cons", OWRITE);
		dup(1, 2);
		execl("/bin/rc","rcX",nil);
		fprint(2, "failed to start up rc: %r\n");
		_exits("rc");
	case -1:
		fprint(2,"rc startup: fork: %r\n");
		_exits("rc_fork");
	}
	return open("/mnt/cons/data", ORDWR);
}

void
send_interrupt(void)
{
	postnote(PNGROUP, hostpid, "interrupt");
}

void
hostreader(void*)
{
	char cb[BSIZE+1], *cp;
	Rune *rb, *rp;
	int n, r;

	n = 0;
	while((r = read(hostfd, cb+n, BSIZE-n)) > 0){
		n += r;
		rb = mallocz((n+1)*sizeof(Rune), 0);
		for(rp = rb, cp = cb; n > 0; n -= r, cp += r){
			if(!fullrune(cp, n))
				break;
			r = chartorune(rp, cp);
			if(*rp != 0)
				rp++;
		}
		if(rp > rb){
			*rp = 0;
			sendp(hc, rb);
		} else {
			free(rb);
		}
		if(n > 0) memmove(cb, cp, n);
	}
	sendp(hc, nil);
}

static void
shutdown(void)
{
	send_interrupt();
	postnote(PNGROUP, getpid(), "exit");
	threadexitsall(nil);
}

void
threadmain(int argc, char **argv)
{
	rfork(RFNAMEG|RFNOTEG|RFENVG);
	atexit(shutdown);
	initialize(argc, argv);
	emulate();
}

void
usage(void)
{
	fprint(2, "usage: %s [-2abcrx] [-f font] [-l logfile] [cmd...]\n", argv0);
	exits("usage");
}

void
initialize(int argc, char **argv)
{
	int rflag;
	int i, blkbg;
	char *fontname, *p;

	fontname = nil;
	term = "vt100";
	fk = vt100fk;
	blkbg = nocolor = 0;
	rflag = 0;
	attr = defattr;
	ARGBEGIN{
	case '2':
		term = "vt220";
		fk = vt220fk;
		break;
	case 'a':
		term = "ansi";
		fk = ansifk;
		break;
	case 'b':
		blkbg = 1;		/* e.g., for linux colored output */
		break;
	case 'c':
		nocolor = 1;
		break;
	case 'f':
		fontname = EARGF(usage());
		break;
	case 'l':
		p = EARGF(usage());
		logfd = create(p, OWRITE|OCEXEC, 0666);
		if(logfd < 0)
			sysfatal("could not create log file: %s: %r", p);
		break;
	case 'x':
		fk = xtermfk;
		term = "xterm";
		break;
	case 'r':
		rflag = 1;
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(initdraw(0, fontname, term) < 0)
		sysfatal("inidraw failed: %r");
	if((mc = initmouse("/dev/mouse", screen)) == nil)
		sysfatal("initmouse failed: %r");
	if((kc = initkeyboard("/dev/cons")) == nil)
		sysfatal("initkeyboard failed: %r");
	if((cs = consctl()) == nil)
		sysfatal("consctl failed: %r");
	cs->raw = rflag;

	histp = hist;
	menu2.item = menutext2;
	menu3.item = menutext3;
	pagemode = 0;
	blocked = 0;
	NS = font->height;
	CW = stringwidth(font, "m");

	red = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DRed);
	green = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DGreen);
	bordercol = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xCCCCCCCC);
	highlight = allocimage(display, Rect(0,0,1,1), CHAN1(CAlpha,8), 1, 0x80);

	for(i=0; i<8; i++){
		colors[i] = allocimage(display, Rect(0,0,1,1), screen->chan, 1,
			rgbacolors[i]);
		hicolors[i] = allocimage(display, Rect(0,0,1,1), screen->chan, 1,
			rgbahicolors[i]);
	}
	bgcolor = (blkbg? display->black: display->white);
	fgcolor = (blkbg? display->white: display->black);
	resize();

	hc = chancreate(sizeof(Rune*), 5);
	if((hostfd = start_host()) >= 0)
		proccreate(hostreader, nil, BSIZE+1024);

	while(*argv != nil){
		sendnchars(strlen(*argv), *argv);
		if(argv[1] == nil){
			sendnchars(1, "\n");
			break;
		}
		sendnchars(1, " ");
		argv++;
	}
}

#define onscreenr(x, y) &onscreenrbuf[((y)*(xmax+2) + (x))]
#define onscreena(x, y) &onscreenabuf[((y)*(xmax+2) + (x))]
#define onscreenc(x, y) &onscreencbuf[((y)*(xmax+2) + (x))]

#define bgcol(a, c) (((a)&TReverse)!=0 ? (c)>>4 : (c&15))
#define fgcol(a, c) ((((a)&TReverse)==0 ? (c)>>4 : (c&15)) | (((a)&THighIntensity)!=0)<<4)

void
drawscreen(void)
{
	int x, y, n;
	uchar c, *ap, *cp;
	Rune *rp;
	Point p;

	/* draw background */
	draw(screen, screen->r, bgcolor, nil, ZP);
	for(y = 0; y <= ymax; y++){
		for(x = 0; x <= xmax; x += n){
			cp = onscreenc(x, y);
			if((*cp & 1) == 0){
				n = 1;
				continue;
			}
			ap = onscreena(x, y);
			c = bgcol(*ap, *cp);
			for(n = 1; x+n <= xmax && bgcol(ap[n], cp[n]) == c; n++)
				;
			p = pt(x, y);
			draw(screen, Rpt(p, Pt(p.x+CW, p.y+NS)), colors[c>>1], nil, ZP);
		}
	}

	/* draw foreground */
	for(y = 0; y <= ymax; y++){
		for(x = 0; x <= xmax; x += n){
			rp = onscreenr(x, y);
			if(*rp == 0){
				n = 1;
				continue;
			}
			ap = onscreena(x, y);
			cp = onscreenc(x, y);
			c = fgcol(*ap, *cp);
			for(n = 1; x+n <= xmax && rp[n] != 0 && fgcol(ap[n], cp[n]) == c; n++)
				;
			runestringn(screen, pt(x, y),
				(c&1) ? (((c&16) ? hicolors : colors)[(c&15)>>1]) : fgcolor,
				ZP, font, rp, n);
		}
		if(*onscreenr(x, y) == 0)
			runestringn(screen, pt(x, y),
				bordercol,
				ZP, font, L">", 1);
	}
}

void
drawcursor(void)
{
	Image *col = (blocked || hostfd < 0) ? red : bordercol;
	border(screen, Rpt(pt(x, y), pt(x+1, y+1)), 2, col, ZP);
}

void
clear(int x1, int y1, int x2, int y2)
{
	while(y1 < y2){
		if(x1 < x2){
			memset(onscreenr(x1, y1), 0, (x2-x1)*sizeof(Rune));
			memset(onscreena(x1, y1), 0, x2-x1);
			memset(onscreenc(x1, y1), 0, x2-x1);
		}
		if(x2 > xmax)
			*onscreenr(xmax+1, y1) = '\n';
		y1++;
	}
}

void
newline(void)
{
	if(x > xmax)
		*onscreenr(xmax+1, y) = 0;	/* wrap arround, remove hidden newline */
	nbacklines--;
	if(y >= yscrmax) {
		y = yscrmax;
		if(pagemode && olines >= yscrmax) {
			blocked = 1;
			return;
		}
		scroll(yscrmin+1, yscrmax+1, yscrmin, yscrmax);
	} else
		y++;
	olines++;
}

int
get_next_char(void)
{
	int c = peekc;

	peekc = 0;
	if(c > 0)
		return(c);
	while(c <= 0) {
		if(backp) {
			c = *backp;
			if(c && nbacklines >= 0) {
				backp++;
				if(backp >= &hist[HISTSIZ])
					backp = hist;
				return(c);
			}
			backp = 0;
		}
		c = waitchar();
		if(c > 0 && logfd >= 0)
			fprint(logfd, "%C", (Rune)c);
	}
	*histp++ = c;
	if(histp >= &hist[HISTSIZ])
		histp = hist;
	*histp = '\0';
	return(c);
}

char*
backrune(char *start, char *cp)
{
	char *ep;

	ep = cp;
	cp -= UTFmax;
	if(cp < start)
		cp = start;
	while(cp < ep){
		Rune r;
		int n;

		n = chartorune(&r, cp);
		if(cp + n >= ep)
			break;
		cp += n;
	}
	return cp;
}

int
canon(char *ep, Rune c)
{
	switch(c) {
	case Kdown:
	case Kpgdown:
		return SCROLL;
	case '\b':
		if(sendbufp > sendbuf){
			sendbufp = backrune(sendbuf, sendbufp);
			*ep++ = '\b';
			*ep++ = ' ';
			*ep++ = '\b';
		}
		break;
	case 0x15:	/* ^U line kill */
		sendbufp = sendbuf;
		*ep++ = '^';
		*ep++ = 'U';
		*ep++ = '\n';
		break;
	case 0x17:	/* ^W word kill */
		while(sendbufp > sendbuf && !alnum(*sendbufp)) {
			sendbufp = backrune(sendbuf, sendbufp);
			*ep++ = '\b';
			*ep++ = ' ';
			*ep++ = '\b';
		}
		while(sendbufp > sendbuf && alnum(*sendbufp)) {
			sendbufp = backrune(sendbuf, sendbufp);
			*ep++ = '\b';
			*ep++ = ' ';
			*ep++ = '\b';
		}
		break;
	case '\177':	/* interrupt */
		sendbufp = sendbuf;
		send_interrupt();
		return(NEWLINE);
	case '\021':	/* quit */
	case '\r':
	case '\n':
		if(sendbufp < &sendbuf[BSIZE])
			*sendbufp++ = '\n';
		sendnchars((int)(sendbufp-sendbuf), sendbuf);
		sendbufp = sendbuf;
		if(c == '\n' || c == '\r')
			*ep++ = '\n';
		*ep = 0;
		return(NEWLINE);
	case '\004':	/* EOT */
		if(sendbufp == sendbuf) {
			sendnchars(0,sendbuf);
			*ep = 0;
			return(NEWLINE);
		}
		/* fall through */
	default:
		if(sendbufp < &sendbuf[BSIZE-UTFmax])
			sendbufp += runetochar(sendbufp, &c);
		ep += runetochar(ep, &c);
		break;
	}
	*ep = 0;
	return(OTHER);
}

void
sendfk(char *name)
{
	int i;
	static int fd;

	for(i=0; fk[i].name; i++)
		if(strcmp(name, fk[i].name)==0){
			sendnchars(strlen(fk[i].sequence), fk[i].sequence);
			return;
		}
}

int
waitchar(void)
{
	static char echobuf[4*BSIZE];

	for(;;) {
		if(resize_flag)
			resize();
		if(backp)
			return(0);
		if(snarffp) {
			int c;

			if((c = Bgetrune(snarffp)) < 0) {
				Bterm(snarffp);
				snarffp = nil;
				continue;
			}
			kbdchar = c;
		}
		if(kbdchar) {
			if(backc){
				backc = 0;
				backup(backc);
			}
			if(blocked)
				resize_flag = 1;
			if(cs->raw) {
				switch(kbdchar){
				case Kup:
					sendfk("up key");
					break;
				case Kdown:
					sendfk("down key");
					break;
				case Kleft:
					sendfk("left key");
					break;
				case Kright:
					sendfk("right key");
					break;
				case Kpgup:
					sendfk("page up");
					break;
				case Kpgdown:
					sendfk("page down");
					break;
				case KF|1:
					sendfk("F1");
					break;
				case KF|2:
					sendfk("F2");
					break;
				case KF|3:
					sendfk("F3");
					break;
				case KF|4:
					sendfk("F4");
					break;
				case KF|5:
					sendfk("F5");
					break;
				case KF|6:
					sendfk("F6");
					break;
				case KF|7:
					sendfk("F7");
					break;
				case KF|8:
					sendfk("F8");
					break;
				case KF|9:
					sendfk("F9");
					break;
				case KF|10:
					sendfk("F10");
					break;
				case KF|11:
					sendfk("F11");
					break;
				case KF|12:
					sendfk("F12");
					break;
				case '\n':
					echobuf[0] = '\r';
					sendnchars(1, echobuf);
					break;
				case '\r':
					echobuf[0] = '\n';
					sendnchars(1, echobuf);
					break;
				default:
					sendnchars(runetochar(echobuf, &kbdchar), echobuf);
					break;
				}
			} else {
				switch(canon(echobuf, kbdchar)){
				case SCROLL:
					if(!blocked)
						bigscroll();
					break;
				default:
					strcat(echo_input,echobuf);
				}
			}
			blocked = 0;
			kbdchar = 0;
			continue;
		}
		if(!blocked){
			if(host_avail())
				return(rcvchar());
			free(hostbuf);
			hostbufp = hostbuf = nbrecvp(hc);
			if(host_avail())
				return(rcvchar());
		}
		drawscreen();
		drawcursor();
		waitio();
	}
}

void
waitio(void)
{
	enum { AMOUSE, ARESIZE, AKBD, AHOST, AEND, };
	Alt a[AEND+1] = {
		{ mc->c, &mc->Mouse, CHANRCV },
		{ mc->resizec, nil, CHANRCV },
		{ kc->c, &kbdchar, CHANRCV },
		{ hc, &hostbuf, CHANNOP },
		{ nil, nil, CHANEND },
	};
	if(hostbuf == nil) a[AHOST].op = CHANRCV;
Next:
	if(display->bufp > display->buf)
		flushimage(display, 1);
	switch(alt(a)){
	case AMOUSE:
		if(button1())
			selection();
		else if(button2() || button3())
			readmenu();
		else if(resize_flag == 0)
			goto Next;
		break;
	case ARESIZE:
		resize_flag = 2;
		break;
	case AHOST:
		hostbufp = hostbuf;
		if(hostbuf == nil){
			close(hostfd);
			hostfd = -1;
		}
		break;
	}
}

void
putenvint(char *name, int x)
{
	char buf[20];

	snprint(buf, sizeof buf, "%d", x);
	putenv(name, buf);
}

void
exportsize(void)
{
	putenvint("XPIXELS", (xmax+1)*CW);
	putenvint("YPIXELS", (ymax+1)*NS);
	putenvint("LINES", ymax+1);
	putenvint("COLS", xmax+1);
	putenv("TERM", term);
}

void
resize(void)
{
	if(resize_flag > 1 && getwindow(display, Refnone) < 0){
		fprint(2, "can't reattach to window: %r\n");
		exits("can't reattach to window");
	}
	xmax = (Dx(screen->r) - 2*INSET)/CW-1;
	ymax = (Dy(screen->r) - 2*INSET)/NS-1;
	XMARGIN = (Dx(screen->r) - (xmax+1)*CW) / 2;
	YMARGIN = (Dy(screen->r) - (ymax+1)*NS) / 2;
	x = 0;
	y = 0;
	yscrmin = 0;
	yscrmax = ymax;
	olines = 0;
	exportsize();

	free(onscreenrbuf);
	onscreenrbuf = mallocz((ymax+1)*(xmax+2)*sizeof(Rune), 1);
	free(onscreenabuf);
	onscreenabuf = mallocz((ymax+1)*(xmax+2), 1);
	free(onscreencbuf);
	onscreencbuf = mallocz((ymax+1)*(xmax+2), 1);

	clear(0,0,xmax+1,ymax+1);
	if(resize_flag > 1)
		backup(backc);
	resize_flag = 0;
	werrstr("");		/* clear spurious error messages */
}

void
setdim(int ht, int wid)
{
	Rectangle r;
	int fd;

	if(wid <= 0) wid = xmax+1;
	if(ht <= 0) ht = ymax+1;
	r.min = screen->r.min;
	r.max = addpt(screen->r.min, Pt(wid*CW+2*INSET, ht*NS+2*INSET));
	fd = open("/dev/wctl", OWRITE);
	if(fd >= 0){
		fprint(fd, "resize -dx %d -dy %d\n", Dx(r)+2*Borderwidth, Dy(r)+2*Borderwidth);
		close(fd);
	}
}

void
sendsnarf(void)
{
	if(snarffp == nil)
		snarffp = Bopen("/dev/snarf",OREAD);
}

int
writesnarf(Rune *s, Rune *e)
{
	Biobuf *b;
	int z, p;

	if(s >= e)
		return 0;
	b = Bopen("/dev/snarf", OWRITE|OTRUNC);
	if(b == nil)
		return 0;
	for(z = p = 0; s < e; s++){
		if(*s){
			if(*s == '\n')
				z = p = 0;
			else if(p++ == 0){
				while(z-- > 0) Bputc(b, ' ');
			}
			Bputrune(b, *s);
		} else {
			z++;
		}
	}
	Bterm(b);
	return 1;
}

Rectangle
drawselection(Rectangle r, Rectangle d, Image *color)
{
	while(r.min.y < r.max.y){
		d = drawselection(Rect(r.min.x, r.min.y, xmax+1, r.min.y), d, color);
		r.min.x = 0;
		r.min.y++;
	}
	if(r.min.x >= r.max.x)
		return d;
	r = Rpt(pt(r.min.x, r.min.y), pt(r.max.x, r.max.y+1));
	draw(screen, r, color, highlight, r.min);
	combinerect(&d, r);
	return d;
}

void
selection(void)
{
	Point p, q;
	Rectangle r, d;
	Image *backup;

	backup = allocimage(display, screen->r, screen->chan, 0, DNofill);
	draw(backup, backup->r, screen, nil, backup->r.min);
	p = pos(mc->xy);
	do {
		q = pos(mc->xy);
		if(onscreenr(p.x, p.y) > onscreenr(q.x, q.y)){
			r.min = q;
			r.max = p;
		} else {
			r.min = p;
			r.max = q;
		}
		if(r.max.y > ymax)
			r.max.x = 0;
		d = drawselection(r, ZR, red);
		flushimage(display, 1);
		readmouse(mc);
		draw(screen, d, backup, nil, d.min);
	} while(button1());
	if((mc->buttons & 07) == 5)
		sendsnarf();
	else if(writesnarf(onscreenr(r.min.x, r.min.y), onscreenr(r.max.x, r.max.y))){
		d = drawselection(r, ZR, green);
		flushimage(display, 1);
		sleep(200);
		draw(screen, d, backup, nil, d.min);
	}
	freeimage(backup);
}

void
readmenu(void)
{
	if(button3()) {
		menu3.item[1] = ttystate[cs->raw].crnl ? "cr" : "crnl";
		menu3.item[2] = ttystate[cs->raw].nlcr ? "nl" : "nlcr";
		menu3.item[3] = cs->raw ? "cooked" : "raw";

		switch(menuhit(3, mc, &menu3, nil)) {
		case 0:		/* 24x80 */
			setdim(24, 80);
			return;
		case 1:		/* newline after cr? */
			ttystate[cs->raw].crnl = !ttystate[cs->raw].crnl;
			return;
		case 2:		/* cr after newline? */
			ttystate[cs->raw].nlcr = !ttystate[cs->raw].nlcr;
			return;
		case 3:		/* switch raw mode */
			cs->raw = !cs->raw;
			return;
		case 4:
			exits(0);
		}
		return;
	}

	menu2.item[5] = pagemode? "scroll": "page";

	switch(menuhit(2, mc, &menu2, nil)) {

	case 0:		/* back up */
		if(atend == 0) {
			backc++;
			backup(backc);
		}
		return;

	case 1:		/* move forward */
		backc--;
		if(backc >= 0)
			backup(backc);
		else
			backc = 0;
		return;

	case 2:		/* reset */
		backc = 0;
		backup(0);
		return;

	case 3:		/* clear screen */
		resize_flag = 1;
		return;

	case 4:		/* send the snarf buffer */
		sendsnarf();
		return;

	case 5:		/* pause and clear at end of screen */
		pagemode = 1-pagemode;
		if(blocked && !pagemode) {
			resize_flag = 1;
			blocked = 0;
		}
		return;
	}
}

void
backup(int count)
{
	Rune *cp;
	int n;

	resize_flag = 1;
	if(count == 0 && !pagemode) {
		n = ymax;
		nbacklines = HISTSIZ;	/* make sure we scroll to the very end */
	} else{
		n = 3*(count+1)*ymax/4;
		nbacklines = ymax-1;
	}
	cp = histp;
	atend = 0;
	while (n >= 0) {
		cp--;
		if(cp < hist)
			cp = &hist[HISTSIZ-1];
		if(*cp == '\0') {
			atend = 1;
			break;
		}
		if(*cp == '\n')
			n--;
	}
	cp++;
	if(cp >= &hist[HISTSIZ])
		cp = hist;
	backp = cp;
}

Point
pt(int x, int y)
{
	return addpt(screen->r.min, Pt(x*CW+XMARGIN,y*NS+YMARGIN));
}

Point
pos(Point pt)
{
	pt.x -= screen->r.min.x + XMARGIN;
	pt.y -= screen->r.min.y + YMARGIN;
	pt.x /= CW;
	pt.y /= NS;
	if(pt.x < 0)
		pt.x = 0;
	else if(pt.x > xmax+1)
		pt.x = xmax+1;
	if(pt.y < 0)
		pt.y = 0;
	else if(pt.y > ymax+1)
		pt.y = ymax+1;
	return pt;
}

void
scroll(int sy, int ly, int dy, int cy)	/* source, limit, dest, which line to clear */
{
	memmove(onscreenr(0, dy), onscreenr(0, sy), (ly-sy)*(xmax+2)*sizeof(Rune));
	memmove(onscreena(0, dy), onscreena(0, sy), (ly-sy)*(xmax+2));
	memmove(onscreenc(0, dy), onscreenc(0, sy), (ly-sy)*(xmax+2));
	clear(0, cy, xmax+1, cy+1);
}

void
bigscroll(void)			/* scroll up half a page */
{
	int half = ymax/3;

	if(x == 0 && y == 0)
		return;
	if(y < half) {
		clear(0, 0, xmax+1, ymax+1);
		x = y = 0;
		return;
	}
	memmove(onscreenr(0, 0), onscreenr(0, half), (ymax-half+1)*(xmax+2)*sizeof(Rune));
	memmove(onscreena(0, 0), onscreena(0, half), (ymax-half+1)*(xmax+2));
	memmove(onscreenc(0, 0), onscreenc(0, half), (ymax-half+1)*(xmax+2));
	clear(0, y-half+1, xmax+1, ymax+1);
	y -= half;
	if(olines)
		olines -= half;
}

int
number(Rune *p, int *got)
{
	int c, n = 0;

	if(got)
		*got = 0;
	while ((c = get_next_char()) >= '0' && c <= '9'){
		if(got)
			*got = 1;
		n = n*10 + c - '0';
	}
	*p = c;
	return(n);
}

/* stubs */

void
sendnchars(int n,char *p)
{
	if(hostfd < 0)
		return;
	if(write(hostfd,p,n) < 0){
		close(hostfd);
		hostfd = -1;
	}
}

int
host_avail(void)
{
	if(*echop != 0 && fullrune(echop, strlen(echop)))
		return 1;
	if(hostbuf == nil)
		return 0;
	return *hostbufp != 0;
}

int
rcvchar(void)
{
	Rune r;

	if(*echop != 0) {
		echop += chartorune(&r, echop);
		if(*echop == 0) {
			echop = echo_input;	
			*echop = 0;
		}
		return r;
	}
	return *hostbufp++;
}

void
ringbell(void){
}

int
alnum(int c)
{
	if(c >= 'a' && c <= 'z')
		return 1;
	if(c >= 'A' && c <= 'Z')
		return 1;
	if(c >= '0' && c <= '9')
		return 1;
	return 0;
}

void
escapedump(int fd,uchar *str,int len)
{
	int i;

	for(i = 0; i < len; i++) {
		if((str[i] < ' ' || str[i] > '\177') && 
			str[i] != '\n' && str[i] != '\t') fprint(fd,"^%c",str[i]+64);
		else if(str[i] == '\177') fprint(fd,"^$");
		else if(str[i] == '\n') fprint(fd,"^J\n");
		else fprint(fd,"%c",str[i]);
	}
}

void
funckey(int key)
{
	if(key >= NKEYS)
		return;
	if(fk[key].name == 0)
		return;
	sendnchars(strlen(fk[key].sequence), fk[key].sequence);
}

void
drawstring(Rune *str, int n)
{
	memmove(onscreenr(x, y), str, n*sizeof(Rune));
	memset(onscreena(x, y), attr & 0xFF, n);
	memset(onscreenc(x, y), attr >> 8, n);
}
