#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <bio.h>
#include <keyboard.h>
#include <mouse.h>

#define MIN(a,b) ((a)<=(b)?(a):(b))
#define MAX(a,b) ((a)>=(b)?(a):(b))

typedef struct Fontdir Fontdir;

struct Fontdir {
	char *name;
	char *prefix;
	char **fonts;
	int nfonts;
	int isttf;
	union {
		int ifont;
		int sz;
	};
};

enum
{
	Ckey,
	Cmouse,
	Cresize,
	Numchan,

	Ttfdefsz = 16,
};

static char *textdef[] = {
	"Cwm fjord bank glyphs vext quiz!",
	"Gud hjälpe Zorns mö qvickt få byxa?",
	"Разъярённый чтец эгоистично бьёт пятью",
	"жердями шустрого фехтовальщика.",
	"今日も一日がんばるぞい!",
	"세계를 향한 대화, 유니코드로 하십시오.",
	"",
	"static int",
	"dosomestuff(Stuff *s, char *t, int n)",
	"{",
	"    if(s == nil && (t == nil || n < 1))",
	"        return 0;",
	"    fprint(2, \"# %c\\n\", t[0]);",
	"    return dostuff(s) || dot(t, n);",
	"}",
	nil
};

static char **text = textdef;

static char *prefixes[] = {
	"/lib/font/bit",
	"/lib/font/ttf",
};

static Font *f;
static Fontdir *dirs, *cdir;
static int ndirs, idir;
static char lasterr[256];
int mainstacksize = 32768;

static void
redraw(void)
{
	Point p;
	int i, w, maxw;
	char t[256];

	lockdisplay(display);
	draw(screen, screen->r, display->white, nil, ZP);
	p = screen->r.min;

	if(f == nil){
		p.x += Dx(screen->r)/2 - stringwidth(font, lasterr)/2;
		p.y += Dy(screen->r)/2 - font->height/2;
		string(screen, p, display->black, ZP, font, lasterr);
	}else{
		maxw = 0;
		for(i = 0; text[i] != nil; i++){
			if((w = stringwidth(f, text[i])) > maxw)
				maxw = w;
		}
		p.x += Dx(screen->r)/2 - maxw/2;
		p.y += Dy(screen->r)/2 - i*f->height/2;

		for(i = 0; text[i] != nil; i++){
			string(screen, p, display->black, ZP, f, text[i]);
			p.y += f->height;
		}

	}

	if(cdir->isttf)
		snprint(t, sizeof(t), "/n/ttf/%s.%d/font", cdir->name, cdir->sz);
	else
		snprint(t, sizeof(t), "%s/%s", cdir->name, cdir->fonts[cdir->ifont]);
	p = screen->r.max;
	p.x -= Dx(screen->r)/2 + stringwidth(font, t)/2;
	p.y -= font->height;
	string(screen, p, display->black, ZP, font, t);

	flushimage(display, 1);
	unlockdisplay(display);
}

static int
fcmp(void *a, void *b)
{
	return strcmp(*(char**)a, *(char**)b);
}

static int
fontdir(char *t, int f, Fontdir *fdir)
{
	Dir *d;
	int doff, k;
	long i, n;

	k = strlen(t);
	if(k > 4 && (cistrcmp(&t[k-4], ".ttf") == 0 || cistrcmp(&t[k-4], ".otf") == 0)){
		fdir->nfonts = 1;
		fdir->sz = Ttfdefsz;
		fdir->isttf = 1;
		return 0;
	}

	if((n = dirreadall(f, &d)) < 1)
		return -1;
	doff = strlen(t);
	t[doff++] = '/';
	for(i = 0; i < n; i++){
		if((k = strlen(d[i].name)) < 5 || strcmp(&d[i].name[k-5], ".font") != 0)
			continue;
		if((fdir->fonts = realloc(fdir->fonts, sizeof(*fdir->fonts)*(fdir->nfonts+1))) == nil)
			sysfatal("no memory");
		d[i].name[k-5] = 0;
		strcpy(t+doff, d[i].name);
		fdir->fonts[fdir->nfonts++] = strdup(t+doff);
	}
	free(d);
	if(fdir->nfonts > 0)
		qsort(fdir->fonts, fdir->nfonts, sizeof(*fdir->fonts), fcmp);

	return 0;
}

static int
dcmp(void *a_, void *b_)
{
	Fontdir *a, *b;

	a = (Fontdir*)a_;
	b = (Fontdir*)b_;
	return strcmp(a->name, b->name);
}

static void
findfonts(char *prefix)
{
	Dir *d, *din;
	int f, fin, doff;
	long i, n;
	char t[1024];

	doff = sprint(t, "%s", prefix);
	t[doff++] = '/';
	t[doff] = 0;
	if((f = open(t, OREAD)) < 0){
		fprint(2, "font dir: %r\n");
		return;
	}
	if((n = dirreadall(f, &d)) < 1){
		fprint(2, "%s: no fonts\n", t);
		close(f);
		return;
	}
	close(f);
	for(i = 0; i < n; i++){
		strcpy(t+doff, d[i].name);
		if((fin = open(t, OREAD)) < 0)
			continue;
		if((din = dirfstat(fin)) != nil){
			if((dirs = realloc(dirs, sizeof(Fontdir)*(ndirs+1))) == nil)
				sysfatal("no memory");
			memset(&dirs[ndirs], 0, sizeof(Fontdir));
			dirs[ndirs].prefix = prefix;
			if(fontdir(t, fin, &dirs[ndirs]) == 0 && dirs[ndirs].nfonts > 0)
				dirs[ndirs++].name = strdup(d[i].name);
			free(din);
		}
		close(fin);
	}
	free(d);
}

static void
newfont(void)
{
	char t[512];

	lockdisplay(display);
	if(f != nil)
		freefont(f);
	if(cdir->isttf)
		snprint(t, sizeof(t), "/n/ttf/%s.%d/font", cdir->name, cdir->sz);
	else
		snprint(t, sizeof(t), "%s/%s/%s.font", cdir->prefix, cdir->name, cdir->fonts[cdir->ifont]);
	if((f = openfont(display, t)) == nil)
		snprint(lasterr, sizeof(lasterr), "%r");
	unlockdisplay(display);
}

static char *
dirgen(int i)
{
	return i < ndirs ? dirs[i].name : nil;
}

static char *
fontgen(int i)
{
	return i < cdir->nfonts ? cdir->fonts[i] : nil;
}

static void
usage(void)
{
	print("usage: %s [FILE]\n", argv0);
	threadexitsall("usage");
}

static void
loadtext(int f)
{
	Biobuf b;
	int i;

	if(f < 0 || Binit(&b, f, OREAD) != 0)
		sysfatal("loadtext: %r");

	text = nil;
	for(i = 0; i < 256; i++){
		if((text = realloc(text, (i+1)*sizeof(char*))) == nil)
			sysfatal("memory");
		if((text[i] = Brdstr(&b, '\n', 1)) == nil)
			break;
	}

	close(f);
}

void
threadmain(int argc, char **argv)
{
	Mousectl *mctl;
	Keyboardctl *kctl;
	Rune r;
	Mouse m;
	Menu menu;
	Alt a[Numchan+1] = {
		[Ckey] = {nil, &r, CHANRCV},
		[Cmouse] = {nil, &m, CHANRCV },
		[Cresize] = {nil, nil, CHANRCV},
		{nil, nil, CHANEND},
	};
	int n;

	ARGBEGIN{
	default:
		usage();
		break;
	}ARGEND;

	if(argc > 1)
		usage();
	else if(argc == 1)
		loadtext(strcmp(argv[0], "-") == 0 ? 0 : open(argv[0], OREAD));

	for(n = 0; n < nelem(prefixes); n++)
		findfonts(prefixes[n]);
	if(ndirs < 1)
		sysfatal("no fonts");
	qsort(dirs, ndirs, sizeof(*dirs), dcmp);

	if(initdraw(nil, nil, "fontsel") < 0)
		sysfatal("initdraw: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	a[Ckey].c = kctl->c;
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	a[Cmouse].c = mctl->c;
	a[Cresize].c = mctl->resizec;
	display->locking = 1;
	unlockdisplay(display);

	memset(&menu, 0, sizeof(menu));
	cdir = &dirs[0];
	newfont();
	redraw();

	for(;;){
		switch(alt(a)){
		case -1:
			goto end;

		case Ckey:
			switch (r) {
			case Kdel:
			case 'q':
				goto end;
			case '-':
				cdir->ifont = MAX(cdir->isttf ? 6 : 0, cdir->ifont-1);
				newfont();
				redraw();
				break;
			case '+':
				cdir->ifont = MIN(cdir->isttf ? 256 : cdir->nfonts-1, cdir->ifont+1);
				newfont();
				redraw();
				break;
			}
			break;

		case Cmouse:
			if(m.buttons == 2){
				menu.gen = dirgen;
				menu.lasthit = idir;
				if((n = menuhit(2, mctl, &menu, nil)) >= 0){
					idir = n;
					cdir = &dirs[idir];
					newfont();
					redraw();
				}
			}else if(m.buttons == 4 && cdir->isttf == 0){
				menu.gen = fontgen;
				menu.lasthit = cdir->ifont;
				if((n = menuhit(3, mctl, &menu, nil)) >= 0){
					cdir->ifont = n;
					newfont();
					redraw();
				}
			}
			break;

		case Cresize:
			getwindow(display, Refnone);
			redraw();
			break;
		}
	}

end:
	if(f != nil){
		if(cdir->isttf)
			print("/n/ttf/%s.%d/font\n", cdir->name, cdir->sz);
		else
			print("%s/%s/%s.font\n", cdir->prefix, cdir->name, cdir->fonts[cdir->ifont]);
	}
	threadexitsall(nil);
}
