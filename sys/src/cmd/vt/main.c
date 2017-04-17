#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <bio.h>
#include <keyboard.h>
#include "cons.h"

enum{
	Ehost		= 4,
};

char	*menutext2[] = {
	"backup",
	"forward",
	"reset",
	"clear",
	"send",
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
Rune	*onscreen;
int	yscrmin, yscrmax;
int	attr, defattr;
int	wctlout;

Image	*bordercol;
Image	*cursback;
Image	*colors[8];
Image	*hicolors[8];
Image	*red;
Image	*green;
Image	*fgcolor;
Image	*bgcolor;
Image	*fgdefault;
Image	*bgdefault;
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
Consstate *cs;
Mouse	mouse;

int	debug;
int	nocolor;
int	logfd = -1;
int	outfd = -1;
Biobuf	*snarffp = 0;

char	*host_buf;
char	*hostp;				/* input from host */

int	host_bsize = 2*BSIZE;
int	hostlength;			/* amount of input from host */
char	echo_input[BSIZE];
char	*echop = echo_input;		/* characters to echo, after canon */
char	sendbuf[BSIZE];	/* hope you can't type ahead more than BSIZE chars */
char	*sendp = sendbuf;

char *term;
struct funckey *fk;

/* functions */
void	initialize(int, char **);
void	ebegin(int);
int	waitchar(void);
int	rcvchar(void);
void	set_input(char *);
void	set_host(Event *);
void	bigscroll(void);
void	readmenu(void);
void	selection(void);
void	eresized(int);
void	resize(void);
void	send_interrupt(void);
int	alnum(int);
void	escapedump(int,uchar *,int);
Rune*	onscreenp(int, int);

void
main(int argc, char **argv)
{
	initialize(argc, argv);
	emulate();
}

void
usage(void)
{
	fprint(2, "usage: %s [-2abcrx] [-f font] [-l logfile]\n", argv0);
	exits("usage");
}

void
initialize(int argc, char **argv)
{
	int rflag;
	int i, blkbg;
	char *fontname, *p;

	rfork(RFNAMEG|RFNOTEG|RFENVG);

	fontname = nil;
	term = "vt100";
	fk = vt100fk;
	blkbg = nocolor = 0;
	rflag = 0;
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
		logfd = create(p, OWRITE, 0666);
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

	host_buf = mallocz(host_bsize, 1);
	hostp = host_buf;
	hostlength = 0;

	if(initdraw(0, fontname, term) < 0){
		fprint(2, "%s: initdraw failed: %r\n", term);
		exits("initdraw");
	}
	werrstr("");		/* clear spurious error messages */
	ebegin(Ehost);

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
	cursback = allocimage(display, Rect(0, 0, CW+1, NS+1), screen->chan, 0, DNofill);
	highlight = allocimage(display, Rect(0,0,1,1), CHAN1(CAlpha,8), 1, 0x80);

	for(i=0; i<8; i++){
		colors[i] = allocimage(display, Rect(0,0,1,1), screen->chan, 1,
			rgbacolors[i]);
		hicolors[i] = allocimage(display, Rect(0,0,1,1), screen->chan, 1,
			rgbahicolors[i]);
	}

	bgdefault = (blkbg? display->black: display->white);
	fgdefault = (blkbg? display->white: display->black);
	bgcolor = bgdefault;
	fgcolor = fgdefault;

	resize();

	if(argc > 0) {
		sendnchars(strlen(argv[0]),argv[0]);
		sendnchars(1,"\n");
	}
}

void
clear(int x1, int y1, int x2, int y2)
{
	draw(screen, Rpt(pt(x1,y1), pt(x2,y2)), bgcolor, nil, ZP);
	while(y1 < y2){
		if(x1 < x2)
			memset(onscreenp(x1, y1), 0, (x2-x1)*sizeof(Rune));
		if(x2 > xmax)
			*onscreenp(xmax+1, y1) = '\n';
		y1++;
	}
}

void
newline(void)
{
	if(x > xmax)
		*onscreenp(xmax+1, y) = 0;	/* wrap arround, remove hidden newline */
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

void
cursoff(void)
{
	draw(screen, Rpt(pt(x, y), addpt(pt(x, y), Pt(CW,NS))), 
		cursback, nil, cursback->r.min);
}

void
curson(int bl)
{
	Image *col;

	if(!cursoron){
		cursoff();
		return;
	}

	draw(cursback, cursback->r, screen, nil, pt(x, y));
	if(bl)
		col = red;
	else
		col = bordercol;
	border(screen, Rpt(pt(x, y), addpt(pt(x, y), Pt(CW,NS))), 2, col, ZP);
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
		if(sendp > sendbuf){
			sendp = backrune(sendbuf, sendp);
			*ep++ = '\b';
			*ep++ = ' ';
			*ep++ = '\b';
		}
		break;
	case 0x15:	/* ^U line kill */
		sendp = sendbuf;
		*ep++ = '^';
		*ep++ = 'U';
		*ep++ = '\n';
		break;
	case 0x17:	/* ^W word kill */
		while(sendp > sendbuf && !alnum(*sendp)) {
			sendp = backrune(sendbuf, sendp);
			*ep++ = '\b';
			*ep++ = ' ';
			*ep++ = '\b';
		}
		while(sendp > sendbuf && alnum(*sendp)) {
			sendp = backrune(sendbuf, sendp);
			*ep++ = '\b';
			*ep++ = ' ';
			*ep++ = '\b';
		}
		break;
	case '\177':	/* interrupt */
		sendp = sendbuf;
		send_interrupt();
		return(NEWLINE);
	case '\021':	/* quit */
	case '\r':
	case '\n':
		if(sendp < &sendbuf[BSIZE])
			*sendp++ = '\n';
		sendnchars((int)(sendp-sendbuf), sendbuf);
		sendp = sendbuf;
		if(c == '\n' || c == '\r')
			*ep++ = '\n';
		*ep = 0;
		return(NEWLINE);
	case '\004':	/* EOT */
		if(sendp == sendbuf) {
			sendnchars(0,sendbuf);
			*ep = 0;
			return(NEWLINE);
		}
		/* fall through */
	default:
		if(sendp < &sendbuf[BSIZE-UTFmax])
			sendp += runetochar(sendp, &c);
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
	Event e;
	int c;
	int newmouse;
	int wasblocked;
	Rune kbdchar = 0;
	char echobuf[4*BSIZE];

	for(;;) {
		if(resize_flag)
			resize();
		wasblocked = blocked;
		if(backp)
			return(0);
		if(ecanmouse()){
			if(button1())
				selection();
			else if(button2() || button3())
				readmenu();
		}
		if(snarffp) {
			static Rune lastc = ~0;

			if((c = Bgetrune(snarffp)) < 0) {
				if(lastc != '\n')
					write(outfd,"\n",1);
				Bterm(snarffp);
				snarffp = 0;
				if(lastc != '\n') {
					lastc = ~0;
					return('\n');
				}
				lastc = ~0;
				continue;
			}
			lastc = c;
			write(outfd, echobuf, runetochar(echobuf, &lastc));
			return(c);
		}
		if(!blocked && host_avail())
			return(rcvchar());
		if(kbdchar > 0) {
			if(backc){
				backc = 0;
				backup(backc);
			}
			if(blocked)
				resize();
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
			} else if(canon(echobuf,kbdchar) == SCROLL) {
				if(!blocked)
					bigscroll();
			} else
				strcat(echo_input,echobuf);
			blocked = 0;
			kbdchar = 0;
			continue;
		}
		curson(wasblocked);	/* turn on cursor while we're waiting */
		flushimage(display, 1);
		do {
			newmouse = 0;
			switch(eread(blocked ? Emouse|Ekeyboard : 
					       Emouse|Ekeyboard|Ehost, &e)) {
			case Emouse:
				mouse = e.mouse;
				if(button1())
					selection();
				else if(button2() || button3())
					readmenu();
				else if(resize_flag == 0) {
					/* eresized() is triggered by special mouse event */
					newmouse = 1;
				}
				break;
			case Ekeyboard:
				kbdchar = e.kbdc;
				break;
			case Ehost:
				set_host(&e);
				break;
			default:
				perror("protocol violation");
				exits("protocol violation");
			}
		} while(newmouse == 1);
		cursoff();	/* turn cursor back off */
	}
}

void
eresized(int new)
{
	resize_flag = 1+new;
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
	draw(screen, screen->r, bgcolor, nil, ZP);
	xmax = (Dx(screen->r) - 2*INSET)/CW-1;
	ymax = (Dy(screen->r) - 2*INSET)/NS-1;
	XMARGIN = (Dx(screen->r) - (xmax+1)*CW) / 2;
	YMARGIN = (Dy(screen->r) - (ymax+1)*NS) / 2;
	x = 0;
	y = 0;
	yscrmin = 0;
	yscrmax = ymax;
	free(onscreen);
	onscreen = mallocz((ymax+1)*(xmax+2)*sizeof(Rune), 1);
	olines = 0;
	exportsize();
	clear(0,0,xmax+1,ymax+1);
	if(resize_flag > 1)
		backup(backc);
	resize_flag = 0;
	werrstr("");		/* clear spurious error messages */
}

void
setdim(int ht, int wid)
{
	int fd;
	Rectangle r;

	if(ht != -1)
		ymax = ht-1;
	if(wid != -1)
		xmax = wid-1;
	r.min = screen->r.min;
	r.max = addpt(screen->r.min, Pt((xmax+1)*CW+2*INSET, (ymax+1)*NS+2*INSET));
	fd = open("/dev/wctl", OWRITE);
	if(fd < 0 || fprint(fd, "resize -dx %d -dy %d\n", Dx(r)+2*Borderwidth, Dy(r)+2*Borderwidth) < 0)
		resize();
	if(fd >= 0)
		close(fd);
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
	p = pos(mouse.xy);
	do {
		q = pos(mouse.xy);
		if(onscreenp(p.x, p.y) > onscreenp(q.x, q.y)){
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
		mouse = emouse();
		draw(screen, d, backup, nil, d.min);
	} while(button1());
	if((mouse.buttons & 07) == 5)
		sendsnarf();
	else if(writesnarf(onscreenp(r.min.x, r.min.y), onscreenp(r.max.x, r.max.y))){
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

		switch(emenuhit(3, &mouse, &menu3)) {
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

	switch(emenuhit(2, &mouse, &menu2)) {

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
		eresized(0);
		return;

	case 4:		/* send the snarf buffer */
		sendsnarf();
		return;

	case 5:		/* pause and clear at end of screen */
		pagemode = 1-pagemode;
		if(blocked && !pagemode) {
			eresized(0);
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

	eresized(0);
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

Rune*
onscreenp(int x, int y)
{
	return onscreen + (y*(xmax+2) + x);
}

void
scroll(int sy, int ly, int dy, int cy)	/* source, limit, dest, which line to clear */
{
	memmove(onscreenp(0, dy), onscreenp(0, sy), (ly-sy)*(xmax+2)*sizeof(Rune));
	draw(screen, Rpt(pt(0, dy), pt(xmax+1, dy+ly-sy)), screen, nil, pt(0, sy));
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
	draw(screen, Rpt(pt(0, 0), pt(xmax+1, ymax+1)), screen, nil, pt(0, half));
	memmove(onscreenp(0, 0), onscreenp(0, half), (ymax-half+1)*(xmax+2)*sizeof(Rune));
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
	if(write(outfd,p,n) < 0) {
		close(outfd);
		close(0);
		close(1);
		close(2);
		exits("write");
	}
}

int
host_avail(void)
{
	if(*echop != 0 && fullrune(echop, strlen(echop)))
		return 1;
	if((hostp - host_buf) < hostlength)
		return fullrune(hostp, hostlength - (hostp - host_buf));
	return 0;
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
	hostp += chartorune(&r, hostp);
	return r;
}

void
set_host(Event *e)
{
	hostlength -= (hostp - host_buf);
	if(hostlength > 0)
		memmove(host_buf, hostp, hostlength);
	hostlength += e->n;
	if(hostlength >= host_bsize) {
		host_bsize = BSIZE*((hostlength + BSIZE)/BSIZE);
		host_buf = realloc(host_buf, host_bsize);
	}
	memmove(host_buf + hostlength - e->n, e->data, e->n);
	host_buf[hostlength] = 0;
	hostp = host_buf;
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
drawstring(Rune *str, int n, int attr)
{
	int i;
	Image *txt, *bg, *tmp;
	Point p;

	txt = fgcolor;
	bg = bgcolor;
	if(attr & TReverse){
		tmp = txt;
		txt = bg;
		bg = tmp;
	}
	if(attr & THighIntensity){
		for(i=0; i<8; i++)
			if(txt == colors[i])
				txt = hicolors[i];
	}
	p = pt(x, y);
	draw(screen, Rpt(p, addpt(p, runestringsize(font, str))), bg, nil, p);
	runestring(screen, p, txt, ZP, font, str);
	memmove(onscreenp(x, y), str, n*sizeof(Rune));
}
