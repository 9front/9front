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
Cursor mothcurs={
	{-7, -7},
	{0x00, 0x00, 0x60, 0x06, 0xf8, 0x1f, 0xfc, 0x3f, 
	 0xfe, 0x7f, 0xff, 0xff, 0x7f, 0xfe, 0x7f, 0xfe, 
	 0x7f, 0xfe, 0x3f, 0xfc, 0x3f, 0xfc, 0x1f, 0xf8, 
	 0x1f, 0xf8, 0x0e, 0x70, 0x0c, 0x30, 0x00, 0x00, },
	{0x00, 0x00, 0x00, 0x00, 0x60, 0x06, 0x58, 0x1a, 
	 0x5c, 0x3a, 0x64, 0x26, 0x27, 0xe4, 0x37, 0xec, 
	 0x37, 0xec, 0x17, 0xe8, 0x1b, 0xd8, 0x0e, 0x70, 
	 0x0c, 0x30, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00, }
};

Www *current=0;
Url *selection=0;
int logfile;
int mothmode;
int kickpipe[2];

void docmd(Panel *, char *);
void doprev(Panel *, int, int);
char *urlstr(Url *);
void setcurrent(int, char *);
char *genwww(Panel *, int);
void updtext(Www *);
void dolink(Panel *, int, Rtext *);
void hit3(int, int);
void mothon(Www *, int);
void killpix(Www *w);
char *buttons[]={
	"alt display",
	"moth mode",
	"snarf url",
	"paste",
	"save hit",
	"hit list",
	"exit",
	0
};

int wwwtop=0;
Www *www(int index){
	static Www a[NWWW];
	return &a[index % NWWW];
}
int nwww(void){
	return wwwtop<NWWW ? wwwtop : NWWW;
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
	if(current){
		yoffs=text->r.min.y-plgetpostextview(text);
		for(t=current->text;t;t=t->next) if(!eqrect(t->r, Rect(0,0,0,0))){
			if(t->r.max.y+yoffs>text->r.max.y) break;
			if(t->r.min.y+yoffs>=text->r.min.y
			&& t->b==0
			&& subpanel(t->p, pl_kbfocus)) return;
		}
	}
	plgrabkb(cmd);
}

void scrolltext(int dy, int whence)
{
	Scroll s;

	s = plgetscroll(text);
	switch(whence){
	case 0:
		s.pos.y = dy;
		break;
	case 1:
		s.pos.y += dy;
		break;
	case 2:
		s.pos.y = s.size.y+dy;
		break;
	}
	if(s.pos.y < 0)
		s.pos.y = 0;
	if(s.pos.y > s.size.y)
		s.pos.y = s.size.y;
	plsetscroll(text, s);
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
			curttl=pllabel(p, PACKE|EXPAND, "---");
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
int cohort = -1;
void killcohort(void){
	int i;
	for(i=0;i!=3;i++){	/* It's a long way to the kitchen */
		postnote(PNGROUP, cohort, "kill\n");
		sleep(1);
	}
}
void catch(void*, char*){
	noted(NCONT);
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

void donecurs(void){
	if(current && current->alldone==0)
		esetcursor(&readingcurs);
	else if(mothmode)
		esetcursor(&mothcurs);
	else
		esetcursor(0);
}

void scrollto(char *tag);
extern char *mtpt; /* url */

void main(int argc, char *argv[]){
	Event e;
	enum { Eplumb = 128 };
	Plumbmsg *pm;
	Www *new;
	Action *a;
	char *url;
	int errfile;
	int i;

	quotefmtinstall();
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
	if(cohort=rfork(RFPROC|RFNOTEG|RFNAMEG|RFREND)){
		atexit(killcohort);
		notify(catch);
		waitpid();
		exits(0);
	}
	cohort = getpid();
	atexit(killcohort);

	switch(argc){
	default:
	Usage:
		fprint(2, "Usage: %s [-d] [-m mtpt] [url]\n", argv[0]);
		exits("usage");
	case 0:
		url=getenv("url");
		break;
	case 1: url=argv[0]; break;
	}
	errfile=mkmfile("mothra.err", 0666);
	if(errfile!=-1){
		dup(errfile, 2);
		close(errfile);
	}
	logfile=mkmfile("mothra.log", 0666|DMAPPEND);
	if(initdraw(0, 0, mothra) < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	chrwidth=stringwidth(font, "0");
	pltabsize(chrwidth, 8*chrwidth);
	einit(Emouse|Ekeyboard);
	eplumb(Eplumb, "web");
	if(pipe(kickpipe) < 0)
		sysfatal("pipe: %r");
	estart(0, kickpipe[0], 256);
	plinit(screen->depth);
	if(debug) notify(dienow);
	getfonts();
	hrule=allocimage(display, Rect(0, 0, 2048, 5), screen->chan, 0, DWhite);
	if(hrule==0)
		sysfatal("can't allocimage!");
	draw(hrule, Rect(0,1,1280,3), display->black, 0, ZP);
	linespace=allocimage(display, Rect(0, 0, 2048, 5), screen->chan, 0, DWhite);
	if(linespace==0)
		sysfatal("can't allocimage!");
	bullet=allocimage(display, Rect(0,0,25, 8), screen->chan, 0, DWhite);
	fillellipse(bullet, Pt(4,4), 3, 3, display->black, ZP);
	mkpanels();

	unlockdisplay(display);
	eresized(0);
	lockdisplay(display);

	if(url && url[0])
		geturl(url, -1, 1, 0);

	if(logfile==-1) message("Can't open log file");
	mouse.buttons=0;
	for(;;){
		if(mouse.buttons==0 && current){
			if(current->finished){
				updtext(current);
				if(current->url->tag[0])
					scrollto(current->url->tag);
				current->finished=0;
				current->changed=0;
				current->alldone=1;
				message(mothra);
				donecurs();
			}
			else if(current->changed){
				updtext(current);
				current->changed=0;
			}
		}

		flushimage(display, 1);
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
			case Khome:
				scrolltext(0, 0);
				break;
			case Kup:
				scrolltext(-text->size.y/4, 1);
				break;
			case Kpgup:
				scrolltext(-text->size.y/2, 1);
				break;
			case Kdown:
				scrolltext(text->size.y/4, 1);
				break;
			case Kpgdown:
				scrolltext(text->size.y/2, 1);
				break;
			case Kend:
				scrolltext(-text->size.y, 2);
				break;
			}
			break;
		case Emouse:
			mouse=e.mouse;
			if(mouse.buttons & (8|16)){
				if(mouse.buttons & 8)
					scrolltext(-text->size.y/24, 1);
				else
					scrolltext(text->size.y/24, 1);
				break;
			}
			plmouse(root, &mouse);
			break;
		case Eplumb:
			pm=e.v;
			if(pm->ndata > 0)
				geturl(pm->data, -1, 1, 0);
			plumbfree(pm);
			break;
		}
	}
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
	plpack(root, r);
	plpack(alt, r);
	draw(screen, r, display->white, 0, ZP);
	pldraw(root, screen);
	unlockdisplay(display);
}
void *emalloc(int n){
	void *v;
	v=malloc(n);
	if(v==0)
		sysfatal("out of memory");
	setmalloctag(v, getcallerpc(&n));
	return v;
}
void *emallocz(int n, int z){
	void *v;
	v = emalloc(n);
	if(z)
		memset(v, 0, n);
	setmalloctag(v, getcallerpc(&n));
	return v;
}
void nstrcpy(char *to, char *from, int len){
	strncpy(to, from, len);
	to[len-1] = 0;
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

void scrollto(char *tag){
	Rtext *tp;
	Action *ap;
	if(current == nil || text == nil)
		return;
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
	flushimage(display, 1);
}

/*
 * selected text should be a url.
 */
void setcurrent(int index, char *tag){
	Www *new;
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
	scrollto(tag);
	if((i = open("/dev/label", OWRITE)) >= 0){
		fprint(i, "%s %s", mothra, current->url->fullname);
		close(i);
	}
	donecurs();
}
char *arg(char *s){
	do ++s; while(*s==' ' || *s=='\t');
	return s;
}
void save(int ifd, char *name){
	char buf[NNAME+64];
	int cfd, ofd;
	if(ifd < 0){
		message("save: %s: %r", name);
		return;
	}
	ofd=create(name, OWRITE, 0666);
	if(ofd < 0){
		message("save: %s: %r", name);
		return;
	}
	switch(rfork(RFNOTEG|RFNAMEG|RFFDG|RFMEM|RFPROC|RFNOWAIT)){
	case -1:
		message("Can't fork: %r");
		break;
	case 0:
		dup(ifd, 0);
		close(ifd);
		dup(ofd, 1);
		close(ofd);

		snprint(buf, sizeof(buf),
			"{tput -p || cat} |[2] {aux/statusmsg -k %q >/dev/null || cat >/dev/null}", name);
		execl("/bin/rc", "rc", "-c", buf, nil);
		exits("exec");
	}
	close(ifd);
	close(ofd);
	donecurs();
}
void screendump(char *name, int full){
	Image *b;
	int fd;
	fd=create(name, OWRITE, 0666);
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
 * convert a url into a local file name.
 */
char *urltofile(Url *url){
	char *name, *slash;
	if(url == nil)
		return nil;
	if(url->fullname[0] || url->reltext[0])
		name = urlstr(url);
	else
		name = "/";
	if(slash = strrchr(name, '/'))
		name = slash+1;
	if(name[0] == 0)
		name = "index";
	return name;
}

/*
 * user typed a command.
 */
void docmd(Panel *p, char *s){
	char buf[NNAME];
	int c;

	USED(p);
	while(*s==' ' || *s=='\t') s++;
	/*
	 * Non-command does a get on the url
	 */
	if(s[0]!='\0' && s[1]!='\0' && s[1]!=' ')
		geturl(s, -1, 0, 0);
	else switch(c = s[0]){
	default:
		message("Unknown command %s, type h for help", s);
		break;
	case 'a':
		s = arg(s);
		if(*s=='\0' && selection)
			hit3(3, 0);
		break;
	case 'g':
		s = arg(s);
		if(*s=='\0'){
	case 'r':
			if(selection)
				s = urlstr(selection);
			else
				message("no url selected");
		}
		geturl(s, -1, 0, 0);
		break;
	case 'j':
		s = arg(s);
		if(*s)
			doprev(nil, 1, wwwtop-atoi(s));
		else
			message("Usage: j index");
		break;
	case 'm':
		mothon(current, !mothmode);
		break;
	case 'k':
		killpix(current);
		break;
	case 'w':
	case 'W':
		s = arg(s);
		if(s==0 || *s=='\0'){
			snprint(buf, sizeof(buf), "dump.bit");
			if(eenter("Screendump to", buf, sizeof(buf), &mouse) <= 0)
				break;
			s = buf;
		}
		screendump(s, c == 'W');
		break;
	case 's':
		s = arg(s);
		if(!selection){
			message("no url selected");
			break;
		}
		if(s==0 || *s=='\0'){
			snprint(buf, sizeof(buf), "%s", urltofile(selection));
			if(eenter("Save to", buf, sizeof(buf), &mouse) <= 0)
				break;
			s = buf;
		}
		save(urlget(selection, -1), s);
		break;
	case 'q':
		exits(0);
	}
	plinitentry(cmd, EXPAND, 0, "", docmd);
	if(defdisplay) pldraw(cmd, screen);
}

void hiturl(int buttons, char *url, int map){
	switch(buttons){
	case 1: geturl(url, -1, 0, map); break;
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
	if(index < 0 || index >= nwww())
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
	char *file, mapurl[NNAME];
	Point coord;
	int yoffs;
	Action *a;

	a=word->user;
	if(a == nil || a->image == nil && a->link == nil)
		return;
	if(mothmode)
		hiturl(buttons, a->image ? a->image : a->link, 0);
	else if(a->ismap){
		yoffs=plgetpostextview(p);
		coord=subpt(subpt(mouse.xy, word->r.min), p->r.min);
		snprint(mapurl, sizeof(mapurl), "%s?%d,%d", a->link, coord.x, coord.y+yoffs);
		hiturl(buttons, mapurl, 1);
	} else
		hiturl(buttons, a->link ? a->link : a->image, 0);
}

void filter(char *cmd, int fd){
	switch(rfork(RFFDG|RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		message("Can't fork!");
		break;
	case 0:
		close(0);
		dup(fd, 0);
		close(fd);
		execl("/bin/rc", "rc", "-c", cmd, 0);
		_exits(0);
	}
	close(fd);
}
void gettext(Www *w, int fd, int type){
	switch(rfork(RFFDG|RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		message("Can't fork, please wait");
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

char*
urlstr(Url *url){
	if(url->fullname[0])
		return url->fullname;
	return url->reltext;
}
Url* selurl(char *urlname){
	static Url url;
	seturl(&url, urlname, current ? current->url->fullname : "");
	selection=&url;
	message("selected: %s", urlstr(selection));
	return selection;
}
void seturl(Url *url, char *urlname, char *base){
	nstrcpy(url->reltext, urlname, sizeof(url->reltext));
	nstrcpy(url->basename, base, sizeof(url->basename));
	url->fullname[0] = 0;
	url->tag[0] = 0;
	url->map = 0;
}
Url *copyurl(Url *u){
	Url *v;
	v=emalloc(sizeof(Url));
	*v=*u;
	return v;
}
void freeurl(Url *u){
	free(u);
}

/*
 * get the file at the given url
 */
void geturl(char *urlname, int post, int plumb, int map){
	int i, fd, typ, pfd[2];
	char cmd[NNAME];
	ulong n;
	Www *w;

	if(*urlname == '#' && post < 0){
		scrollto(urlname+1);
		return;
	}

	selurl(urlname);
	selection->map=map;

	message("getting %s", selection->reltext);
	esetcursor(&patientcurs);
	for(;;){
		if((fd=urlget(selection, post)) < 0){
			message("%r");
			break;
		}
		message("getting %s", selection->fullname);
		if(mothmode && !plumb)
			typ = -1;
		else
			typ = snooptype(fd);
		switch(typ){
		default:
			if(plumb){
				message("unknown file type");
				close(fd);
				break;
			}
			snprint(cmd, sizeof(cmd), "%s", urltofile(selection));
			if(eenter("Save to", cmd, sizeof(cmd), &mouse) <= 0){
				close(fd);
				break;
			}
			save(fd, cmd);
			break;
		case HTML:
			fd = pipeline("/bin/uhtml", fd);
		case PLAIN:
			n=0; 
			for(i=wwwtop-1; i>=0 && i!=(wwwtop-NWWW-1); i--){
				w = www(i);
				n += countpix(w->pix);
				if(n >= NPIXMB*1024*1024)
					killpix(w);
			}
			w = www(i = wwwtop++);
			if(i >= NWWW){
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
			gettext(w, fd, typ);
			plinitlist(list, PACKN|FILLX, genwww, 8, doprev);
			if(defdisplay) pldraw(list, screen);
			setcurrent(i, selection->tag);
			break;
		case GIF:
		case JPEG:
		case PNG:
		case BMP:
		case PAGE:
			filter("page -w", fd);
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
	if(w != current)
		return;
	w->yoffs=plgetpostextview(text);
	plinittextview(text, PACKE|EXPAND, Pt(0, 0), w->text, dolink);
	plsetpostextview(text, w->yoffs);
	pldraw(root, screen);
}
void update(Www *w){
	w->changed = 1;
	write(kickpipe[1], "C", 1);
}
void finish(Www *w){
	w->finished = 1;
	write(kickpipe[1], "F", 1);
}

void
mothon(Www *w, int on)
{
	Rtext *t, *x;
	Action *a, *ap;

	if(w==0 || mothmode==on)
		return;
	if(mothmode = on)
		message("moth mode!");
	else
		message(mothra);
	/*
	 * insert or remove artificial links to the href for 
	 * images that are also links
	 */
	for(t=w->text;t;t=t->next){
		a=t->user;
		if(a == nil || a->image == nil || a->link == nil)
			continue;
		x = t->next;
		if(on){
			t->next = nil;
			ap=mallocz(sizeof(Action), 1);
			ap->link = strdup(a->link);
			plrtstr(&t->next, 0, 0, t->font, strdup("->"), 1, ap);
			t->next->next = x;
		} else {
			t->next = x->next;
			x->next = nil;
			freetext(x);
		}
	}
	updtext(w);
	donecurs();
}

void killpix(Www *w){
	Rtext *t;

	if(w==0 || !w->finished && !w->alldone)
		return;
	for(t=w->text; t; t=t->next)
		if(t->b && t->user)
			t->b=0;
	freepix(w->pix);
	w->pix=0;
	updtext(w);
}

void snarf(Panel *p){
	int fd;
	if((fd=open("/dev/snarf", OWRITE|OTRUNC))>=0){
		fprint(fd, "%s", urlstr(selection));
		close(fd);
	}
}
void paste(Panel *p){
	char buf[1024];
	int n, len, fd;
	if((fd=open("/dev/snarf", OREAD))<0)
		return;
	nstrcpy(buf, plentryval(p), sizeof(buf));
	len=strlen(buf);
	n=read(fd, buf+len, sizeof(buf)-len-1);
	if(n>0){
		buf[len+n]='\0';
		plinitentry(cmd, PACKE|EXPAND, 0, buf, docmd);
		pldraw(cmd, screen);
	}
	close(fd);
}
void hit3(int button, int item){
	char name[NNAME];
	char file[128];
	Panel *swap;
	int fd;
	USED(button);
	switch(item){
	case 0:
		swap=root;
		root=alt;
		alt=swap;
		if(current)
			current->yoffs=plgetpostextview(text);
		swap=text;
		text=alttext;
		alttext=swap;
		defdisplay=!defdisplay;
		plpack(root, screen->r);
		if(current){
			plinittextview(text, PACKE|EXPAND, Pt(0, 0), current->text, dolink);
			plsetpostextview(text, current->yoffs);
		}
		pldraw(root, screen);
		break;
	case 1:
		mothon(current, !mothmode);
		break;
	case 2:
		snarf(cmd);
		break;
	case 3:
		paste(cmd);
		break;
	case 4:
		snprint(name, sizeof(name), "%s/hit.html", home);
		fd=open(name, OWRITE);
		if(fd==-1){
			fd=create(name, OWRITE, 0666);
			if(fd==-1){
				message("can't open %s", name);
				return;
			}
			fprint(fd, "<html><head><title>Hit List</title></head>\n");
			fprint(fd, "<body><h1>Hit list</h1>\n");
		}
		seek(fd, 0, 2);
		fprint(fd, "<p><a href=\"%s\">%s</a>\n", urlstr(selection), urlstr(selection));
		close(fd);
		break;
	case 5:
		snprint(name, sizeof(name), "file:%s/hit.html", home);
		geturl(name, -1, 1, 0);
		break;
	case 6:
		if(confirm(3))
			exits(0);
		break;
	}
}
