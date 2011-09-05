/*
 * Trivial web browser
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>
#include <plumb.h>
#include <cursor.h>
#include <panel.h>
#include "mothra.h"
#include "rtext.h"
int verbose=0;		/* -v flag causes html errors to appear in error log */
int defdisplay=1;	/* is the default (initial) display visible? */
Panel *root;	/* the whole display */
Panel *alt;	/* the alternate display */
Panel *alttext;	/* the alternate text window */
Panel *cmd;	/* command entry */
Panel *curttl;	/* label giving the title of the visible text */
Panel *cururl;	/* label giving the url of the visible text */
Panel *list;	/* list of previously acquired www pages */
Panel *msg;	/* message display */
Panel *menu3;	/* button 3 menu */
Mouse mouse;	/* current mouse data */
char mothra[] = "mothra!";
Url defurl={
	"http://cat-v.org/",
	"",
	"http://cat-v.org/",
	"",
	"",
	HTML,
};
Url badurl={
	"",
	"",
	"No file loaded",
	"",
	"",
	HTML,
};
Cursor patientcurs={
	0, 0,
	0x01, 0x80, 0x03, 0xC0, 0x07, 0xE0, 0x07, 0xe0,
	0x07, 0xe0, 0x07, 0xe0, 0x03, 0xc0, 0x0F, 0xF0,
	0x1F, 0xF8, 0x1F, 0xF8, 0x1F, 0xF8, 0x1F, 0xF8,
	0x0F, 0xF0, 0x1F, 0xF8, 0x3F, 0xFC, 0x3F, 0xFC,

	0x01, 0x80, 0x03, 0xC0, 0x07, 0xE0, 0x04, 0x20,
	0x04, 0x20, 0x06, 0x60, 0x02, 0x40, 0x0C, 0x30,
	0x10, 0x08, 0x14, 0x08, 0x14, 0x28, 0x12, 0x28,
	0x0A, 0x50, 0x16, 0x68, 0x20, 0x04, 0x3F, 0xFC,
};
Cursor confirmcurs={
	0, 0,
	0x0F, 0xBF, 0x0F, 0xBF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE,
	0xFF, 0xFE, 0xFF, 0xFF, 0x00, 0x03, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFC,

	0x00, 0x0E, 0x07, 0x1F, 0x03, 0x17, 0x73, 0x6F,
	0xFB, 0xCE, 0xDB, 0x8C, 0xDB, 0xC0, 0xFB, 0x6C,
	0x77, 0xFC, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03,
	0x94, 0xA6, 0x63, 0x3C, 0x63, 0x18, 0x94, 0x90
};
Cursor readingcurs={
	-10, -3,
	0x00, 0x00, 0x00, 0x00, 0x0F, 0xF0, 0x0F, 0xF0,
	0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x1F, 0xF0,
	0x3F, 0xF0, 0x7F, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFB, 0xFF, 0xF3, 0xFF, 0x00, 0x00, 0x00, 0x00,

	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xE0,
	0x07, 0xE0, 0x01, 0xE0, 0x03, 0xE0, 0x07, 0x60,
	0x0E, 0x60, 0x1C, 0x00, 0x38, 0x00, 0x71, 0xB6,
	0x61, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
char *mtpt="/mnt/web";
Www *current=0;
Url *selection=0;
int logfile;
void docmd(Panel *, char *);
void doprev(Panel *, int, int);
void selurl(char *);
void setcurrent(int, char *);
char *genwww(Panel *, int);
void updtext(Www *);
void dolink(Panel *, int, Rtext *);
void hit3(int, int);
char *buttons[]={
	"alt display",
	"snarf url",
	"paste",
	"save hit",
	"hit list",
	"exit",
	0
};

int wwwtop=0;
Www *www(int index){
	static Www a[1+NWWW];
	return &a[1+(index % NWWW)];
}
int nwww(void){
	return wwwtop<NWWW ? wwwtop : NWWW;
}

void err(Display *, char *msg){
	fprint(2, "err: %s (%r)\n", msg);
	abort();
}
int subpanel(Panel *obj, Panel *subj){
	if(obj==0) return 0;
	if(obj==subj) return 1;
	for(obj=obj->child;obj;obj=obj->next)
		if(subpanel(obj, subj)) return 1;
	return 0;
}
/*
 * Make sure that the keyboard focus is on-screen, by adjusting it to
 * be the cmd entry if necessary.
 */
void adjkb(void){
	Rtext *t;
	int yoffs;
	extern Panel *pl_kbfocus;	/* this is a secret panel library name */
	yoffs=text->r.min.y-plgetpostextview(text);
	for(t=current->text;t;t=t->next) if(!eqrect(t->r, Rect(0,0,0,0))){
		if(t->r.max.y+yoffs>text->r.max.y) break;
		if(t->r.min.y+yoffs>=text->r.min.y
		&& t->b==0
		&& subpanel(t->p, pl_kbfocus)) return;
	}
	plgrabkb(cmd);
}

void scrolltext(int dy)
{
	Scroll s;

	s = plgetscroll(text);
	s.pos.y += dy;
	if(s.pos.y < 0)
		s.pos.y = 0;
	if(s.pos.y > s.size.y)
		s.pos.y = s.size.y;
	plsetscroll(text, s);
	pldraw(root, screen);
}

void mkpanels(void){
	Panel *p, *bar;
	menu3=plmenu(0, 0, buttons, PACKN|FILLX, hit3);
	root=plpopup(root, EXPAND, 0, 0, menu3);
		p=plgroup(root, PACKN|FILLX);
			msg=pllabel(p, PACKN|FILLX, mothra);
			plplacelabel(msg, PLACEW);
			pllabel(p, PACKW, "Go:");
			cmd=plentry(p, PACKN|FILLX, 0, "", docmd);
		p=plgroup(root, PACKN|FILLX);
			bar=plscrollbar(p, PACKW);
			list=pllist(p, PACKN|FILLX, genwww, 8, doprev);
			plscroll(list, 0, bar);
		p=plgroup(root, PACKN|FILLX);
			pllabel(p, PACKW, "Title:");
			curttl=pllabel(p, PACKE|EXPAND, "Initializing");
			plplacelabel(curttl, PLACEW);
		p=plgroup(root, PACKN|FILLX);
			pllabel(p, PACKW, "Url:");
			cururl=pllabel(p, PACKE|EXPAND, "---");
			plplacelabel(cururl, PLACEW);
		p=plgroup(root, PACKN|EXPAND);
			bar=plscrollbar(p, PACKW);
			text=pltextview(p, PACKE|EXPAND, Pt(0, 0), 0, dolink);
			plscroll(text, 0, bar);
	plgrabkb(cmd);
	alt=plpopup(0, PACKE|EXPAND, 0, 0, menu3);
		bar=plscrollbar(alt, PACKW);
		alttext=pltextview(alt, PACKE|EXPAND, Pt(0, 0), 0, dolink);
		plscroll(alttext, 0, bar);
}
void killcohort(void){
	int i;
	for(i=0;i!=3;i++){	/* It's a long way to the kitchen */
		postnote(PNGROUP, getpid(), "kill\n");
		sleep(1);
	}
}
void dienow(void*, char*){
	noted(NDFLT);
}
int mkmfile(char *stem, int mode){
	char *henv;
	char filename[NNAME];
	int f;
	if(home[0]=='\0'){
		henv=getenv("home");
		if(henv){
			sprint(home, "%s/lib", henv);
			f=create(home, OREAD, DMDIR|0777);
			if(f!=-1) close(f);
			sprint(home, "%s/lib/mothra", henv);
			f=create(home, OREAD, DMDIR|0777);
			if(f!=-1) close(f);
			free(henv);
		}
		else
			strcpy(home, "/tmp");
	}
	snprint(filename, sizeof(filename), "%s/%s", home, stem);
	f=create(filename, OWRITE, mode);
	if(f==-1)
		f=create(stem, OWRITE, mode);
	return f;
}
void main(int argc, char *argv[]){
	Event e;
	enum { Eplumb = 128 };
	Plumbmsg *pm;
	Www *new;
	char *url;
	int errfile;
	int i;
	ARGBEGIN{
	case 'd': debug++; break;
	case 'v': verbose=1; break;
	case 'm':
		if(mtpt = ARGF())
			break;
	default:  goto Usage;
	}ARGEND

	/*
	 * so that we can stop all subprocesses with a note,
	 * and to isolate rendezvous from other processes
	 */
	rfork(RFNOTEG|RFNAMEG|RFREND);
	atexit(killcohort);
	switch(argc){
	default:
	Usage:
		fprint(2, "Usage: %s [-d] [-m mtpt] [url]\n", argv[0]);
		exits("usage");
	case 0:
		url=getenv("url");
		if(url==0 || url[0]=='\0')
			url=defurl.fullname;
		break;
	case 1: url=argv[0]; break;
	}
	errfile=mkmfile("mothra.err", 0666);
	if(errfile!=-1){
		dup(errfile, 2);
		close(errfile);
	}
	logfile=mkmfile("mothra.log", 0666|DMAPPEND);
	
	initdraw(err,0,"mothra");
	display->locking = 1;
	chrwidth=stringwidth(font, "0");
	pltabsize(chrwidth, 8*chrwidth);
	einit(Emouse|Ekeyboard);
	eplumb(Eplumb, "web");
	etimer(0, 1000);
	plinit(screen->depth);
	if(debug) notify(dienow);
	getfonts();
	hrule=allocimage(display, Rect(0, 0, 2048, 5), screen->chan, 0, DWhite);
	if(hrule==0){
		fprint(2, "%s: can't allocimage!\n", argv[0]);
		exits("no mem");
	}
	draw(hrule, Rect(0,1,1280,3), display->black, 0, ZP);
	linespace=allocimage(display, Rect(0, 0, 2048, 5), screen->chan, 0, DWhite);
	if(linespace==0){
		fprint(2, "%s: can't allocimage!\n", argv[0]);
		exits("no mem");
	}
	bullet=allocimage(display, Rect(0,0,25, 8), screen->chan, 0, DWhite);
	fillellipse(bullet, Pt(4,4), 3, 3, display->black, ZP);
	new = www(-1);
	new->url=&badurl;
	strcpy(new->title, "See error message above");
	plrtstr(&new->text, 0, 0, font, "See error message above", 0, 0);
	new->alldone=1;
	mkpanels();

	unlockdisplay(display);
	eresized(0);
	lockdisplay(display);

	geturl(url, GET, 0, 1, 0);

	if(logfile==-1) message("Can't open log file");
	mouse.buttons=0;
	for(;;){
		if(mouse.buttons==0 && current){
			if(current->finished){
				updtext(current);
				current->finished=0;
				current->changed=0;
				current->alldone=1;
				message(mothra);
				esetcursor(0);
			}
			else if(current->changed){
				updtext(current);
				current->changed=0;
			}
		}

		unlockdisplay(display);
		i=event(&e);
		lockdisplay(display);

		switch(i){
		case Ekeyboard:
			switch(e.kbdc){
			default:
				adjkb();
				plkeyboard(e.kbdc);
				break;
			case Kup:
				scrolltext(-text->size.y/4);
				break;
			case Kdown:
				scrolltext(text->size.y/4);
				break;
			}
			break;
		case Emouse:
			mouse=e.mouse;
			plmouse(root, e.mouse);
			break;
		case Eplumb:
			pm=e.v;
			if(pm->ndata > 0)
				geturl(pm->data, GET, 0, 1, 0);
			plumbfree(pm);
			break;
		}
	}
}
void message(char *s, ...){
	static char buf[1024];
	char *out;
	va_list args;
	va_start(args, s);
	out = buf + vsnprint(buf, sizeof(buf), s, args);
	va_end(args);
	*out='\0';
	plinitlabel(msg, PACKN|FILLX, buf);
	if(defdisplay) pldraw(msg, screen);
}
void htmlerror(char *name, int line, char *m, ...){
	static char buf[1024];
	char *out;
	va_list args;
	if(verbose){
		va_start(args, m);
		out=buf+snprint(buf, sizeof(buf), "%s: line %d: ", name, line);
		out+=vsnprint(out, sizeof(buf)-(out-buf)-1, m, args);
		va_end(args);
		*out='\0';
		fprint(2, "%s\n", buf);
	}
}
void eresized(int new){
	Rectangle r;

	lockdisplay(display);
	if(new && getwindow(display, Refnone) == -1) {
		fprint(2, "getwindow: %r\n");
		exits("getwindow");
	}
	r=screen->r;
	plinitlabel(curttl, PACKE|EXPAND, "---");
	plinitlabel(cururl, PACKE|EXPAND, "---");
	plpack(root, r);
	if(current){
		plinitlabel(curttl, PACKE|EXPAND, current->title);
		plinitlabel(cururl, PACKE|EXPAND, current->url->fullname);
	}
	draw(screen, r, display->white, 0, ZP);
	pldraw(root, screen);
	unlockdisplay(display);
}
void *emalloc(int n){
	void *v;
	v=malloc(n);
	if(v==0){
		fprint(2, "out of space\n");
		exits("no mem");
	}
	return v;
}
void *emallocz(int n, int z){
	void *v;
	v = emalloc(n);
	if(z)
		memset(v, 0, n);
	return v;
}

char *genwww(Panel *, int index){
	static char buf[1024];
	int i;

	if(index >= nwww())
		return 0;
	i = wwwtop-index-1;
	snprint(buf, sizeof(buf), "%2d %s", i+1, www(i)->title);
	return buf;
}

void donecurs(void){
	esetcursor(current && current->alldone?0:&readingcurs);
}
/*
 * selected text should be a url.
 * get the document, scroll to the given tag
 */
void setcurrent(int index, char *tag){
	Www *new;
	Rtext *tp;
	Action *ap;
	int i;
	new=www(index);
	if(new==current && (tag==0 || tag[0]==0)) return;
	if(current)
		current->yoffs=plgetpostextview(text);
	current=new;
	plinitlabel(curttl, PACKE|EXPAND, current->title);
	if(defdisplay) pldraw(curttl, screen);
	plinitlabel(cururl, PACKE|EXPAND, current->url->fullname);
	if(defdisplay) pldraw(cururl, screen);
	plinittextview(text, PACKE|EXPAND, Pt(0, 0), current->text, dolink);
	if(tag && tag[0]){
		for(tp=current->text;tp;tp=tp->next){
			ap=tp->user;
			if(ap && ap->name && strcmp(ap->name, tag)==0){
				current->yoffs=tp->topy;
				break;
			}
		}
	}
	plsetpostextview(text, current->yoffs);
	donecurs();
	flushimage(display, 1);
}
char *arg(char *s){
	do ++s; while(*s==' ' || *s=='\t');
	return s;
}
void save(Url *url, char *name){
	int ofd, ifd, n;
	char buf[4096];
	ofd=create(name, OWRITE, 0666);
	if(ofd==-1){
		message("save: %s: %r", name);
		return;
	}
	esetcursor(&patientcurs);
	ifd=urlopen(url, GET, 0);
	donecurs();
	if(ifd==-1){
		message("save: %s: %r", selection->fullname);
		close(ofd);
	}
	switch(rfork(RFNOTEG|RFFDG|RFPROC|RFNOWAIT)){
	case -1:
		message("Can't fork -- please wait");
		esetcursor(&patientcurs);
		while((n=read(ifd, buf, 4096))>0)
			write(ofd, buf, n);
		donecurs();
		break;
	case 0:
		while((n=read(ifd, buf, 4096))>0)
			write(ofd, buf, n);
		if(n==-1) fprint(2, "save: %s: %r\n", url->fullname);
		_exits(0);
	}
	close(ifd);
	close(ofd);
}
void screendump(char *name, int full){
	Image *b;
	int fd;
	fd=create(name, OWRITE|OTRUNC, 0666);
	if(fd==-1){
		message("can't create %s", name);
		return;
	}
	if(full){
		writeimage(fd, screen, 0);
	} else {
		if((b=allocimage(display, text->r, screen->chan, 0, DNofill)) == nil){
			message("can't allocate image");
			close(fd);
			return;
		}
		draw(b, b->r, screen, 0, b->r.min);
		writeimage(fd, b, 0);
		freeimage(b);
	}
	close(fd);
}

/*
 * user typed a command.
 */
void docmd(Panel *p, char *s){
	USED(p);
	while(*s==' ' || *s=='\t') s++;
	/*
	 * Non-command does a get on the url
	 */
	if(s[0]!='\0' && s[1]!='\0' && s[1]!=' ')
		geturl(s, GET, 0, 1, 0);
	else switch(s[0]){
	default:
		message("Unknown command %s, type h for help", s);
		break;
	case 'g':
		s=arg(s);
		if(*s=='\0'){
			if(selection)
				geturl(selection->fullname, GET, 0, 1, 0);
			else
				message("no url selected");
		}
		else geturl(s, GET, 0, 1, 0);
		break;
	case 'r':
		s = arg(s);
		if(*s == '\0' && selection)
			geturl(selection->fullname, GET, 0, 0, 0);
		break;
	case 'W':
		s=arg(s);
		if(s=='\0'){
			message("Usage: W file");
			break;
		}
		screendump(s, 1);
		break;
	case 'w':
		s=arg(s);
		if(s=='\0'){
			message("Usage: w file");
			break;
		}
		screendump(s, 0);
		break;
	case 's':
		s=arg(s);
		if(*s=='\0'){
			if(selection){
				s=strrchr(selection->fullname, '/');
				if(s) s++;
			}
			if(s==0 || *s=='\0'){
				message("Usage: s file");
				break;
			}
		}
		save(selection, s);
		break;
	case 'q':
		draw(screen, screen->r, display->white, 0, ZP);
		exits(0);
	}
	plinitentry(cmd, EXPAND, 0, "", docmd);
	if(defdisplay) pldraw(cmd, screen);
}
void hiturl(int buttons, char *url, int map){
	switch(buttons){
	case 1: geturl(url, GET, 0, 1, map); break;
	case 2: selurl(url); break;
	case 4: message("Button 3 hit on url can't happen!"); break;
	}
}
/*
 * user selected from the list of available pages
 */
void doprev(Panel *p, int buttons, int index){
	int i;
	USED(p);
	if(index >= nwww())
		return;
	i = wwwtop-index-1;
	switch(buttons){
	case 1: setcurrent(i, 0);	/* no break ... */
	case 2: selurl(www(i)->url->fullname); break;
	case 4: message("Button 3 hit on page can't happen!"); break;
	}
}

/*
 * Follow an html link
 */
void dolink(Panel *p, int buttons, Rtext *word){
	char mapurl[NNAME];
	Action *a;
	Point coord;
	int yoffs;
	USED(p);
	a=word->user;
	if(a && a->link){
		if(a->ismap){
			yoffs=plgetpostextview(p);
			coord=subpt(subpt(mouse.xy, word->r.min), p->r.min);
			snprint(mapurl, sizeof(mapurl), "%s?%d,%d", a->link, coord.x, coord.y+yoffs);
			hiturl(buttons, mapurl, 1);
		}
		else
			hiturl(buttons, a->link, 0);
	}
}
void filter(char *cmd, int fd){
	flushimage(display, 1);
	switch(rfork(RFFDG|RFPROC|RFNOWAIT)){
	case -1:
		message("Can't fork!");
		break;
	case 0:
		close(0);
		dup(fd, 0);
		close(fd);
		execl("/bin/rc", "rc", "-c", cmd, 0);
		message("Can't exec /bin/rc!");
		_exits(0);
	default:
		break;
	}
	close(fd);
}
void gettext(Www *w, int fd, int type){
	flushimage(display, 1);
	switch(rfork(RFFDG|RFPROC|RFNOWAIT|RFMEM)){
	case -1:
		message("Can't fork, please wait");
		if(type==HTML)
			plrdhtml(w->url->fullname, fd, w);
		else
			plrdplain(w->url->fullname, fd, w);
		break;
	case 0:
		if(type==HTML)
			plrdhtml(w->url->fullname, fd, w);
		else
			plrdplain(w->url->fullname, fd, w);
		_exits(0);
	}
	close(fd);
}

void freetext(Rtext *t){
	Rtext *tt;
	Action *a;

	tt = t;
	for(; t!=0; t = t->next){
		t->b=0;
		free(t->text);
		t->text=0;
		if(a = t->user){
			t->user=0;
			free(a->image);
			free(a->link);
			free(a->name);
			free(a);
		}
	}
	plrtfree(tt);
}

void popwin(char *cmd){
	flushimage(display, 1);
	switch(rfork(RFFDG|RFPROC|RFNOWAIT)){
	case -1:
		message("sorry, can't fork to %s", cmd);
		break;
	case 0:
		execl("/bin/window", "window", "100 100 800 800", "rc", "-c", cmd, 0);
		_exits(0);
	}
}

int readstr(char *buf, int nbuf, char *base, char *name)
{
	char path[128];
	int n, fd;

	snprint(path, sizeof path, "%s/%s", base, name);
	if((fd = open(path, OREAD)) < 0){
	ErrOut:
		memset(buf, 0, nbuf);
		return 0;
	}
	n = read(fd, buf, nbuf-1);
	close(fd);
	if(n <= 0){
		close(fd);
		goto ErrOut;
	}
	buf[n] = 0;
	return n;
}

int urlopen(Url *url, int method, char *body){
	int conn, ctlfd, fd, n;
	char buf[1024+1];

	snprint(buf, sizeof buf, "%s/clone", mtpt);
	if((ctlfd = open(buf, ORDWR)) < 0)
		return -1;
	if((n = read(ctlfd, buf, sizeof buf-1)) <= 0){
		close(ctlfd);
		return -1;
	}
	buf[n] = 0;
	conn = atoi(buf);

	if(url->basename[0]){
		n = snprint(buf, sizeof buf, "baseurl %s", url->basename);
		write(ctlfd, buf, n);
	}
	n = snprint(buf, sizeof buf, "url %s", url->reltext);
	if(write(ctlfd, buf, n) != n){
	ErrOut:
		close(ctlfd);
		return -1;
	}

	if(method == POST && body){
		snprint(buf, sizeof buf, "%s/%d/postbody", mtpt, conn);
		if((fd = open(buf, OWRITE)) < 0)
			goto ErrOut;
		n = strlen(body);
		if(write(fd, body, n) != n){
			close(fd);
			goto ErrOut;
		}
		close(fd);
	}

	snprint(buf, sizeof buf, "%s/%d/body", mtpt, conn);
	if((fd = open(buf, OREAD)) < 0)
		goto ErrOut;

	snprint(buf, sizeof buf, "%s/%d/parsed", mtpt, conn);
	readstr(url->fullname, sizeof(url->fullname), buf, "url");
	readstr(url->tag, sizeof(url->tag), buf, "fragment");

	snprint(buf, sizeof buf, "%s/%d", mtpt, conn);
	readstr(buf, sizeof buf, buf, "contenttype");
	url->type = content2type(buf, url->fullname);

	close(ctlfd);
	return fd;
}

int pipeline(char *cmd, int fd)
{
	int pfd[2];

	if(pipe(pfd)==-1){
Err:
		close(fd);
		werrstr("pipeline for %s failed: %r", cmd);
		return -1;
	}
	switch(fork()){
	case -1:
		close(pfd[0]);
		close(pfd[1]);
		goto Err;
	case 0:
		dup(fd, 0);
		dup(pfd[0], 1);
		close(pfd[0]);
		close(pfd[1]);
		execl("/bin/rc", "rc", "-c", cmd, 0);
		_exits(0);
	}
	close(pfd[0]);
	close(fd);
	return pfd[1];
}

/*
 * select the file at the given url
 */
void selurl(char *urlname){
	static Url url;
	seturl(&url, urlname, current?
		current->url->fullname :
		defurl.fullname);
	selection=&url;
	message("selected: %s", selection->fullname);
}
void seturl(Url *url, char *urlname, char *base){
	strncpy(url->reltext, urlname, sizeof(url->reltext));
	strncpy(url->basename, base, sizeof(url->basename));
	url->fullname[0] = 0;
	url->charset[0] = 0;
	url->tag[0] = 0;
	url->type = 0;
	url->map = 0;
}
Url *copyurl(Url *u){
	Url *v;
	v=emalloc(sizeof(Url));
	*v=*u;
	return v;
}
void freeurl(Url *u){
	if(u!=&defurl && u!=&badurl)
		free(u);
}

/*
 * get the file at the given url
 */
void geturl(char *urlname, int method, char *body, int cache, int map){
	int i, fd;
	char cmd[NNAME];
	int pfd[2];
	Www *w;

	selurl(urlname);
	selection->map=map;

	message("getting %s", selection->reltext);
	esetcursor(&patientcurs);
	for(;;){
		if((fd=urlopen(selection, method, body)) < 0){
			message("%r");
			setcurrent(-1, 0);
			break;
		}
		message("getting %s", selection->fullname);
		if(selection->type&COMPRESS)
			fd=pipeline("/bin/uncompress", fd);
		else if(selection->type&GUNZIP)
			fd=pipeline("/bin/gunzip", fd);
		switch(selection->type&~COMPRESSION){
		default:
			message("Bad type %x in geturl", selection->type);
			break;
		case PLAIN:
		case HTML:
			w = www(i = wwwtop++);
			if(i >= NWWW){
				extern void freeform(void *p);
				extern void freepix(void *p);

				/* wait for the reader to finish the document */
				while(!w->finished && !w->alldone){
					unlockdisplay(display);
					sleep(10);
					lockdisplay(display);
				}

				freetext(w->text);
				freeform(w->form);
				freepix(w->pix);
				freeurl(w->url);
				memset(w, 0, sizeof(*w));
			}
			if(selection->map)
				w->url=copyurl(current->url);
			else
				w->url=copyurl(selection);
			w->finished = 0;
			w->alldone = 0;
			gettext(w, fd, selection->type&~COMPRESSION);
			plinitlist(list, PACKN|FILLX, genwww, 8, doprev);
			if(defdisplay) pldraw(list, screen);
			setcurrent(i, selection->tag);
			break;
		case POSTSCRIPT:
		case GIF:
		case JPEG:
		case PNG:
		case PDF:
			filter("page -w", fd);
			break;
		case TIFF:
			filter("/sys/lib/mothra/tiffview", fd);
			break;
		case XBM:
			filter("fb/xbm2pic|fb/9v", fd);
			break;
		}
		break;
	}
	donecurs();
}
void updtext(Www *w){
	Rtext *t;
	Action *a;
	for(t=w->text;t;t=t->next){
		a=t->user;
		if(a){
			if(a->field)
				mkfieldpanel(t);
			a->field=0;
		}
	}
	w->yoffs=plgetpostextview(text);
	plinittextview(text, PACKE|EXPAND, Pt(0, 0), w->text, dolink);
	plsetpostextview(text, w->yoffs);
	pldraw(root, screen);
}
Cursor confirmcursor={
	0, 0,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

	0x00, 0x0E, 0x07, 0x1F, 0x03, 0x17, 0x73, 0x6F,
	0xFB, 0xCE, 0xDB, 0x8C, 0xDB, 0xC0, 0xFB, 0x6C,
	0x77, 0xFC, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03,
	0x94, 0xA6, 0x63, 0x3C, 0x63, 0x18, 0x94, 0x90,
};
int confirm(int b){
	Mouse down, up;
	esetcursor(&confirmcursor);
	do down=emouse(); while(!down.buttons);
	do up=emouse(); while(up.buttons);
	donecurs();
	return down.buttons==(1<<(b-1));
}
void snarf(Panel *p){
	int fd;
	fd=create("/dev/snarf", OWRITE, 0666);
	if(fd>=0){
		fprint(fd, "%s", selection->fullname);
		close(fd);
	}
}
void paste(Panel *p){
	char buf[1024];
	int n, len, fd;
	fd=open("/dev/snarf", OREAD);
	strncpy(buf, plentryval(p), sizeof(buf));
	len=strlen(buf);
	n=read(fd, buf+len, 1023-len);
	if(n>0){
		buf[len+n]='\0';
		plinitentry(cmd, PACKE|EXPAND, 0, buf, docmd);
		pldraw(cmd, screen);
	}
	close(fd);
}
void hit3(int button, int item){
	char name[100], *home;
	Panel *swap;
	int fd;
	USED(button);
	switch(item){
	case 0:
		swap=root;
		root=alt;
		alt=swap;
		current->yoffs=plgetpostextview(text);
		swap=text;
		text=alttext;
		alttext=swap;
		defdisplay=!defdisplay;
		plpack(root, screen->r);
		plinittextview(text, PACKE|EXPAND, Pt(0, 0), current->text, dolink);
		plsetpostextview(text, current->yoffs);
		pldraw(root, screen);
		break;
	case 1:
		snarf(cmd);
		break;
	case 2:
		paste(cmd);
		break;
	case 3:
		home=getenv("home");
		if(home==0){
			message("no $home");
			return;
		}
		snprint(name, sizeof(name), "%s/lib/mothra/hit.html", home);
		fd=open(name, OWRITE);
		if(fd==-1){
			fd=create(name, OWRITE, 0666);
			if(fd==-1){
				message("can't open %s", name);
				return;
			}
			fprint(fd, "<head><title>Hit List</title></head>\n");
			fprint(fd, "<body><h1>Hit list</h1>\n");
		}
		seek(fd, 0, 2);
		fprint(fd, "<p><a href=\"%s\">%s</a>\n",
			selection->fullname, selection->fullname);
		close(fd);
		break;
	case 4:
		home=getenv("home");
		if(home==0){
			message("no $home");
			return;
		}
		snprint(name, sizeof(name), "file:%s/lib/mothra/hit.html", home);
		geturl(name, GET, 0, 1, 0);
		break;
	case 5:
		if(confirm(3)){
			draw(screen, screen->r, display->white, 0, ZP);
			exits(0);
		}
		break;
	}
}
