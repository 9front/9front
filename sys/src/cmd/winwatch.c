#include <u.h>
#include <libc.h>
#include <draw.h>
#include <cursor.h>
#include <event.h>
#include <regexp.h>
#include <keyboard.h>

enum {
	VISIBLE = 1,
	CURRENT = 2,
};

typedef struct Win Win;
struct Win {
	int n;
	int dirty;
	int state;
	char *label;
	Rectangle r;
};

Reprog  *exclude  = nil;
Win *win;
int nwin;
int mwin;
int onwin;
int rows, cols;
Image *lightblue;
Image *statecol[4];

enum {
	PAD = 3,
	MARGIN = 5
};

void*
erealloc(void *v, ulong n)
{
	v = realloc(v, n);
	if(v == nil)
		sysfatal("out of memory reallocating %lud", n);
	return v;
}

void*
emalloc(ulong n)
{
	void *v;

	v = malloc(n);
	if(v == nil)
		sysfatal("out of memory allocating %lud", n);
	memset(v, 0, n);
	return v;
}

char*
estrdup(char *s)
{
	int l;
	char *t;

	if (s == nil)
		return nil;
	l = strlen(s)+1;
	t = emalloc(l);
	memcpy(t, s, l);

	return t;
}

int
readfile(char *buf, int nbuf, char *file, ...)
{
	va_list arg;
	int n, fd;

	va_start(arg, file);
	vsnprint(buf, nbuf, file, arg);
	va_end(arg);

	if((fd = open(buf, OREAD)) < 0){
		buf[0] = 0;
		return -1;
	}
	n = read(fd, buf, nbuf-1);
	close(fd);
	if(n < 0){
		buf[0] = 0;
		return -1;
	}
	buf[n] = 0;
	return n;
}

void
refreshwin(void)
{
	char label[128], wctl[128], *tok[8];
	int i, fd, n, nr, nw, state;
	static int mywinid = -1;
	Dir *pd;

	if(mywinid < 0){
		if(readfile(wctl, sizeof(wctl), "/dev/winid") > 0)
			mywinid = atoi(wctl);
	}

	if((fd = open("/dev/wsys", OREAD)) < 0)
		return;

	nw = 0;
/* i'd rather read one at a time but rio won't let me */
	while((nr=dirread(fd, &pd)) > 0){
		for(i=0; i<nr; i++){
			n = atoi(pd[i].name);
			if(n == mywinid)
				continue;
			if(readfile(label, sizeof(label), "/dev/wsys/%d/label", n) < 0)
				continue;
			if(exclude != nil && regexec(exclude,label,nil,0))
				continue;
			if(readfile(wctl, sizeof(wctl), "/dev/wsys/%d/wctl", n) <= 0)
				continue;
			if(tokenize(wctl, tok, nelem(tok)) != 6)
				continue;
			state = 0;
			if(strcmp(tok[4], "current") == 0)
				state |= CURRENT;
			if(strcmp(tok[5], "visible") == 0)
				state |= VISIBLE;
			if(nw < nwin && win[nw].n == n && win[nw].state == state && 
			   strcmp(win[nw].label, label)==0){
				nw++;
				continue;
			}
	
			if(nw < nwin){
				free(win[nw].label);
				win[nw].label = nil;
			}
			
			if(nw >= mwin){
				mwin += 8;
				win = erealloc(win, mwin*sizeof(win[0]));
			}
			win[nw].n = n;
			win[nw].label = estrdup(label);
			win[nw].state = state;
			win[nw].dirty = 1;
			win[nw].r = Rect(0,0,0,0);
			nw++;
		}
		free(pd);
	}
	while(nwin > nw)
		free(win[--nwin].label);
	nwin = nw;
	close(fd);
}

void
drawnowin(int i)
{
	Rectangle r;

	r = Rect(0,0,(Dx(screen->r)-2*MARGIN+PAD)/cols-PAD, font->height);
	r = rectaddpt(rectaddpt(r, Pt(MARGIN+(PAD+Dx(r))*(i/rows),
				MARGIN+(PAD+Dy(r))*(i%rows))), screen->r.min);
	draw(screen, insetrect(r, -1), lightblue, nil, ZP);
}

void
drawwin(int i)
{
	draw(screen, win[i].r, statecol[win[i].state], nil, ZP);
	_string(screen, addpt(win[i].r.min, Pt(2,0)), display->black, ZP,
		font, win[i].label, nil, strlen(win[i].label), 
		win[i].r, nil, ZP, SoverD);
	border(screen, win[i].r, 1, display->black, ZP);	
	win[i].dirty = 0;
}

int
geometry(void)
{
	int i, ncols, z;
	Rectangle r;

	z = 0;
	rows = (Dy(screen->r)-2*MARGIN+PAD)/(font->height+PAD);
	if(rows <= 0)
		rows = 1;
	if(rows*cols < nwin || rows*cols >= nwin*2){
		ncols = nwin <= 0 ? 1 : (nwin+rows-1)/rows;
		if(ncols != cols){
			cols = ncols;
			z = 1;
		}
	}

	r = Rect(0,0,(Dx(screen->r)-2*MARGIN+PAD)/cols-PAD, font->height);
	for(i=0; i<nwin; i++)
		win[i].r = rectaddpt(rectaddpt(r, Pt(MARGIN+(PAD+Dx(r))*(i/rows),
					MARGIN+(PAD+Dy(r))*(i%rows))), screen->r.min);

	return z;
}

void
redraw(Image *screen, int all)
{
	int i;

	all |= geometry();
	if(all)
		draw(screen, screen->r, lightblue, nil, ZP);
	for(i=0; i<nwin; i++)
		if(all || win[i].dirty)
			drawwin(i);
	if(!all)
		for(; i<onwin; i++)
			drawnowin(i);

	onwin = nwin;
}

void
eresized(int new)
{
	if(new && getwindow(display, Refmesg) < 0)
		fprint(2,"can't reattach to window");
	geometry();
	redraw(screen, 1);
}

int
label(Win w, Mouse m)
{
	char buf[512], fname[128];
	int n, fd;

	snprint(buf, sizeof(buf), "%s", w.label);
	n = eenter(nil, buf, sizeof(buf), &m);
	if(n <= 0)
		return 0;
	sprint(fname, "/dev/wsys/%d/label", w.n);
	if((fd = open(fname, OWRITE)) < 0)
		return 0;
	write(fd, buf, n);
	close(fd);
	refreshwin();
	redraw(screen, 1);
	return 1;
}

int
unhide(Win w)
{
	char buf[128];
	int fd;

	sprint(buf, "/dev/wsys/%d/wctl", w.n);
	if((fd = open(buf, OWRITE)) < 0)
		return 0;
	if(w.state == (CURRENT|VISIBLE))
		write(fd, "hide\n", 5);
	else {
		write(fd, "unhide\n", 7);
		write(fd, "top\n", 4);
		write(fd, "current\n", 8);
	}
	close(fd);
	return 1;
}

int
click(Mouse m)
{
	int i, b;

	b = m.buttons & 7;
	if(b != 2 && b != 4)
		return 0;
	for(i=0; i<nwin; i++)
		if(ptinrect(m.xy, win[i].r))
			break;
	if(i == nwin)
		return 0;
	do
		m = emouse();
	while((m.buttons & 7) == b);
	if((m.buttons & 7) || !ptinrect(m.xy, win[i].r))
		return 0;

	switch(b) {
	case 2:
		return label(win[i], m);
	case 4:
		return unhide(win[i]);
	default:
		return 0;
	}
}

void
usage(void)
{
	fprint(2, "usage: winwatch [-e exclude] [-f font]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *fontname = nil;
	int Etimer;
	Event e;
	int i;

	ARGBEGIN{
	case 'f':
		fontname = EARGF(usage());
		break;
	case 'e':
		exclude = regcomp(EARGF(usage()));
		if(exclude == nil)
			sysfatal("Bad regexp");
		break;
	default:
		usage();
	}ARGEND

	if(argc)
		usage();

	if(initdraw(0, fontname, "winwatch") < 0)
		sysfatal("initdraw: %r");
	lightblue = allocimagemix(display, DPalebluegreen, DWhite);

	statecol[0] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xCCCCCCFF);
	statecol[1] = lightblue;
	statecol[2] = lightblue;
	statecol[3] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DPalegreygreen);

	for(i=0; i<nelem(statecol); i++)
		if(statecol[i] == nil)
			sysfatal("allocimage: %r");

	refreshwin();
	redraw(screen, 1);
	einit(Emouse|Ekeyboard);
	Etimer = etimer(0, 2500);

	for(;;){
		switch(eread(Emouse|Ekeyboard|Etimer, &e)){
		case Ekeyboard:
			if(e.kbdc==Kdel || e.kbdc=='q')
				exits(0);
			break;
		case Emouse:
			if(click(e.mouse) == 0)
				continue;
			/* fall through  */
		default:	/* Etimer */
			refreshwin();
			redraw(screen, 0);
			break;
		}
	}
}
