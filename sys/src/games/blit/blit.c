#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include <cursor.h>
#include "dat.h"
#include "fns.h"

int baud = 40000;
int scale = 1;
Rectangle picr;
Image *tmp, *bg;
Channel *keych, *uartrxch, *uarttxch;
Mousectl *mc;
int daddr;
u16int dstat;
u8int invert;
int vblctr, uartrxctr;
Rectangle updated;
u32int colbgv, colfgv;
Image *colbg, *colfg;
int realcolors;

static void
screeninit(void)
{
	Point p;

	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = (Rectangle){subpt(p, Pt(scale * SX/2, scale * SY/2)), addpt(p, Pt(scale * SX/2, scale * SY/2))};
	if(picr.min.x < screen->r.min.x){
		picr.max.x += screen->r.min.x - picr.min.x;
		picr.min.x = screen->r.min.x;
	}
	if(picr.min.y < screen->r.min.y){
		picr.max.y += screen->r.min.y - picr.min.y;
		picr.min.y = screen->r.min.y;
	}
	freeimage(tmp);
	tmp = allocimage(display, Rect(0, 0, scale * SX, scale > 1 ? 1 : scale * SY), CHAN1(CMap, 1), scale > 1, 0);
	draw(screen, screen->r, bg, nil, ZP);
	updated = Rect(0, 0, SX, SY);	
}

static void
redraw(void)
{
	static uchar pic[SX*SY/8];
	ushort *p;
	uchar *q;
	int o, n;
	Mouse m;
	Rectangle r;
		
	if(nbrecvul(mc->resizec) > 0){
		if(getwindow(display, Refnone) < 0)
			sysfatal("resize failed: %r");
		screeninit();
	}
	while(nbrecv(mc->c, &m) > 0){
		if(ptinrect(m.xy, picr)){
			mousex = picr.max.x - m.xy.x - 1;
			mousey = picr.max.y - m.xy.y - 1;
		}
		n = m.buttons >> 2 & 1 | m.buttons & 2 | m.buttons << 2 & 4;
		if(n != mousebut){
			mousebut = n;
			irq |= INTMOUSE;
		}
	}

	if(Dy(updated) <= 0 || Dx(updated) <= 0)
		return;

	assert(daddr + sizeof(pic) <= sizeof(ram));

	r = tmp->r;
	if(updated.min.y > r.min.y)
		r.min.y = updated.min.y;
	if(updated.max.y < r.max.y)
		r.max.y = updated.max.y;

	o = r.min.y*(SX/8);
	p = ram + (daddr + o) / 2;
	q = pic + o;
	for(n = Dy(r)*(SX/16); --n >= 0; ){
		*q++ = invert ^ *p >> 8;
		*q++ = invert ^ *p++;
	}

	loadimage(tmp, r, pic+o, Dy(r)*(SX/8));
	if(realcolors){
		draw(screen, rectaddpt(r, picr.min), colfg, nil, r.min);
		draw(screen, rectaddpt(r, picr.min), colbg, tmp, r.min);
	}else
		draw(screen, rectaddpt(r, picr.min), tmp, nil, r.min);
	updated = Rect(SX, SY, 0, 0);
	flushimage(display, 1);
}

static uchar
keymap[] = {
	[Kup-KF] 0xf1,
	[Kdown-KF] 0xf2,
	[Kleft-KF] 0xf3,
	[Kright-KF] 0xf4,
	[1] 0xf6, /* PF1 */
	[2] 0xf7, /* PF2 */
	[3] 0xf8, /* PF3 */
	[4] 0xf9, /* PF4 */
	[12] 0xfe, /* SET-UP */
	[Kpgdown-KF] 0xb0, /* SCROLL */
	[Kins-KF] 0xe0, /* BREAK */
};

static void
keyproc(void *)
{
	int fd, cfd, ch, rc;
	static char buf[256];
	char *p;
	Rune r;

	fd = open("/dev/cons", OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	cfd = open("/dev/consctl", OWRITE);
	if(cfd < 0)
		sysfatal("open: %r");
	fprint(cfd, "rawon");
	for(;;){
		rc = read(fd, buf, sizeof(buf) - 1);
		if(rc <= 0)
			sysfatal("read /dev/cons: %r");
		for(p = buf; p < buf + rc && (p += chartorune(&r, p)); ){
			if(r == Kend){
				close(fd);
				threadexitsall(nil);
			}
			ch = r;
			if(ch == '\n') ch = '\r';
			else if(ch >= KF){
				if(ch >= KF + nelem(keymap)) continue;
				ch = keymap[ch - KF];
				if(ch == 0) continue;
			}else if(ch >= 0x80) continue;
			send(keych, &ch);
		}
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-b baud] [-C bg,fg] [-d] [-t [net!]host[!service]]\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	int n, ms;
	static Cursor blank;
	char *telnet;
	char *p;
	extern int diag;
	
	ms = 0;
	telnet = nil;
	ARGBEGIN{
	case 'b':
		baud = strtol(EARGF(usage()), &p, 0);
		if(*p != 0) usage();
		break;
	case 't':
		telnet = EARGF(usage());
		break;
	case 'C':
		if(realcolors) usage();
		realcolors++;
		p = EARGF(usage());
		colbgv = strtol(p, &p, 16) << 8 | 0xff;
		if(*p++ != ',') usage();
		colfgv = strtol(p, &p, 16) << 8 | 0xff;
		if(*p != 0) usage();
		break;		
	case 'd':
		diag++;
		break;
	case 'm':
		ms++;
		break;
	default: usage();
	}ARGEND;
	if(argc != 0) usage();
	
	keych = chancreate(sizeof(int), 64);
	uartrxch = chancreate(sizeof(int), 128);
	uarttxch = chancreate(sizeof(int), 128);
	if(telnet != nil) telnetinit(telnet);
	meminit();
	if(initdraw(nil, nil, nil) < 0)
		sysfatal("initdraw: %r");

	bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	colbg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, colbgv);
	colfg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, colfgv);	
	screeninit();
	proccreate(keyproc, nil, mainstacksize);
	mc = initmouse(nil, screen);
	if(mc == nil)
		sysfatal("initmouse: %r");
	if(ms == 0)
		setcursor(mc, &blank);

	cpureset();
	for(;;){
		keycheck();
		n = step();
		vblctr += n;
		if(vblctr >= VBLDIV){
			irq |= INTVBL;
			redraw();
			vblctr -= VBLDIV;
		}
		if(uartrxctr > 0)
			uartrxctr -= n;
	}
}
