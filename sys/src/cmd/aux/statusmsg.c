#include <u.h>
#include <libc.h>
#include <draw.h>
#include <bio.h>
#include <event.h>
#include <keyboard.h>

int newwin(char*);

int nokill;
int textmode;
char *title = nil;
char *message = nil;
Biobuf *bout;

Image *light;
Image *text;
Rectangle rtext;

void
initcolor(void)
{
	text = display->black;
	light = allocimagemix(display, DPalegreen, DWhite);
}

void
drawmsg(void)
{
	if(textmode){
		static int last = 0;

		while(last-- > 0)
			Bputc(bout, '\b');
		Bwrite(bout, message, strlen(message));
		Bflush(bout);
		last = utflen(message);
		return;
	}
	draw(screen, rtext, light, nil, ZP);
	string(screen, rtext.min, text, ZP, display->defaultfont, message);
	flushimage(display, 1);
}

void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0)
		fprint(2,"can't reattach to window");
	rtext = screen->r;
	draw(screen, rtext, light, nil, ZP);
	rtext.min.x += 4;
	rtext.min.y += 4;
	if(title){
		string(screen, rtext.min, text, ZP, display->defaultfont, title);
		rtext.min.y += 8+display->defaultfont->height;
	}
	rtext.max.y = rtext.min.y + display->defaultfont->height;
	drawmsg();
}

void
msg(Biobuf *b)
{
	char *p;
	Event e;
	int k, die, parent, child;

	parent = getpid();

	die = 0;
	if(textmode){
		child = -1;
		if(title){
			Bwrite(bout, title, strlen(title));
			Bwrite(bout, ": ", 2);
			Bflush(bout);
		}
	} else
	switch(child = rfork(RFMEM|RFPROC)) {
	case 0:
		sleep(1000);
		while(!die && (k = eread(Ekeyboard|Emouse, &e))) {
			if(nokill==0 && k == Ekeyboard && (e.kbdc == Kdel || e.kbdc == Ketx)) {
				die = 1;
				postnote(PNPROC, parent, "interrupt");
				_exits("interrupt");
			}
		}
		_exits(0);
	}
	while(!die && (p = Brdline(b, '\n'))){
		snprint(message, Bsize, "%.*s", utfnlen(p, Blinelen(b)-1), p);
		drawmsg();
	}
	if(textmode){
		Bwrite(bout, "\n", 1);
		Bterm(bout);
	}
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
		break;
	case 0:
		break;
	}
	lfd = dup(0, -1);

	while(q = strchr(p, ','))
		*q = ' ';
	Binit(&b, lfd, OREAD);
	if((message = malloc(Bsize)) == nil)
		sysfatal("malloc: %r");
	memset(message, 0, Bsize);
	if(textmode || newwin(p) < 0){
		textmode = 1;
		if((bout = Bfdopen(1, OWRITE)) == nil)
			sysfatal("Bfdopen: %r");
	}else{
		if(initdraw(0, 0, title) < 0)
			sysfatal("initdraw: %r");
		initcolor();
		einit(Emouse|Ekeyboard);
		eresized(0);
	}
	msg(&b);

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
