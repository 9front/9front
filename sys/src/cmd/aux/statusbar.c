#include <u.h>
#include <libc.h>
#include <draw.h>
#include <bio.h>
#include <event.h>
#include <keyboard.h>

int newwin(char*);

int nokill;
int textmode;
char *title;

Image *light;
Image *dark;
Image *text;

void
initcolor(void)
{
	text = display->black;
	light = allocimagemix(display, DPalegreen, DWhite);
	dark = allocimage(display, Rect(0,0,1,1), CMAP8, 1, DDarkgreen);
	if(light == nil || dark == nil) sysfatal("initcolor: %r");
}

Rectangle rbar;
vlong n, d;
int last;
int lastp = -1;

char backup[80];

void
drawbar(void)
{
	int i, j;
	int p;
	char buf[400], bar[200];
	static char lastbar[200];

	if(n > d || n < 0 || d <= 0)
		return;

	i = (Dx(rbar)*n)/d;
	p = (n*100LL)/d;

	if(textmode){
		if(Dx(rbar) > 150){
			rbar.min.x = 0;
			rbar.max.x = 150;
			return;
		}
		bar[0] = '|';
		for(j=0; j<i; j++)
			bar[j+1] = '#';
		for(; j<Dx(rbar); j++)
			bar[j+1] = '-';
		bar[j++] = '|';
		bar[j++] = ' ';
		sprint(bar+j, "%3d%% ", p);
		for(i=0; bar[i]==lastbar[i] && bar[i]; i++)
			;
		memset(buf, '\b', strlen(lastbar)-i);
		strcpy(buf+strlen(lastbar)-i, bar+i);
		if(buf[0])
			write(1, buf, strlen(buf));
		strcpy(lastbar, bar);
		return;
	}

	if(lastp == p && last == i)
		return;

	if(lastp != p){
		sprint(buf, "%3d%%", p);
		
		stringbg(screen, Pt(screen->r.max.x-4-stringwidth(display->defaultfont, buf), screen->r.min.y+4), text, ZP, display->defaultfont, buf, light, ZP);
		lastp = p;
	}

	if(last != i){
		if(i > last)
			draw(screen, Rect(rbar.min.x+last, rbar.min.y, rbar.min.x+i, rbar.max.y),
				dark, nil, ZP);
		else
			draw(screen, Rect(rbar.min.x+i, rbar.min.y, rbar.min.x+last, rbar.max.y),
				light, nil, ZP);
		last = i;
	}
	flushimage(display, 1);
}

void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0)
		fprint(2,"can't reattach to window");

	draw(screen, screen->r, light, nil, ZP);
	if(title) string(screen, addpt(screen->r.min, Pt(4,4)), text, ZP, font, title);
	rbar = insetrect(screen->r, 4);
	rbar.min.y += font->height + 4;
	border(screen, rbar, -2, dark, ZP);
	last = 0;
	lastp = -1;

	drawbar();
}

void
bar(Biobuf *b)
{
	char *p, *f[2];
	Event e;
	int k, die, parent, child;

	parent = getpid();

	die = 0;
	if(textmode)
		child = -1;
	else
	switch(child = rfork(RFMEM|RFPROC)) {
	case 0:
		sleep(1000);
		while(!die && (k = eread(Ekeyboard|Emouse, &e))) {
			if(nokill==0 && k == Ekeyboard && (e.kbdc == Kdel || e.kbdc == Ketx)) {
				postnote(PNPROC, parent, "interrupt");
				_exits("interrupt");
			}
		}
		_exits(0);
	}

	while(!die && (p = Brdline(b, '\n'))) {
		p[Blinelen(b)-1] = '\0';
		if(tokenize(p, f, 2) != 2)
			continue;
		n = strtoll(f[0], 0, 0);
		d = strtoll(f[1], 0, 0);
		drawbar();
	}
	if(textmode)
		write(1, "\n", 1);
	else
		postnote(PNPROC, child, "kill");
}


void
usage(void)
{
	fprint(2, "usage: %s [-kt] [-w minx,miny,maxx,maxy] [title]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	Biobuf b;
	char *p, *q;
	int lfd;

	p = "0,0,200,60";
	
	ARGBEGIN{
	case 'w':
		p = ARGF();
		break;
	case 't':
		textmode = 1;
		break;
	case 'k':
		nokill = 1;
		break;
	default:
		usage();
	}ARGEND;

	switch(argc){
	default:
		usage();
	case 1:
		title = argv[0];
	case 0:
		break;
	}
	lfd = dup(0, -1);

	while(q = strchr(p, ','))
		*q = ' ';
	Binit(&b, lfd, OREAD);
	if(textmode || newwin(p) < 0){
		textmode = 1;
		rbar = Rect(0, 0, 60, 1);
	}else{
		if(initdraw(0, 0, title ? title : argv0) < 0)
			exits("initdraw");
		initcolor();
		einit(Emouse|Ekeyboard);
		eresized(0);
	}
	bar(&b);

	exits(0);
}

int
newwin(char *win)
{
	char spec[100];
	int cons;

	if(win != nil){
		snprint(spec, sizeof(spec), "-r %s", win);
		win = spec;
	}
	if(newwindow(win) < 0){
		fprint(2, "%s: newwindow: %r", argv0);
		return -1;
	}
	if((cons = open("/dev/cons", OREAD)) < 0){
	NoCons:
		fprint(2, "%s: can't open /dev/cons: %r", argv0);
		return -1;
	}
	dup(cons, 0);
	close(cons);
	if((cons = open("/dev/cons", OWRITE)) < 0)
		goto NoCons;
	dup(cons, 1);
	dup(cons, 2);
	close(cons);
	return 0;
}
