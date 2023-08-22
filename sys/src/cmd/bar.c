#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include <thread.h>
#include <tos.h>

#define MAX(a,b) ((a)>=(b)?(a):(b))

enum {
	Off = 3,
};

static int wctl = -1, owidth, width, twidth, bottom, bat, minheight, seplen, sepw, hlitem;
static int lastpx;
static char sep[16], bats[16], *aux;
static char *pos = "rb", *dfmt = "YYYY/MM/DD WW hh:mm:ss", *items[64];
static int itemw[64], nitems;
static Image *cback, *ctext;
static Tzone *local;
static Font *f;

#pragma varargck type "|" char*
static int
sepfmt(Fmt *f)
{
	return fmtstrcpy(f, va_arg(f->args, char*)[0] ? sep : "");
}

/*
 * nsec() is wallclock and can be adjusted by timesync
 * so need to use cycles() instead, but fall back to
 * nsec() in case we can't
 */
static uvlong
nanosec(void)
{
	static uvlong fasthz, xstart;
	uvlong x, div;

	if(fasthz == ~0ULL)
		return nsec() - xstart;

	if(fasthz == 0){
		fasthz = _tos->cyclefreq;
		if(fasthz == 0){
			fasthz = ~0ULL;
			xstart = nsec();
			return 0;
		}else{
			cycles(&xstart);
		}
	}
	cycles(&x);
	x -= xstart;

	/* this is ugly */
	for(div = 1000000000ULL; x < 0x1999999999999999ULL && div > 1 ; div /= 10ULL, x *= 10ULL);

	return x / (fasthz / div);
}

static void
place(void)
{
	int w, h, minx, miny, maxx, maxy;
	char t[64];
	static int ow, oh;

	if(wctl < 0 && (wctl = open("/dev/wctl", OWRITE)) < 0)
		return;

	fprint(wctl, bottom ? "bottom" : "top");
	w = Dx(display->image->r);
	h = Dy(display->image->r);

	if(ow != w || oh != h || owidth < width){
		if(pos[0] == 't' || pos[1] == 't'){
			miny = 0;
			maxy = minheight;
		}else{
			miny = h - minheight;
			maxy = h;
		}
		if(pos[0] == 'l' || pos[1] == 'l'){
			minx = 0;
			maxx = MAX(100, Borderwidth+Off+width+Off+Borderwidth);
		}else if(pos[0] == 'r' || pos[1] == 'r'){
			minx = MAX(100, w-(Borderwidth+Off+width+Off+Borderwidth));
			maxx = w;
		}else{
			minx = (w-MAX(100, Borderwidth+Off+width+Off+Borderwidth))/2;
			maxx = (w+MAX(100, Borderwidth+Off+width+Off+Borderwidth))/2;
		}
		snprint(t, sizeof(t), "resize -r %d %d %d %d", minx, miny, maxx, maxy);
		write(wctl, "current", 7);
		if(fprint(wctl, "%s", t) < 0)
			fprint(2, "%s: %r\n", t);
		ow = w;
		oh = h;
		owidth = width;
	}
}

static void
split(char *s)
{
	char *i;

	for(nitems = 0, i = s; nitems < nelem(items); s += seplen, i = s){
		if((s = strstr(s, sep)) != nil)
			*s = 0;
		if(*i == 0)
			continue;
		items[nitems] = i;
		itemw[nitems++] = stringwidth(f, i);
		if(s == nil)
			break;
	}
	
}

static void
redraw(void)
{
	static char s[1024];
	char tmp[1024];
	Rectangle r;
	Tmfmt tf;
	Point p;
	Tm tm;
	int i;

	r = screen->r;

	tf = tmfmt(tmnow(&tm, local), dfmt);
	p.x = r.min.x + Off;
	p.y = (pos[0] == 't' || pos[1] == 't') ? r.max.y - (f->height + Off) : r.min.y + Off;
	if(pos[0] == 'l' || pos[1] == 'l'){
		snprint(s, sizeof(s), "%τ%|%s%|%s", tf, bats, bats, aux, aux);
	}else{
		snprint(s, sizeof(s), "%s%|%s%|%τ", aux, aux, bats, bats, tf);
		if(pos[0] == 'r' || pos[1] == 'r')
			p.x = r.max.x - (stringwidth(f, s) + Off);
	}
	lastpx = p.x;
	draw(screen, r, cback, nil, ZP);
	string(screen, p, ctext, ZP, f, s);
	if(hlitem >= 0){
		r.min.x = lastpx;
		for(i = 0; i < hlitem; i++)
			r.min.x += itemw[i] + sepw;
		r.max.x = r.min.x + itemw[i];
		replclipr(screen, 0, r);
		stringbg(screen, p, cback, ZP, f, s, ctext, ZP);
		replclipr(screen, 0, screen->r);
	}
	split(s);

	flushimage(display, 1);

	snprint(tmp, sizeof(tmp), "%τ", tf);
	twidth = MAX(twidth, stringwidth(f, tmp));
	snprint(tmp, sizeof(tmp), "%|%s%|%s", bats, bats[0] ? "100%" : "", aux, aux);
	width = twidth + stringwidth(f, tmp);
	if(owidth != width)
		place();
}

static void
readbattery(void)
{
	char *s, tmp[16];

	s = bat < 0 || pread(bat, tmp, 4, 0) < 4 ? nil : strchr(tmp, ' ');
	if(s != nil){
		*s = 0;
		snprint(bats, sizeof(bats), "%s%%", tmp);
	}else{
		bats[0] = 0;
	}
}

static void
timerproc(void *c)
{
	threadsetname("timer");
	for(;;){
		sleep(990);
		sendul(c, 0);
	}
}

static void
auxproc(void *c)
{
	u8int buf[1024];
	Biobufhdr b;
	char *s;

	threadsetname("aux");
	Binits(&b, 0, OREAD, buf, sizeof(buf));
	for(;;){
		sendp(c, s = Brdstr(&b, '\n', 1));
		if(s == nil)
			break;
	}
	Bterm(&b);

	threadexits(nil);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-b] [-d dateformat] [-p lt|t|rt|lb|b|rb] [-s separator]\n", argv0);
	threadexitsall("usage");
}

static void
clicked(int x, int buttons)
{
	int i, ix;

	x -= lastpx;
	for(i = ix = 0; i < nitems; i++){
		if(x >= ix && x <= ix+itemw[i]){
			fprint(1, "%d\t%s\n", buttons, items[i]);
			hlitem = i;
			break;
		}
		ix += itemw[i] + sepw;
	}
}

void
threadmain(int argc, char **argv)
{
	Keyboardctl *kctl;
	Mousectl *mctl;
	uvlong t, oldt;
	int oldbuttons;
	char *s, *v[3];
	u32int brgb;
	Biobuf *b;
	Rune key;
	Mouse m;
	enum {
		Emouse,
		Eresize,
		Ekeyboard,
		Eaux,
		Etimer,
		Eend,
	};
	Alt a[] = {
		[Emouse] = { nil, &m, CHANRCV },
		[Eresize] = { nil, nil, CHANRCV },
		[Ekeyboard] = { nil, &key, CHANRCV },
		[Eaux] = { nil, &s, CHANRCV },
		[Etimer] = { nil, nil, CHANRCV },
		[Eend] = { nil, nil, CHANEND },
	};

	strcpy(sep, " │ ");
	ARGBEGIN{
	case 'b':
		bottom = 1;
		break;
	case 'd':
		dfmt = EARGF(usage());
		break;
	case 'p':
		pos = EARGF(usage());
		break;
	case 's':
		snprint(sep, sizeof(sep), "%s", EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND
	seplen = strlen(sep);

	fmtinstall('|', sepfmt);
	tmfmtinstall();
	if((local = tzload("local")) == nil)
		sysfatal("zone: %r");

	brgb = DPalegreygreen;
	if((b = Bopen("/dev/theme", OREAD)) != nil){
		while((s = Brdline(b, '\n')) != nil){
			s[Blinelen(b)-1] = 0;
			if(tokenize(s, v, nelem(v)) > 1 && strcmp(v[0], "ltitle") == 0){
				brgb = strtoul(v[1], nil, 16)<<8 | 0xff;
				break;
			}
		}
		Bterm(b);
	}

	if((bat = open("/mnt/pm/battery", OREAD)) < 0)
		bat = open("/dev/battery", OREAD);
	if(initdraw(nil, nil, "bar") < 0)
		sysfatal("initdraw: %r");
	f = display->defaultfont;
	minheight = 2*(Borderwidth+1) + f->height;
	sepw = stringwidth(f, sep);
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	cback = allocimage(display, Rect(0,0,1,1), RGB24, 1, brgb);
	if(brgb == DPalegreygreen)
		brgb = DBlack;
	else{ /* dunno, just invert */
		brgb = ~(brgb>>8 | brgb>>16 | brgb>>24);
		brgb = brgb<<8 | brgb<<16 | brgb<<24 | 0xff;
	}
	ctext = allocimage(display, Rect(0,0,1,1), RGB24, 1, brgb);

	a[Emouse].c = mctl->c;
	a[Eresize].c = mctl->resizec;
	a[Ekeyboard].c = kctl->c;
	a[Eaux].c = chancreate(sizeof(s), 0);
	a[Etimer].c = chancreate(sizeof(ulong), 0);

	hlitem = -1;
	aux = strdup("");
	readbattery();
	redraw();
	proccreate(timerproc, a[Etimer].c, 4096);
	proccreate(auxproc, a[Eaux].c, 4096);

	m.buttons = 0;
	oldt = nanosec();
	for(;;){
		oldbuttons = m.buttons;

		switch(alt(a)){
		case Ekeyboard:
			if(key == Kdel){
				close(wctl);
				threadexitsall(nil);
			}
			break;

		case Emouse:
			if(m.buttons == oldbuttons)
				break;
			if(m.buttons == 0)
				hlitem = -1;
			else
				clicked(m.xy.x, m.buttons);
			/* wet floor */

		if(0){
		case Eresize:
			if(getwindow(display, Refnone) < 0)
				threadexitsall(nil);
			owidth = 0;
			/* wet floor */
		}

		if(0){
		case Eaux:
			free(aux);
			aux = s;
			if(aux == nil)
				threadexitsall(nil);
			/* wet floor */
		}

		if(0){
		case Etimer:
			t = nanosec();
			if(t - oldt >= 30000000000ULL){
				readbattery();
				oldt = t;
			}
		}
			place();
			redraw();
			break;
		}
	}
}
