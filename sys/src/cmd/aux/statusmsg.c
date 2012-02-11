#include <u.h>
#include <libc.h>
#include <draw.h>
#include <bio.h>
#include <event.h>

enum {PNCTL=3};

static char* rdenv(char*);
int newwin(char*);
Rectangle screenrect(void);

int nokill;
int textmode;
char *title = nil;
char message[1024];

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
			write(1, "\b", 1);
		write(1, message, strlen(message));
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
			write(1, title, strlen(title));
			write(1, ": ", 2);
		}
	} else
	switch(child = rfork(RFMEM|RFPROC)) {
	case 0:
		sleep(1000);
		while(!die && (k = eread(Ekeyboard|Emouse, &e))) {
			if(nokill==0 && k == Ekeyboard && (e.kbdc == 0x7F || e.kbdc == 0x03)) { /* del, ctl-c */
				die = 1;
				postnote(PNPROC, parent, "interrupt");
				_exits("interrupt");
			}
		}
		_exits(0);
	}
	while(!die && (p = Brdline(b, '\n'))) {
		snprint(message, sizeof(message), "%.*s", Blinelen(b)-1, p);
		drawmsg();
	}
	if(textmode)
		write(1, "\n", 1);
	postnote(PNCTL, child, "kill");
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
	if(textmode || newwin(p) < 0){
		textmode = 1;
	}else{
		if(initdraw(0, 0, title) < 0)
			exits("initdraw");
		initcolor();
		einit(Emouse|Ekeyboard);
		eresized(0);
	}
	msg(&b);

	exits(0);
}


/* all code below this line should be in the library, but is stolen from colors instead */
static char*
rdenv(char *name)
{
	char *v;
	int fd, size;

	fd = open(name, OREAD);
	if(fd < 0)
		return 0;
	size = seek(fd, 0, 2);
	v = malloc(size+1);
	if(v == 0){
		fprint(2, "%s: can't malloc: %r\n", argv0);
		exits("no mem");
	}
	seek(fd, 0, 0);
	read(fd, v, size);
	v[size] = 0;
	close(fd);
	return v;
}

int
newwin(char *win)
{
	char *srv, *mntsrv;
	char spec[100];
	int srvfd, cons, pid;

	switch(rfork(RFFDG|RFPROC|RFNAMEG|RFENVG|RFNOTEG|RFNOWAIT)){
	case -1:
		fprint(2, "%s: can't fork: %r\n", argv0);
		return -1;
	case 0:
		break;
	default:
		exits(0);
	}

	srv = rdenv("/env/wsys");
	if(srv == 0){
		mntsrv = rdenv("/mnt/term/env/wsys");
		if(mntsrv == 0){
			fprint(2, "%s: can't find $wsys\n", argv0);
			return -1;
		}
		srv = malloc(strlen(mntsrv)+10);
		sprint(srv, "/mnt/term%s", mntsrv);
		free(mntsrv);
		pid  = 0;			/* can't send notes to remote processes! */
	}else
		pid = getpid();
	USED(pid);
	srvfd = open(srv, ORDWR);
	free(srv);
	if(srvfd == -1){
		fprint(2, "%s: can't open %s: %r\n", argv0, srv);
		return -1;
	}
	sprint(spec, "new -r %s", win);
	if(mount(srvfd, -1, "/mnt/wsys", 0, spec) == -1){
		fprint(2, "%s: can't mount /mnt/wsys: %r (spec=%s)\n", argv0, spec);
		return -1;
	}
	close(srvfd);
	unmount("/mnt/acme", "/dev");
	bind("/mnt/wsys", "/dev", MBEFORE);
	cons = open("/dev/cons", OREAD);
	if(cons==-1){
	NoCons:
		fprint(2, "%s: can't open /dev/cons: %r", argv0);
		return -1;
	}
	dup(cons, 0);
	close(cons);
	cons = open("/dev/cons", OWRITE);
	if(cons==-1)
		goto NoCons;
	dup(cons, 1);
	dup(cons, 2);
	close(cons);
	return 0;
}

Rectangle
screenrect(void)
{
	int fd;
	char buf[12*5];

	fd = open("/dev/screen", OREAD);
	if(fd == -1)
		fd=open("/mnt/term/dev/screen", OREAD);
	if(fd == -1){
		fprint(2, "%s: can't open /dev/screen: %r\n", argv0);
		exits("window read");
	}
	if(read(fd, buf, sizeof buf) != sizeof buf){
		fprint(2, "%s: can't read /dev/screen: %r\n", argv0);
		exits("screen read");
	}
	close(fd);
	return Rect(atoi(buf+12), atoi(buf+24), atoi(buf+36), atoi(buf+48));
}

int
postnote(int group, int pid, char *note)
{
	char file[128];
	int f, r;

	switch(group) {
	case PNPROC:
		sprint(file, "/proc/%d/note", pid);
		break;
	case PNGROUP:
		sprint(file, "/proc/%d/notepg", pid);
		break;
	case PNCTL:
		sprint(file, "/proc/%d/ctl", pid);
		break;
	default:
		return -1;
	}

	f = open(file, OWRITE);
	if(f < 0)
		return -1;

	r = strlen(note);
	if(write(f, note, r) != r) {
		close(f);
		return -1;
	}
	close(f);
	return 0;
}
