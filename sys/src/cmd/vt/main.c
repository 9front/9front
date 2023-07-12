#include <u.h>
#include <libc.h>
#include <draw.h>

#include "cons.h"

#include <thread.h>
#include <fcall.h>
#include <9p.h>

#include <bio.h>
#include <mouse.h>
#include <keyboard.h>
#include <plumb.h>

enum menuact2{
	Mbackup,
	Mforward,
	Mreset,
	Mpaste,
	Msnarf,
	Mplumb,
	Mpage,
};

enum menuact3{
	M24x80,
	Mcrnl,
	Mnl,
	Mraw,
	Mblocksel,
	Mexit,
};

char	*menutext2[] = {
	[Mbackup]	"backup",
	[Mforward]	"forward",
	[Mreset]	"reset",
	[Mpaste]	"paste",
	[Msnarf]	"snarf",
	[Mplumb]	"plumb",
	[Mpage]		"page",
	nil
};

char	*menutext3[] = {
	[M24x80]	"24x80",
	[Mcrnl]		"crnl",
	[Mnl]		"nl",
	[Mraw]		"raw",
	[Mblocksel]	"blocksel",
	[Mexit]		"exit",
	nil
};

/* variables associated with the screen */

int	x, y;	/* character positions */
Rune	*backp;
int	backc;
int	nbacklines;
int	xmax, ymax;
int	blocked;
int	winchgen;
int	resize_flag = 1;
int	pagemode;
int	olines;
int	peekc;
int	blocksel = 0;
int	cursoron = 1;
int	chording = 0;
int	hostclosed = 0;
Menu	menu2;
Menu	menu3;
Rune	*histp;
Rune	hist[HISTSIZ];
Rune	*onscreenrbuf;
uchar	*onscreenabuf;
uchar	*onscreencbuf;

#define onscreenr(x, y) &onscreenrbuf[((y)*(xmax+2) + (x))]
#define onscreena(x, y) &onscreenabuf[((y)*(xmax+2) + (x))]
#define onscreenc(x, y) &onscreencbuf[((y)*(xmax+2) + (x))]

uchar	*screenchangebuf;
uint	scrolloff;

#define	screenchange(y)	screenchangebuf[((y)+scrolloff) % (ymax+1)]

int	yscrmin, yscrmax;
int	attr, defattr;

Rectangle selrect;

Image	*cursorsave;
Image	*bordercol;
Image	*colors[8];
Image	*hicolors[8];
Image	*red;
Image	*green;
Image	*fgcolor;
Image	*bgcolor;
Image	*fgselected;
Image	*bgselected;
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
struct ttystate ttystate[2] = { {0, 1}, {0, 0} };

Point	margin;
Point	ftsize;

Rune	kbdchar;

#define	button(num)	(mc->buttons == (1<<((num)-1)))

Mousectl	*mc;
Keyboardctl	*kc;
Channel		*hc[2];
Consstate	cs[1];

int	nocolor;
int	logfd = -1;
int	hostpid = -1;
Biobuf	*snarffp = 0;
Rune	*hostbuf, *hostbufp;
char	*hostin;
char	echo_input[BSIZE];
char	*echop = echo_input;		/* characters to echo, after canon */
char	sendbuf[BSIZE];	/* hope you can't type ahead more than BSIZE chars */
char	*sendbufp = sendbuf;

char *term;
struct funckey *fk, *appfk;

/* functions */
int	input(void);
int	waitchar(void);
void	waitio(void);
int	rcvchar(void);
void	bigscroll(void);
void	readmenu(void);
void	selecting(void);
int	selected(int, int);
void	resized(void);
void	drawcursor(void);
void	send_interrupt(void);
int	alnum(int);
void	escapedump(int,uchar *,int);
void	paste(void);
void	snarfsel(void);
void	plumbsel(void);

static Channel *pidchan;

static void
runcmd(void *args)
{
	char **argv = args;
	char *cmd;

	rfork(RFNAMEG);
	mountcons();

	rfork(RFFDG);
	close(0);
	open("/dev/cons", OREAD);
	close(1);
	open("/dev/cons", OWRITE);
	dup(1, 2);

	cmd = nil;
	while(*argv != nil){
		if(cmd == nil)
			cmd = strdup(*argv);
		else
			cmd = smprint("%s %q", cmd, *argv);
		argv++;
	}

	procexecl(pidchan, "/bin/rc", "rcX", cmd == nil ? nil : "-c", cmd, nil);
	sysfatal("%r");
}

void
send_interrupt(void)
{
	if(hostpid > 0)
		postnote(PNGROUP, hostpid, "interrupt");
}

void
sendnchars(int n, char *p)
{
	if((n = utfnlen(p, n)) < 1)
		return;
	hostin = smprint("%.*s", n, p);
	while(hostin != nil){
		if(nbsendp(hc[0], hostin)){
			hostin = nil;
			break;
		}
		drawcursor();
		waitio();
		if(resize_flag)
			resized();
	}
}

static void
shutdown(void)
{
	send_interrupt();
	threadexitsall(nil);
}

static void
catch(void*, char*)
{
	shutdown();
}

void
usage(void)
{
	fprint(2, "usage: %s [-2abcrx] [-f font] [-l logfile] [cmd...]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	int rflag;
	int i, blkbg;
	char *fontname, *p;

	fontname = nil;
	fk = ansifk;
	term = "vt100";
	blkbg = 0;
	rflag = 0;
	attr = defattr;
	ARGBEGIN{
	case '2':
		fk = vt220fk;
		term = "vt220";
		break;
	case 'a':
		term = "ansi";
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

	if(rfork(RFENVG) < 0)
		sysfatal("rfork: %r");
	doquote = needsrcquote;
	quotefmtinstall();
	notify(catch);
	atexit(shutdown);

	if(initdraw(0, fontname, term) < 0)
		sysfatal("inidraw failed: %r");
	if((mc = initmouse("/dev/mouse", screen)) == nil)
		sysfatal("initmouse failed: %r");
	if((kc = initkeyboard("/dev/cons")) == nil)
		sysfatal("initkeyboard failed: %r");

	hc[0] = chancreate(sizeof(char*), 256);	/* input to host */
	hc[1] = chancreate(sizeof(Rune*), 256);	/* output from host */

	cs->raw = rflag;

	histp = hist;
	menu2.item = menutext2;
	menu3.item = menutext3;
	pagemode = 0;
	blocked = 0;
	ftsize.y = font->height;
	ftsize.x = stringwidth(font, "m");

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
	bgselected = allocimage(display, Rect(0,0,1,1), CMAP8, 1, blkbg ? 0x333333FF : 0xCCCCCCFF);
	fgselected = allocimage(display, Rect(0,0,1,1), CMAP8, 1, blkbg ? 0xCCCCCCFF : 0x333333FF);
	resized();

	pidchan = chancreate(sizeof(int), 0);
	proccreate(runcmd, argv, 16*1024);
	hostpid = recvul(pidchan);

	emulate();
}

Image*
bgcol(int a, int c, int sel)
{
	if(sel)
		return bgselected;
	if(nocolor || (c & (1<<0)) == 0){
		if(a & TReverse)
			return fgcolor;
		return bgcolor;
	}
	if((a & TReverse) != 0)
		c >>= 4;
	return colors[(c>>1)&7];
}

Image*
fgcol(int a, int c, int sel)
{
	if(sel)
		return fgselected;
	if(nocolor || (c & (1<<4)) == 0){
		if(a & TReverse)
			return bgcolor;
		return fgcolor;
	}
	if((a & TReverse) == 0)
		c >>= 4;
	if(a & THighIntensity)
		return hicolors[(c>>1)&7];
	return colors[(c>>1)&7];
}

void
hidecursor(void)
{
	if(cursorsave == nil)
		return;
	draw(screen, cursorsave->r, cursorsave, nil, cursorsave->r.min);
	freeimage(cursorsave);
	cursorsave = nil;
}

void
drawscreen(void)
{
	int x, y, n;
	uchar *ap, *cp;
	Image *c;
	Rune *rp;
	Point p, q;

	hidecursor();
	
	if(scrolloff && scrolloff <= ymax)
		draw(screen, Rpt(pt(0,0), pt(xmax+2, ymax+1-scrolloff)),
			screen, nil, pt(0, scrolloff));

	for(y = 0; y <= ymax; y++){
		if(!screenchange(y))
			continue;
		screenchange(y) = 0;

		for(x = 0; x <= xmax; x += n){
			cp = onscreenc(x, y);
			ap = onscreena(x, y);
			c = bgcol(*ap, *cp, selected(x, y));
			for(n = 1; x+n <= xmax && bgcol(ap[n], cp[n], selected(x + n, y)) == c; n++)
				;
			draw(screen, Rpt(pt(x, y), pt(x+n, y+1)), c, nil, ZP);
		}
		draw(screen, Rpt(pt(x, y), pt(x+1, y+1)), bgcolor, nil, ZP);

		for(x = 0; x <= xmax; x += n){
			rp = onscreenr(x, y);
			if(*rp == 0){
				n = 1;
				continue;
			}
			ap = onscreena(x, y);
			cp = onscreenc(x, y);
			c = fgcol(*ap, *cp, selected(x, y));
			for(n = 1; x+n <= xmax && rp[n] != 0 && fgcol(ap[n], cp[n], selected(x + n, y)) == c
			&& ((ap[n] ^ *ap) & TUnderline) == 0; n++)
				;
			p = pt(x, y);
			q = runestringn(screen, p, c, ZP, font, rp, n);
			if(*ap & TUnderline){
				p.y += font->ascent+1;
				q.y += font->ascent+2;
				draw(screen, Rpt(p, q), c, nil, ZP);
			}
		}
		if(*onscreenr(x, y) == 0)
			runestringn(screen, pt(x, y),
				bordercol,
				ZP, font, L">", 1);
	}

	scrolloff = 0;
}

void
drawcursor(void)
{
	Image *col;
	Rectangle r;

	hidecursor();
	if(cursoron == 0)
		return;

	col = (hostin != nil || blocked || hostclosed) ? red : bordercol;
	r = Rpt(pt(x, y), pt(x+1, y+1));

	cursorsave = allocimage(display, r, screen->chan, 0, DNofill);
	draw(cursorsave, r, screen, nil, r.min);

	border(screen, r, 2, col, ZP);
	
}

void
clear(int x1, int y1, int x2, int y2)
{
	int c = (attr & 0x0F00)>>8; /* bgcolor */

	if(y1 < 0 || y1 > ymax || x1 < 0 || x1 > xmax || y2 <= y1 || x2 <= x1)
		return;
	
	while(y1 < y2){
		screenchange(y1) = 1;
		if(x1 < x2){
			memset(onscreenr(x1, y1), 0, (x2-x1)*sizeof(Rune));
			memset(onscreena(x1, y1), 0, x2-x1);
			memset(onscreenc(x1, y1), c, x2-x1);
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
		if(pagemode && olines >= yscrmax){
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
			if(c && nbacklines >= 0){
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

char*
lookfk(struct funckey *fk, char *name)
{
	int i;

	for(i=0; fk[i].name; i++){
		if(strcmp(name, fk[i].name)==0)
			return fk[i].sequence;
	}
	return nil;
}

int
sendfk(char *name)
{
	char *s = lookfk(appfk != nil ? appfk : fk, name);
	if(s == nil && appfk != nil)
		s = lookfk(fk, name);
	if(s != nil){
		sendnchars(strlen(s), s);
		return 1;
	}
	return 0;
}

int
input(void)
{
	static char echobuf[4*BSIZE];
	static int pasting;

Again:
	if(resize_flag)
		resized();
	if(backp)
		return(0);
	if(snarffp) {
		int c;

		if(bracketed && !pasting){
			sendnchars(6, "\033[200~");
			pasting = 1;
		}
		if((c = Bgetrune(snarffp)) < 0) {
			Bterm(snarffp);
			snarffp = nil;
			if(bracketed){
				sendnchars(6, "\033[201~");
				pasting = 0;
			}
			goto Again;
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
			case Kins:
				if(!sendfk("insert"))
					goto Send;
				break;
			case Kdel:
				if(!sendfk("delete"))
					goto Send;
				break;
			case Khome:
				if(!sendfk("home"))
					goto Send;
				break;
			case Kend:
				if(!sendfk("end"))
					goto Send;
				break;

			case Kpgup:
				sendfk("page up");
				break;
			case Kpgdown:
				sendfk("page down");
				break;

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
			Send:
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
		goto Again;
	} else if(nbrecv(kc->c, &kbdchar))
		goto Again;
	if(!blocked){
		if(host_avail())
			return(rcvchar());
		free(hostbuf);
		hostbufp = hostbuf = nbrecvp(hc[1]);
		if(host_avail() && nrand(32))
			return(rcvchar());
	}
	return -1;
}


int
waitchar(void)
{
	int r;

	for(;;) {
		r = input();
		if(r != -1)
			return r;
		drawscreen();
		drawcursor();
		waitio();
	}
}

void
waitio(void)
{
	enum { AMOUSE, ARESIZE, AKBD, AHOSTIN, AHOSTOUT, AEND, };
	Alt a[AEND+1] = {
		{ mc->c, &mc->Mouse, CHANRCV },
		{ mc->resizec, nil, CHANRCV },
		{ kc->c, &kbdchar, CHANRCV },
		{ hc[0], &hostin, CHANSND },
		{ hc[1], &hostbuf, CHANRCV },
		{ nil, nil, CHANEND },
	};
	if(kbdchar != 0)
		a[AKBD].op = CHANNOP;
	if(hostin == nil)
		a[AHOSTIN].op = CHANNOP;
	if(blocked)
		a[AHOSTOUT].op = CHANNOP;
	else if(hostbuf != nil)
		a[AHOSTOUT].op = CHANNOBLK;
Next:
	if(display->bufp > display->buf)
		flushimage(display, 1);
	switch(alt(a)){
	case AMOUSE:
		if(button(1) || chording)
			selecting();
		else if(button(2) || button(3))
			readmenu();
		else if(button(4))
			backup(backc+1);
		else if(button(5) && backc > 0)
			backup(--backc);
		else if(resize_flag == 0)
			goto Next;
		break;
	case ARESIZE:
		resize_flag = 2;
		break;
	case AHOSTIN:
		hostin = nil;
		break;
	case AHOSTOUT:
		hostbufp = hostbuf;
		if(hostbuf == nil)
			hostclosed = 1;
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
	putenvint("WINCH", ++winchgen);
	putenvint("XPIXELS", (xmax+1)*ftsize.x);
	putenvint("YPIXELS", (ymax+1)*ftsize.y);
	putenvint("LINES", ymax+1);
	putenvint("COLS", xmax+1);
	putenv("TERM", term);
	if(cs->winch)
		send_interrupt();
}

void
setdim(int ht, int wid)
{
	char tmp[128];
	int n, fd;
 
	if(wid > 0) xmax = wid-1;
	if(ht > 0) ymax = ht-1;

	x = 0;
	y = 0;
	yscrmin = 0;
	yscrmax = ymax;
	olines = 0;

	margin.x = (Dx(screen->r) - (xmax+1)*ftsize.x) / 2;
	margin.y = (Dy(screen->r) - (ymax+1)*ftsize.y) / 2;

	free(screenchangebuf);
	screenchangebuf = emalloc9p(ymax+1);
	scrolloff = 0;
	selrect = ZR;

	free(onscreenrbuf);
	onscreenrbuf = emalloc9p((ymax+1)*(xmax+2)*sizeof(Rune));
	free(onscreenabuf);
	onscreenabuf = emalloc9p((ymax+1)*(xmax+2));
	free(onscreencbuf);
	onscreencbuf = emalloc9p((ymax+1)*(xmax+2));
	clear(0,0,xmax+1,ymax+1);

	draw(screen, screen->r, bgcolor, nil, ZP);

	if(resize_flag || backc)
		return;

	exportsize();

	fd = open("/dev/wctl", ORDWR);
	if(fd >= 0){
		ht = (ymax+1) * ftsize.y + 2*INSET + 2*Borderwidth;
		wid = (xmax+1) * ftsize.x + ftsize.x + 2*INSET + 2*Borderwidth;
		if((n = read(fd, tmp, sizeof(tmp)-1)) < 0)
			n = 0;
		tmp[n] = 0;
		if(strstr(tmp, "hidden") == nil)
			fprint(fd, "resize -dx %d -dy %d\n", wid, ht);
		close(fd);
	}
}

void
resized(void)
{
	if(resize_flag > 1 && getwindow(display, Refnone) < 0){
		fprint(2, "can't reattach to window: %r\n");
		exits("can't reattach to window");
	}
	setdim((Dy(screen->r) - 2*INSET)/ftsize.y, (Dx(screen->r) - 2*INSET - ftsize.x)/ftsize.x);
	exportsize();
	if(resize_flag > 1)
		backup(backc);
	resize_flag = 0;
	werrstr("");		/* clear spurious error messages */
}

char*
selrange(char *d, int x0, int y0, int x1, int y1)
{
	Rune *s, *e;
	int z, p;

	s = onscreenr(x0, y0);
	e = onscreenr(x1, y1);
	for(z = p = 0; s < e; s++){
		if(*s){
			if(*s == '\n')
				z = p = 0;
			else if(p++ == 0){
				while(z-- > 0) *d++ = ' ';
			}
			d += runetochar(d, s);
		} else {
			z++;
		}
	}
	return d;
}

char*
selection(void)
{
	char *s, *p;
	int y;

	/* generous, but we can spare a few bytes for a few microseconds */
	s = p = malloc(UTFmax*(xmax+1)*(Dy(selrect)+1)+1);
	if(s == nil)
		return nil;
	if(blocksel){
		for(y = selrect.min.y; y <= selrect.max.y; y++){
			p = selrange(p, selrect.min.x, y, selrect.max.x, y);
			*p++ = '\n';
		}
	} else {
		p = selrange(p, selrect.min.x, selrect.min.y, selrect.max.x, selrect.max.y);
	}
	*p = 0;
	return s;
}

void
snarfsel(void)
{
	Biobuf *b;
	char *s;

	if((s = selection()) == nil)
		return;
	if((b = Bopen("/dev/snarf", OWRITE|OTRUNC)) == nil){
		free(s);
		return;
	}
	Bprint(b, "%s", s);
	Bterm(b);
	free(s);
}

void
plumbsel(void)
{
	char *s, *e, wdir[WDIR];
	Plumbmsg msg;
	int plumb;

	s = selection();
	if(s == nil || *s == 0)
		return;
	memset(&msg, 0, sizeof(msg));
	msg.src = "vt";
	msg.type = "text";
	msg.ndata = strlen(s);
	msg.data = s;
	msg.wdir = wdir;
	if(*osc7cwd != 0){
		strcpy(wdir, osc7cwd);
		/* absolute path? adjust wdir and path */
		if(*s == '/'){
			if((e = strchr(wdir+3, '/')) != nil){
				*e = 0;
				msg.data++;
				msg.ndata--;
			}
		}
	}else if(getwd(wdir, sizeof wdir) == nil){
		free(s);
		return;
	}
	if((plumb = plumbopen("send", OWRITE)) < 0){
		free(s);
		return;
	}
	plumbsend(plumb, &msg);
	close(plumb);
	free(s);
}

void
paste(void)
{
	if(snarffp == nil)
		snarffp = Bopen("/dev/snarf",OREAD);
}

int
isalnum(Rune c)
{
	/*
	 * Hard to get absolutely right.  Use what we know about ASCII
	 * and assume anything above the Latin control characters is
	 * potentially an alphanumeric.
	 */
	if(c <= ' ')
		return 0;
	if(0x7F<=c && c<=0xA0)
		return 0;
	if(utfrune("!\"#$%&'()*+,-./:;<=>?@[\\]^`{|}~", c))
		return 0;
	return 1;
}

int
isspace(Rune c)
{
	return c == 0 || c == ' ' || c == '\t' ||
		c == '\n' || c == '\r' || c == '\v';
}

void
unselect(void)
{
	int y;

	for(y = selrect.min.y; y <= selrect.max.y; y++)
		screenchange(y) = 1;
	selrect = ZR;
}

int
inmode(Rune r, int mode)
{
	return (mode == 1) ? isalnum(r) : r && !isspace(r);
}

/*
 * Selects different things based on mode.
 * 0: selects swept-over text.
 * 1: selects alphanumeric segment
 * 2: selects non-whitespace segment.
 */
void
select(Point p, Point q, int mode)
{
	if(onscreenr(p.x, p.y) > onscreenr(q.x, q.y)){
		select(q, p, mode);
		return;
	}
	unselect();
	if(p.y < 0 || p.y > ymax)
		return;
	if(p.y < 0){
		p.y = 0;
		if(!blocksel) p.x = 0;
	}
	if(q.y > ymax){
		q.y = ymax;
		if(!blocksel) q.x = xmax+1;
	}
	if(mode != 0 && eqpt(p, q)){
		while(p.x > 0 && inmode(*onscreenr(p.x-1, p.y), mode))
			p.x--;
		while(q.x <= xmax && inmode(*onscreenr(q.x, q.y), mode))
			q.x++;
		if(p.x != q.x)
			mode = 0;
	}
	if(p.x < 0 || mode)
		p.x = 0;
	if(q.x > xmax+1 || mode)
		q.x = xmax+1;
	selrect = Rpt(p, q);
	for(; p.y <= q.y; p.y++)
		screenchange(p.y) = 1;
}

void
selecting(void)
{
	Point p, q;
	static ulong t, mode;

	if(!chording){
		p = pos(mc->xy);
		t += mc->msec;
		mode++;
		do{
			q = pos(mc->xy);
			if(t > 200)
				mode = 0;
			if(mode > 2)
				mode = 2;
			select(p, q, mode);
			drawscreen();
			readmouse(mc);
		} while(button(1));
	}
	if(mc->buttons != chording){
		switch(mc->buttons & 0x7){
		case 0:	/* nothing */	break;
		case 3:	snarfsel();	break;
		case 5:	paste();	break;
		}
	}
	drawscreen();
	t = -mc->msec;
	chording = mc->buttons;
}

int
selected(int x, int y)
{
	int s;

	s = y >= selrect.min.y && y <= selrect.max.y;
	if (blocksel)
		s = s && x >= selrect.min.x && x < selrect.max.x;
	else{
		if(y == selrect.min.y)
			s = s && x >= selrect.min.x;
		if(y == selrect.max.y)
			s = s && x < selrect.max.x;
		if(y > selrect.min.y && y < selrect.max.y)
			s = 1;
	}
	return s;
}

void
readmenu(void)
{
	Point p;

	p = pos(mc->xy);
	if(button(3)) {
		menu3.item[1] = ttystate[cs->raw].crnl ? "cr" : "crnl";
		menu3.item[2] = ttystate[cs->raw].nlcr ? "nl" : "nlcr";
		menu3.item[3] = cs->raw ? "cooked" : "raw";
		menu3.item[4] = blocksel ? "linesel" : "blocksel";

		switch(menuhit(3, mc, &menu3, nil)) {
		case M24x80:		/* 24x80 */
			setdim(24, 80);
			backup(backc);
			return;
		case Mcrnl:		/* newline after cr? */
			ttystate[cs->raw].crnl = !ttystate[cs->raw].crnl;
			return;
		case Mnl:		/* cr after newline? */
			ttystate[cs->raw].nlcr = !ttystate[cs->raw].nlcr;
			return;
		case Mraw:		/* switch raw mode */
			cs->raw = !cs->raw;
			return;
		case Mblocksel:
			unselect();
			blocksel = !blocksel;
			return;
		case Mexit:
			exits(0);
		}
		return;
	}

	menu2.item[Mpage] = pagemode? "scroll": "page";

	switch(menuhit(2, mc, &menu2, nil)) {
	case Mbackup:		/* back up */
		backup(backc+1);
		return;

	case Mforward:		/* move forward */
		if(backc > 0)
			backup(--backc);
		return;

	case Mreset:		/* reset */
		backc = 0;
		backup(0);
		return;

	case Mpaste:		/* paste the snarf buffer */
		paste();
		return;

	case Msnarf:		/* send the snarf buffer */
		snarfsel();
		return;

	case Mplumb:
		plumbsel();
		return;

	case Mpage:		/* pause and clear at end of screen */
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
	int left, n;

	unselect();

	resize_flag = 1;
	if(count == 0 && !pagemode) {
		n = ymax;
		nbacklines = HISTSIZ;	/* make sure we scroll to the very end */
	} else{
		n = 3*(count+1)*ymax/4;
		nbacklines = ymax-1;
	}
	cp = histp;
	left = 1;
	while (n >= 0) {
		cp--;
		if(cp < hist)
			cp = &hist[HISTSIZ-1];
		if(*cp == '\0') {
			left = 0;
			break;
		}
		if(*cp == '\n')
			n--;
	}
	cp++;
	if(cp >= &hist[HISTSIZ])
		cp = hist;
	backp = cp;
	if(left)
		backc = count;
}

Point
pt(int x, int y)
{
	return addpt(screen->r.min, Pt(x*ftsize.x+margin.x,y*ftsize.y+margin.y));
}

Point
pos(Point pt)
{
	pt.x -= screen->r.min.x + margin.x;
	pt.y -= screen->r.min.y + margin.y;
	pt.x /= ftsize.x;
	pt.y /= ftsize.y;
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
shift(int x1, int y, int x2, int w)
{
	if(y < 0 || y > ymax || x1 < 0 || x2 < 0 || w <= 0)
		return;

	if(x1+w > xmax+1)
		w = xmax+1 - x1;
	if(x2+w > xmax+1)
		w = xmax+1 - x2;

	screenchange(y) = 1;
	memmove(onscreenr(x1, y), onscreenr(x2, y), w*sizeof(Rune));
	memmove(onscreena(x1, y), onscreena(x2, y), w);
	memmove(onscreenc(x1, y), onscreenc(x2, y), w);
}

void
scroll(int sy, int ly, int dy, int cy)	/* source, limit, dest, which line to clear */
{
	int n, d, i;

	if(sy < 0 || sy > ymax || dy < 0 || dy > ymax)
		return;

	n = ly - sy;
	if(sy + n > ymax+1)
		n = ymax+1 - sy;
	if(dy + n > ymax+1)
		n = ymax+1 - dy;

	d = sy - dy;
	if(n > 0 && d != 0){
		if(d > 0 && dy == 0 && n >= ymax){
			scrolloff += d;
		} else {
			for(i = 0; i < n; i++)
				screenchange(dy+i) = 1;
		}
		memmove(onscreenr(0, dy), onscreenr(0, sy), n*(xmax+2)*sizeof(Rune));
		memmove(onscreena(0, dy), onscreena(0, sy), n*(xmax+2));
		memmove(onscreenc(0, dy), onscreenc(0, sy), n*(xmax+2));
	}

	/* move selection */
	selrect.min.y -= d;
	selrect.max.y -= d;
	select(selrect.min, selrect.max, 0);
	
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
		scrolloff = 0;
		x = y = 0;
		return;
	}
	scroll(half, ymax+1, 0, ymax);
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
drawstring(Rune *str, int n)
{
	screenchange(y) = 1;
	memmove(onscreenr(x, y), str, n*sizeof(Rune));
	memset(onscreena(x, y), attr & 0xFF, n);
	memset(onscreenc(x, y), attr >> 8, n);
}
