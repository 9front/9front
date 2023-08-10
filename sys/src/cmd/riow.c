#include <u.h>
#include <libc.h>
#include <draw.h>
#include <keyboard.h>
#include <ctype.h>

typedef struct W W;

enum {
	Mmod4 = 1<<0,
	Mctl = 1<<1,
	Mshift = 1<<2,

	Step = 16,
	Stepbig = 64,

	Fvisible = 1<<0,
	Fcurrent = 1<<1,
	Fsticky = 1<<2,
	Ffullscreen = 1<<3,
};

struct W {
	int id;
	Rectangle r;
	int vd;
	int flags;
	int stickyforced;
};

static int vd = 1; /* current virtual desktop */
static int wsys; /* rios /dev/wsys fd */
static int mod;
static W *ws, *wcur;
static int wsn;
static int vd2wcur[10] = {-1};

static char *sticky[32] = {
	"bar",
	"cat clock",
	"clock",
	"faces",
	"kbmap",
	"stats",
	"winwatch",
	nil,
};

static int
wwctl(int id, int mode)
{
	char s[64];

	snprint(s, sizeof(s), "/dev/wsys/%d/wctl", id);

	return open(s, mode);
}

static void
wsupdate(void)
{
	int i, k, n, f, seen, tn, dsn;
	char s[256], *t[8];
	W *newws, *w;
	Dir *ds, *d;

	seek(wsys, 0, 0);
	if((dsn = dirreadall(wsys, &ds)) < 0)
		sysfatal("/dev/wsys: %r");

	newws = malloc(sizeof(W)*dsn);
	wcur = nil;
	for(i = 0, d = ds, w = newws; i < dsn; i++, d++){
		if((f = wwctl(atoi(d->name), OREAD)) < 0)
			continue;
		n = read(f, s, sizeof(s)-1);
		close(f);
		if(n < 12)
			continue;
		s[n] = 0;
		if((tn = tokenize(s, t, nelem(t))) < 6)
			continue;

		w->id = atoi(d->name);
		w->r.min.x = atoi(t[0]);
		w->r.min.y = atoi(t[1]);
		w->r.max.x = atoi(t[2]);
		w->r.max.y = atoi(t[3]);
		w->vd = -1;
		w->flags = 0;
		w->stickyforced = 0;

		/* move over the current state of the window */
		for(k = 0, seen = 0; k < wsn; k++){
			if(ws[k].id == w->id){
				w->vd = ws[k].vd;
				w->flags = ws[k].flags & ~(Fvisible|Fcurrent);
				w->stickyforced = ws[k].stickyforced;
				if(w->flags & Ffullscreen)
					w->r = ws[k].r;
				seen = 1;
				break;
			}
		}

		/* update current state */
		for(k = 4; k < tn; k++){
			if(strcmp(t[k], "current") == 0){
				w->flags |= Fcurrent;
				wcur = w;
				w->vd = vd;
			}else if(strcmp(t[k], "visible") == 0){
				w->flags |= Fvisible;
				w->vd = vd;
			}
		}

		if(!seen){
			/* not seen previously - set the new state for it */
			w->vd = vd;
		}

		/* because a different program can run in any window we have to re-read */
		w->flags &= ~Fsticky;
		if(w->stickyforced){
			w->flags |= Fsticky;
		}else{
			snprint(s, sizeof(s), "/dev/wsys/%d/label", w->id);
			if((f = open(s, OREAD)) >= 0){
				n = read(f, s, sizeof(s)-1);
				close(f);
				if(n > 0){
					s[n] = 0;
					for(k = 0; k < nelem(sticky) && sticky[k] != nil; k++){
						if(strcmp(sticky[k], s) == 0){
							w->flags |= Fsticky;
							break;
						}
					}
				}
			}
		}
		w++;
	}

	free(ds);
	free(ws);
	ws = newws;
	wsn = w - newws;
}

static void
spawn(char *s)
{
	if(rfork(RFPROC|RFNOWAIT|RFNAMEG|RFENVG|RFCFDG|RFREND) == 0)
		execl("/bin/rc", "rc", s, nil);
}

static void
togglefullscreen(void)
{
	int f;

	wsupdate();
	if(wcur == nil || (f = wwctl(wcur->id, OWRITE)) < 0)
		return;
	wcur->flags ^= Ffullscreen;
	if(wcur->flags & Ffullscreen)
		fprint(f, "resize -r 0 0 9999 9999");
	else
		fprint(f, "resize -r %d %d %d %d", wcur->r.min.x, wcur->r.min.y, wcur->r.max.x, wcur->r.max.y);
	close(f);
}

static void
togglesticky(void)
{
	wsupdate();
	if(wcur != nil)
		wcur->stickyforced ^= 1;
}

static void
vdaction(int nvd)
{
	int f, wcurf;
	W *w;

	if(vd == nvd)
		return;

	wsupdate();
	if(mod == Mmod4){
		wcur = nil;
		wcurf = -1;
		vd2wcur[vd] = -1;
		for(w = ws; w < ws+wsn; w++){
			if(w->flags & Fvisible)
				w->vd = vd;
			else if(w->vd == vd)
				w->vd = -1;

			if(w->flags & Fcurrent)
				vd2wcur[vd] = w->id;

			if(w->vd == nvd && (w->flags & Fsticky) == 0){
				if((f = wwctl(w->id, OWRITE)) >= 0){
					fprint(f, "unhide");
					if(vd2wcur[nvd] == w->id && wcurf < 0){
						wcur = w;
						wcurf = f;
					}else
						close(f);
				}
			}
		}

		if(wcur != nil){
			fprint(wcurf, "top");
			fprint(wcurf, "current");
			close(wcurf);
		}

		for(w = ws; w < ws+wsn; w++){
			if(w->vd != nvd && (w->flags & (Fsticky|Fvisible)) == Fvisible){
				if((f = wwctl(w->id, OWRITE)) >= 0){
					fprint(f, "hide");
					close(f);
				}
			}
		}

		vd = nvd;
		fprint(3, "%d\n", vd);
	}else if(mod == (Mmod4 | Mshift) && wcur != nil && wcur->vd != nvd){
		if((f = wwctl(wcur->id, OWRITE)) >= 0){
			fprint(f, "hide");
			wcur->vd = nvd;
			vd2wcur[nvd] = wcur->id; /* bring to the top */
			wcur = nil;
			close(f);
		}
	}
}

static void
arrowaction(int x, int y)
{
	int f;

	wsupdate();
	if(wcur == nil || (f = wwctl(wcur->id, OWRITE)) < 0)
		return;

	x *= (mod & Mctl) ? Stepbig : Step;
	y *= (mod & Mctl) ? Stepbig : Step;
	if((mod & Mshift) == 0)
		fprint(f, "move -minx %+d -miny %+d", x, y);
	else
		fprint(f, "resize -maxx %+d -maxy %+d -minx %+d -miny %+d", x, y, -x, -y);
	close(f);
}

static void
delete(void)
{
	int f;

	wsupdate();
	if(wcur == nil || (f = wwctl(wcur->id, OWRITE)) < 0)
		return;
	fprint(f, "delete");
	close(f);
}

static struct {
	int x, y;
}cyclectx;

static int
cyclecmp(void *a_, void *b_)
{
	W *a = a_, *b = b_;

	return cyclectx.x*(a->r.min.x - b->r.min.x) + cyclectx.y*(a->r.min.y - b->r.min.y);
}

static void
cycleaction(int x, int y)
{
	int wcurid, i, f;
	W *w, *w₀;

	wsupdate();
	wcurid = wcur == nil ? -1 : wcur->id;
	cyclectx.x = x;
	cyclectx.y = y;
	qsort(ws, wsn, sizeof(*ws), cyclecmp);
	w₀ = nil;
	wcur = nil;
	for(i = 0, w = ws; i < wsn; i++, w++){
		if(w->id == wcurid){
			wcur = w;
			continue;
		}
		if((w->flags & Fsticky) != 0 || w->vd != vd)
			continue;
		if(w₀ == nil)
			w₀ = w;
		if(wcur != nil)
			break;
	}
	if(i >= wsn)
		w = w₀;
	if(w == nil || (f = wwctl(w->id, OWRITE)) < 0)
		return;
	fprint(f, "top");
	fprint(f, "current");
	close(f);
	wcur = w;
}

static int
keyevent(char c, Rune r)
{
	if(c == 'c'){
		if(r == '\n' && mod == Mmod4){
			spawn("window");
			return 0;
		}
		if(r == 'f' && mod == Mmod4){
			togglefullscreen();
			return 0;
		}
		if(r == 's' && mod == Mmod4){
			togglesticky();
			return 0;
		}
		if(r == Kup){
			arrowaction(0, -1);
			return 0;
		}
		if(r == Kdown){
			arrowaction(0, 1);
			return 0;
		}
		if(r == Kleft){
			arrowaction(-1, 0);
			return 0;
		}
		if(r == Kright){
			arrowaction(1, 0);
			return 0;
		}
		if(r == 'h' && mod == Mmod4){
			cycleaction(-1, 0);
			return 0;
		}
		if(r == 'l' && mod == Mmod4){
			cycleaction(1, 0);
			return 0;
		}
		if(r == 'j' && mod == Mmod4){
			cycleaction(0, 1);
			return 0;
		}
		if(r == 'k' && mod == Mmod4){
			cycleaction(0, -1);
			return 0;
		}
		if(r == 'Q' && mod == (Mmod4|Mshift)){
			delete();
			return 0;
		}
		if(r >= '0' && r <= '9' && (mod & Mctl) == 0){
			vdaction(r - '0');
			return 0;
		}
	}
	/* skip redundant event */
	if(c == 'k' && (mod & Mshift) == 0)
		return 0;
	/* mod4 + shift + 1…0 yields a shifted value on 'c': workaround */
	if(c == 'k' && mod == (Mmod4|Mshift) && r >= '0' && r <= '9'){
		vdaction(r - '0');
		return 0;
	}
	/* don't bother to properly deal with handling shifted digit keys */
	if((mod & Mshift) != 0 && (c == 'c' || c == 'k' || c == 'K'))
		return ispunct(r) ? 0 : -1;

	return -1;
}

static int
process(char *s)
{
	char b[128], *p;
	int n, o;
	Rune r;

	o = 0;
	b[o++] = *s;
	if(*s == 'k' || *s == 'K'){
		mod = 0;
		if(utfrune(s+1, Kmod4) != nil)
			mod |= Mmod4;
		if(utfrune(s+1, Kctl) != nil)
			mod |= Mctl;
		if(utfrune(s+1, Kshift) != nil)
			mod |= Mshift;
	}

	for(p = s+1; *p != 0; p += n){
		if((n = chartorune(&r, p)) == 1 && r == Runeerror){
			/* bail out */
			n = strlen(p);
			memmove(b+o, p, n);
			o += n;
			break;
		}

		if((mod & Mmod4) == 0 || keyevent(*s, r) != 0){
			memmove(b+o, p, n);
			o += n;
		}
	}
	b[o++] = 0;
	return (o > 1 && write(1, b, o) <= 0) ? -1 : 0;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-s label]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char b[128];
	int i, n;

	for(n = 0; sticky[n] != nil; n++)
		;

	ARGBEGIN{
	case 's':
		if(n >= nelem(sticky))
			sysfatal("ewwww");
		sticky[n++] = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if((wsys = open("/dev/wsys", OREAD)) < 0)
		sysfatal("%r");

	/* initial state */
	wsupdate();
	for(i = 0; i < wsn; i++)
		ws[i].vd = vd;
	fprint(3, "%d\n", vd);

	for(;;){
		if((n = read(0, b, sizeof(b)-1)) <= 0)
			break;
		b[n] = 0;
		if(process(b) != 0)
			break;
	}

	exits(nil);
}
