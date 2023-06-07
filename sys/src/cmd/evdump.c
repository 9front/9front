#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include <thread.h>

typedef struct {
	u8int e;
	u8int c;
	Rune r;
}K;

enum {
	Kmbase = 0xf0000,
};

static K k[10*128];
static int nk;
static int kbd;
static Biobuf *kbmap, *wctl;
static char *layertab[] = {
	"none", "shift", "esc", "altgr",
	"ctl", "ctlesc", "shiftesc", "shiftaltgr",
	"mod4", "altgrmod4",
};

static char *
k2s(Rune r)
{
	switch(r){
	case Ksbwd: return "Ksbwd";
	case Ksfwd: return "Ksfwd";
	case Kpause: return "Kpause";
	case Kvoldn: return "Kvoldn";
	case Kvolup: return "Kvolup";
	case Kmute: return "Kmute";
	case Kbrtdn: return "Kbrtdn";
	case Kbrtup: return "Kbrtup";
	case Kack: return "Kack";
	case Kalt: return "Kalt";
	case Kaltgr: return "Kaltgr";
	case Kbreak: return "Kbreak";
	case Kbs: return "Kbs";
	case Kcaps: return "Kcaps";
	case Kctl: return "Kctl";
	case Kdel: return "Kdel";
	case Kdown: return "Kdown";
	case Kend: return "Kend";
	case Kenq: return "Kenq";
	case Keof: return "Keof";
	case Kesc: return "Kesc";
	case Ketb: return "Ketb";
	case Ketx: return "Ketx";
	case Khome: return "Khome";
	case Kins: return "Kins";
	case Kleft: return "Kleft";
	case Kmiddle: return "Kmiddle";
	case Kmod4: return "Kmod4";
	case Knack: return "Knack";
	case Knum: return "Knum";
	case Kpgdown: return "Kpgdown";
	case Kpgup: return "Kpgup";
	case Kprint: return "Kprint";
	case Kright: return "Kright";
	case Kscroll: return "Kscroll";
	case Kscrollonedown: return "Kscrollonedown";
	case Kscrolloneup: return "Kscrolloneup";
	case Kshift: return "Kshift";
	case Ksoh: return "Ksoh";
	case Kstx: return "Kstx";
	case Kup: return "Kup";
	case KF|1: return "F1";
	case KF|2: return "F2";
	case KF|3: return "F3";
	case KF|4: return "F4";
	case KF|5: return "F5";
	case KF|6: return "F6";
	case KF|7: return "F7";
	case KF|8: return "F8";
	case KF|9: return "F9";
	case KF|10: return "F10";
	case KF|11: return "F11";
	case KF|12: return "F12";
	case Kmouse|1: return "Kmouse1";
	case Kmouse|2: return "Kmouse2";
	case Kmouse|3: return "Kmouse3";
	case Kmouse|4: return "Kmouse4";
	case Kmouse|5: return "Kmouse5";
	case '\n': return "\\n";
	}

	return nil;
}

static int
kmreset(void *, char *)
{
	int i;

	for(i = 0; i < nk; i++)
		Bprint(kbmap, "%d\t%d\t%d\n", k[i].e, k[i].c, k[i].r);
	Bflush(kbmap);

	return 0;
}

static void
kmset(void)
{
	int i;

	for(i = 0; i < nk; i++)
		Bprint(kbmap, "%d\t%d\t%d\n", k[i].e, k[i].c, Kmbase+i);
	Bflush(kbmap);
}

static void
key(Rune r, char *type)
{
	char *s, t[32];
	Rune c;
	K q;

	if(r < Kmbase || r >= Kmbase+nk){
		if((s = k2s(r)) != nil)
			snprint(t, sizeof(t), "%s", s);
		else if((r < 0x80 && isprint(r)) || r >= 0x20)
			snprint(t, sizeof(t), "%C (0x%x)", r, r);
		else
			snprint(t, sizeof(t), "0x%x", r);
		return;
	}
	q = k[r-Kmbase];
	c = q.r;
	if((s = k2s(c)) != nil)
		snprint(t, sizeof(t), "%s", s);
	else if((c < 0x80 && isprint(c)) || c >= 0x20)
		snprint(t, sizeof(t), "%C (0x%x)", c, c);
	else
		snprint(t, sizeof(t), "0x%x", c);

	print("key %s %s: %d %d 0x%ux\n", type, t, q.e, q.c, q.r);
}

static void
wctlproc(void *)
{
	char s[256], *t[8];
	int wctl, n;

	if((wctl = open("/dev/wctl", OREAD)) < 0)
		sysfatal("%r");
	for(;;){
		if((n = read(wctl, s, sizeof(s)-1)) <= 0)
			break;
		s[n] = 0;
		if(tokenize(s, t, nelem(t)) < 6)
			continue;

		if(strcmp(t[4], "current") == 0)
			kmset();
		else if(strcmp(t[4], "notcurrent") == 0)
			kmreset(nil, nil);

		print("wctl %s %s\n", t[4], t[5]);
	}
	close(wctl);

	threadexits(nil);
}

static void
kbproc(void *)
{
	char *s, buf[128], buf2[128];
	int kbd, n;
	Rune r;

	threadsetname("kbproc");
	if((kbd = open("/dev/kbd", OREAD)) < 0)
		sysfatal("/dev/kbd: %r");

	buf2[0] = 0;
	buf2[1] = 0;
	buf[0] = 0;
	for(;;){
		if(buf[0] != 0){
			n = strlen(buf)+1;
			memmove(buf, buf+n, sizeof(buf)-n);
		}
		if(buf[0] == 0){
			n = read(kbd, buf, sizeof(buf)-1);
			if(n <= 0)
				break;
			buf[n-1] = 0;
			buf[n] = 0;
		}

		switch(buf[0]){
		case 'k':
			for(s = buf+1; *s;){
				s += chartorune(&r, s);
				if(utfrune(buf2+1, r) == nil)
					key(r, "down");
			}
			break;
		case 'K':
			for(s = buf2+1; *s;){
				s += chartorune(&r, s);
				if(utfrune(buf+1, r) == nil)
					key(r, "up");
			}
			break;
		case 'c':
			if(chartorune(&r, buf+1) > 0 && r != Runeerror)
				key(r, "repeat");
		default:
			continue;
		}

		strcpy(buf2, buf);
	}

	close(kbd);

	threadexits(nil);
}

static void
usage(void)
{
	fprint(2, "usage: %s\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	Mousectl *mctl;
	char tmp[32], *f[4], *s;
	int nf, e;
	Mouse m;
	enum { Cmouse, Cresize, Numchan };
	Alt a[Numchan+1] = {
		[Cmouse] = { nil, &m, CHANRCV },
		[Cresize] = { nil, nil, CHANRCV },
		{ nil, nil, CHANEND },
	};

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc != 0)
		usage();

	if((kbmap = Bopen("/dev/kbmap", OREAD)) == nil)
		sysfatal("%r");

	nk = 0;
	for(;;){
		if((s = Brdline(kbmap, '\n')) == nil)
			break;
		s[Blinelen(kbmap)-1] = '\0';
		nf = getfields(s, f, nelem(f), 1, " \t");
		if(nf < 3)
			continue;
		for(e = 0; e < nelem(layertab); e++)
			if(strcmp(f[0], layertab[e]) == 0)
				break;
		if(e >= nelem(layertab)){
			e = strtoul(f[0], &s, 0);
			if(*s != '\0')
				continue;
		}
		k[nk].e = e;
		k[nk].c = strtoul(f[1], &s, 0);
		if(*s != '\0')
			continue;
		k[nk].r = strtoul(f[2], &s, 0);
		if(*s != '\0')
			continue;
		if(++nk >= nelem(k))
			break;
	}
	Bterm(kbmap);

	if((kbmap = Bopen("/dev/kbmap", OWRITE)) == nil)
		sysfatal("%r");
	threadnotify(kmreset, 1);

	snprint(tmp, sizeof(tmp), "-pid %d -dx %d -dy %d", getpid(), 256, 256);
	newwindow(tmp);

	if(initdraw(nil, nil, "evdump") < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	a[Cmouse].c = mctl->c;
	a[Cresize].c = mctl->resizec;

	proccreate(kbproc, nil, mainstacksize);
	proccreate(wctlproc, nil, mainstacksize);

	for(;;){
		draw(screen, screen->r, display->black, nil, ZP);

		switch(alt(a)){
		case -1:
			goto end;

		case Cmouse:
			print(
				"mouse buttons 0x%x x %d y %d\n",
				m.buttons,
				m.xy.x, m.xy.y
			);
			break;

		case Cresize:
			getwindow(display, Refnone);
			print(
				"resize min %d %d max %d %d\n",
				screen->r.min.x, screen->r.min.y,
				screen->r.max.x, screen->r.max.y
			);
			break;
		}
	}

end:
	threadexitsall(nil);
}
