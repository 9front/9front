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
#include <regexp.h>
#include "mothra.h"
#include "rtext.h"
int debug=0;
int verbose=0;		/* -v flag causes html errors to be written to file-descriptor 2 */
int killimgs=0;	/* should mothra kill images? */
int defdisplay=1;	/* is the default (initial) display visible? */
int visxbar=0;	/* horizontal scrollbar visible? */
int topxbar=0;	/* horizontal scrollbar at top? */
Panel *root;	/* the whole display */
Panel *alt;	/* the alternate display */
Panel *alttext;	/* the alternate text window */
Panel *cmd;	/* command entry */
Panel *cururl;	/* label giving the url of the visible text */
Panel *list;	/* list of previously acquired www pages */
Panel *msg;	/* message display */
Panel *menu3;	/* button 3 menu */
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
	"snarf",
	"paste",
	"plumb",
	"search",
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
int adjkb(void){
	Rtext *t;
	int yoffs;
	if(current){
		yoffs=text->r.min.y-plgetpostextview(text);
		for(t=current->text;t;t=t->next) if(!eqrect(t->r, Rect(0,0,0,0))){
			if(t->r.max.y+yoffs>=text->r.min.y
			&& t->r.min.y+yoffs<text->r.max.y
			&& t->b==0
			&& subpanel(t->p, plkbfocus))
				return 1;
		}
	}
	plgrabkb(cmd);
	return 0;
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
	if(s.pos.y > s.size.y)
		s.pos.y = s.size.y;
	if(s.pos.y < 0)
		s.pos.y = 0;
	plsetscroll(text, s);
}

void sidescroll(int dx, int whence)
{
	Scroll s;

	s = plgetscroll(text);
	switch(whence){
	case 0:
		s.pos.x = dx;
		break;
	case 1:
		s.pos.x += dx;
		break;
	case 2:
		s.pos.x = s.size.x+dx;
		break;
	}
	if(s.pos.x > s.size.x - text->size.x + 5)
		s.pos.x = s.size.x - text->size.x + 5;
	if(s.pos.x < 0)
		s.pos.x = 0;
	plsetscroll(text, s);
}

void mkpanels(void){
	Panel *p, *xbar, *ybar, *swap;
	int xflags;

	if(topxbar)
		xflags=PACKN|USERFL;
	else
		xflags=PACKS|USERFL;
	if(!visxbar)
		xflags|=IGNORE;
	menu3=plmenu(0, 0, buttons, PACKN|FILLX, hit3);
	root=plpopup(root, EXPAND, 0, 0, menu3);
		p=plgroup(root, PACKN|FILLX);
			msg=pllabel(p, PACKN|FILLX, mothra);
			plplacelabel(msg, PLACEW);
			pllabel(p, PACKW, "Go:");
			cmd=plentry(p, PACKN|FILLX, 0, "", docmd);
		p=plgroup(root, PACKN|FILLX);
			ybar=plscrollbar(p, PACKW);
			list=pllist(p, PACKN|FILLX, genwww, 8, doprev);
			plscroll(list, 0, ybar);
		p=plgroup(root, PACKN|FILLX);
			pllabel(p, PACKW, "Url:");
			cururl=pllabel(p, PACKE|EXPAND, "---");
			plplacelabel(cururl, PLACEW);
		p=plgroup(root, PACKN|EXPAND);
			ybar=plscrollbar(p, PACKW|USERFL);
			xbar=plscrollbar(p, xflags);
			text=pltextview(p, PACKE|EXPAND, Pt(0, 0), 0, dolink);
			plscroll(text, xbar, ybar);
	plgrabkb(cmd);
	alt=plpopup(0, PACKE|EXPAND, 0, 0, menu3);
		ybar=plscrollbar(alt, PACKW|USERFL);
		xbar=plscrollbar(alt, xflags);
		alttext=pltextview(alt, PACKE|EXPAND, Pt(0, 0), 0, dolink);
		plscroll(alttext, xbar, ybar);
	if(!defdisplay){
		swap=root;
		root=alt;
		alt=swap;
		swap=text;
		text=alttext;
		alttext=swap;
	}
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

char* mkhome(void){
	static char *home;		/* where to put files */
	char *henv, *tmp;
	int f;

	if(home == nil){
		henv=getenv("home");
		if(henv){
			tmp = smprint("%s/lib", henv);
			f=create(tmp, OREAD, DMDIR|0777);
			if(f!=-1) close(f);
			free(tmp);

			home = smprint("%s/lib/mothra", henv);
			f=create(home, OREAD, DMDIR|0777);
			if(f!=-1) close(f);
			free(henv);
		}
		else
			home = strdup("/tmp");
	}
	return home;
}

void donecurs(void){
	if(current && current->alldone==0)
		esetcursor(&readingcurs);
	else if(mothmode)
		esetcursor(&mothcurs);
	else
		esetcursor(0);
}

void drawlock(int dolock){
	static int ref = 0;
	if(dolock){
		if(ref++ == 0)
			lockdisplay(display);
	} else {
		if(--ref == 0)
			unlockdisplay(display);
	}
}

void scrollto(char *tag);
void search(void);

extern char *mtpt; /* url */

void main(int argc, char *argv[]){
	Event e;
	enum { Eplumb = 128, Ekick = 256 };
	Plumbmsg *pm;
	char *url;
	int i;

	quotefmtinstall();
	fmtinstall('U', Ufmt);

	ARGBEGIN{
	case 'd': debug=1; break;
	case 'v': verbose=1; break;
	case 'k': killimgs=1; break;
	case 'm':
		if(mtpt = ARGF())
			break;
	case 'a': defdisplay=0; break;
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
		fprint(2, "usage: %s [-dvak] [-m mtpt] [url]\n", argv0);
		exits("usage");
	case 0:
		url=getenv("url");
		break;
	case 1: url=argv[0]; break;
	}
	if(initdraw(0, 0, mothra) < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	chrwidth=stringwidth(font, "0");
	pltabsize(chrwidth, 8*chrwidth);
	einit(Emouse|Ekeyboard);
	eplumb(Eplumb, "web");
	if(pipe(kickpipe) < 0)
		sysfatal("pipe: %r");
	estart(Ekick, kickpipe[0], 256);
	plinit();
	if(debug) notify(dienow);
	getfonts();
	hrule=allocimage(display, Rect(0, 0, 1, 5), screen->chan, 1, DWhite);
	if(hrule==0)
		sysfatal("can't allocimage!");
	draw(hrule, Rect(0,1,1,3), display->black, 0, ZP);
	linespace=allocimage(display, Rect(0, 0, 1, 5), screen->chan, 1, DWhite);
	if(linespace==0)
		sysfatal("can't allocimage!");
	bullet=allocimage(display, Rect(0,0,25, 8), screen->chan, 0, DWhite);
	fillellipse(bullet, Pt(4,4), 3, 3, display->black, ZP);
	mkpanels();
	unlockdisplay(display);
	eresized(0);
	drawlock(1);

	if(url && url[0])
		geturl(url, -1, 1, 0);

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
		}

		drawlock(0);
		i=event(&e);
		drawlock(1);

		switch(i){
		case Ekick:
			if(mouse.buttons==0 && current && current->changed){
				if(!current->finished)
					updtext(current);
				current->changed=0;
			}
			break;
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
			case Kack:
				search();
				break;
			case Kright:
				sidescroll(text->size.x/4, 1);
				break;
			case Kleft:
				sidescroll(-text->size.x/4, 1);
				break;
			}
			break;
		case Emouse:
			mouse=e.mouse;
			if(mouse.buttons & (8|16) && ptinrect(mouse.xy, text->r)){
				if(mouse.buttons & 8)
					scrolltext(text->r.min.y - mouse.xy.y, 1);
				else
					scrolltext(mouse.xy.y - text->r.min.y, 1);
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

	drawlock(1);
	if(new && getwindow(display, Refnone) == -1) {
		fprint(2, "getwindow: %r\n");
		exits("getwindow");
	}
	r=screen->r;
	plpack(root, r);
	plpack(alt, r);
	pldraw(cmd, screen);	/* put cmd box on screen for alt display */
	pldraw(root, screen);
	flushimage(display, 1);
	drawlock(0);
}
void *emalloc(int n){
	void *v;
	v=malloc(n);
	if(v==0)
		sysfatal("out of memory");
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
	Www *w;
	int i;

	if(index >= nwww())
		return 0;
	i = wwwtop-index-1;
	w = www(i);
	if(!w->url)
		return 0;
	if(w->title[0]!='\0'){
		w->gottitle=1;
		snprint(buf, sizeof(buf), "%2d %s", i+1, w->title);
	} else
		snprint(buf, sizeof(buf), "%2d %s", i+1, urlstr(w->url));
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
	int ofd;
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
	name = urlstr(url);
	if(name == nil || name[0] == 0)
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
		message("Unknown command %s", s);
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
		killimgs = !killimgs;
		if (killimgs)
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
	pldraw(root, screen);
}

void regerror(char *msg)
{
	werrstr("regerror: %s", msg);
}

void search(void){
	static char last[256];
	char buf[256];
	Reprog *re;
	Rtext *tp;

	for(;;){
		if(current == nil || current->text == nil || text == nil)
			return;
		strncpy(buf, last, sizeof(buf)-1);
		if(eenter("Search for", buf, sizeof(buf), &mouse) <= 0)
			return;
		strncpy(last, buf, sizeof(buf)-1);
		re = regcompnl(buf);
		if(re == nil){
			message("%r");
			continue;
		}
		for(tp=current->text;tp;tp=tp->next)
			if(tp->flags & PL_SEL)
				break;
		if(tp == nil)
			tp = current->text;
		else {
			tp->flags &= ~PL_SEL;
			tp = tp->next;
		}
		while(tp != nil){
			tp->flags &= ~PL_SEL;
			if(tp->text && *tp->text)
			if(regexec(re, tp->text, nil, 0)){
				tp->flags |= PL_SEL;
				plsetpostextview(text, tp->topy);
				break;
			}
			tp = tp->next;
		}
		free(re);
		updtext(current);
	}
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
	Action *a;

	a=word->user;
	if(a == nil || (a->link == nil && a->image == nil))
		return;
	if(mothmode)
		hiturl(buttons, a->image ? a->image : a->link, 0);
	else if(a->link){
		if(a->ismap){
			char mapurl[NNAME];
			Point coord;
			int yoffs;

			yoffs=plgetpostextview(p);
			coord=subpt(subpt(mouse.xy, word->r.min), p->r.min);
			snprint(mapurl, sizeof(mapurl), "%s?%d,%d", a->link, coord.x, coord.y+yoffs);
			hiturl(buttons, mapurl, 1);
		} else
			hiturl(buttons, a->link, 0);
	}
}

void filter(int fd, char *cmd){
	switch(rfork(RFFDG|RFPROC|RFMEM|RFREND|RFNOWAIT|RFNOTEG)){
	case -1:
		message("Can't fork!");
		break;
	case 0:
		dupfds(fd, 1, 2, -1);
		execl("/bin/rc", "rc", "-c", cmd, nil);
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
			plrdhtml(w->url->fullname, fd, w, killimgs);
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

int pipeline(int fd, char *fmt, ...)
{
	char buf[80], *argv[4];
	va_list arg;
	int pfd[2];

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);

	if(pipe(pfd) < 0){
	Err:
		close(fd);
		werrstr("pipeline for %s failed: %r", buf);
		return -1;
	}
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
		_exits(0);
	}
	close(fd);
	close(pfd[1]);
	return pfd[0];
}

char*
urlstr(Url *url){
	if(url->fullname[0])
		return url->fullname;
	return url->reltext;
}

Url *copyurl(Url *u){
	Url *v;
	v=emalloc(sizeof(Url));
	*v=*u;
	v->reltext = strdup(u->reltext);
	v->basename = strdup(u->basename);
	return v;
}

void freeurl(Url *u){
	free(u->reltext);
	free(u->basename);
	free(u);
}

void seturl(Url *url, char *urlname, char *base){
	url->reltext = strdup(urlname);
	url->basename = strdup(base);
	url->fullname[0] = 0;
	url->tag[0] = 0;
	url->map = 0;
}

Url* selurl(char *urlname){
	Url *last;

	last=selection;
	selection=emalloc(sizeof(Url));
	seturl(selection, urlname, current ? current->url->fullname : "");
	if(last) freeurl(last);
	message("selected: %s", urlstr(selection));
	plgrabkb(cmd);		/* for snarf */
	return selection;
}

/*
 * get the file at the given url
 */
void geturl(char *urlname, int post, int plumb, int map){
	int i, fd, typ;
	char cmd[NNAME];
	ulong n;
	Www *w;

	if(*urlname == '#' && post < 0){
		scrollto(urlname+1);
		return;
	}

	selurl(urlname);
	selection->map=map;

	message("getting %s", urlstr(selection));
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
			fd = pipeline(fd, "exec uhtml");
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
					drawlock(0);
					sleep(10);
					drawlock(1);
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
			if(rfork(RFPROC|RFMEM|RFNOWAIT) == 0){
				for(;;){
					sleep(1000);
					if(w->finished || w->alldone)
						break;
					if(w->changed)
						write(kickpipe[1], "C", 1);
				}
				_exits(0);
			}
			plinitlist(list, PACKN|FILLX, genwww, 8, doprev);
			if(defdisplay) pldraw(list, screen);
			setcurrent(i, selection->tag);
			break;
		case GIF:
		case JPEG:
		case PNG:
		case BMP:
		case PAGE:
			filter(fd, "exec page -w");
			break;
		}
		break;
	}
	donecurs();
}
void updtext(Www *w){
	Rtext *t;
	Action *a;
	if(defdisplay && w->gottitle==0 && w->title[0]!='\0')
		pldraw(list, screen);
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
	pldraw(text, screen);
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
		if(a == nil || a->image == nil)
			continue;
		if(a->link == nil){
			if(on)
				t->flags |= PL_HOT;
			else
				t->flags &= ~PL_HOT;
			continue;
		}
		x = t->next;
		if(on){
			t->next = nil;
			ap=emalloc(sizeof(Action));
			ap->link = strdup(a->link);
			plrtstr(&t->next, 0, 0, 0, t->font, strdup("->"), PL_HOT, ap);
			t->next->next = x;
		} else {
			if(x) {
				t->next = x->next;
				x->next = nil;
				freetext(x);
			}
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
	if(p==0 || p==cmd){
		if(selection){
			plputsnarf(urlstr(selection));
			plsnarf(text);
		}else
			message("no url selected");
	}else
		plsnarf(p);
}
void paste(Panel *p){
	if(p==0) p=cmd;
	plpaste(p);
}
void hit3(int button, int item){
	char buf[1024];
	char name[NNAME];
	char *s;
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
		snarf(plkbfocus);
		break;
	case 3:
		paste(plkbfocus);
		break;
	case 4:
		if(plkbfocus==nil || plkbfocus==cmd){
			if(text==nil || text->snarf==nil || selection==nil)
				return;
			if((s=text->snarf(text))==nil)
				s=smprint("%s", urlstr(selection));
		}else
			if((s=plkbfocus->snarf(plkbfocus))==nil)
				return;
		if((fd=plumbopen("send", OWRITE))<0){
			message("can't plumb");
			free(s);
			return;
		}
		plumbsendtext(fd, "mothra", nil, getwd(buf, sizeof buf), s);
		close(fd);
		free(s);
		break;
	case 5:
		search();
		break;
	case 6:
		if(!selection){
			message("no url selected");
			break;
		}
		snprint(name, sizeof(name), "%s/hit.html", mkhome());
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
	case 7:
		snprint(name, sizeof(name), "file:%s/hit.html", mkhome());
		geturl(name, -1, 1, 0);
		break;
	case 8:
		if(confirm(3))
			exits(0);
		break;
	}
}
