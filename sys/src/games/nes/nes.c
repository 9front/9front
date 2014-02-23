#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>
#include "dat.h"
#include "fns.h"

extern uchar ppuram[16384];
int nprg, nchr, map, chrram;
uchar *prg, *chr;
int scale;
Rectangle picr;
Image *tmp, *bg;
int clock, ppuclock, syncclock, syncfreq, checkclock, msgclock, sleeps;
Mousectl *mc;
int keys, paused, savereq, loadreq, oflag;
int mirr;
QLock pauselock;

void
message(char *fmt, ...)
{
	va_list va;
	static char buf[512];
	
	va_start(va, fmt);
	vsnprint(buf, sizeof buf, fmt, va);
	string(screen, Pt(10, 10), display->black, ZP, display->defaultfont, buf);
	msgclock = FREQ;
	va_end(va);
}

void
loadrom(char *file)
{
	int fd;
	int nes20;
	static uchar header[16];
	static u32int flags;
	
	fd = open(file, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	if(readn(fd, header, sizeof(header)) < sizeof(header))
		sysfatal("read: %r");
	if(memcmp(header, "NES\x1a", 4) != 0)
		sysfatal("not a ROM");
	if(header[15] != 0)
		memset(header + 7, 0, 9);
	flags = header[6] | header[7] << 8;
	nes20 = (flags & FLNES20M) == FLNES20V;
	if(flags & (FLVS | FLPC10))
		sysfatal("ROM not supported");
	nprg = header[HPRG];
	if(nes20)
		nprg |= (header[HROMH] & 0xf) << 8;
	if(nprg == 0)
		sysfatal("invalid ROM");
	nchr = header[HCHR];
	if(nes20)
		nchr |= (header[HROMH] & 0xf0) << 4;
	map = (flags >> FLMAPPERL) & 0x0f | (((flags >> FLMAPPERH) & 0x0f) << 4);
	if(nes20)
		map |= (header[8] & 0x0f) << 8;
	if(map >= 256 || mapper[map] == nil)
		sysfatal("unimplemented mapper %d", map);

	memset(mem, 0, sizeof(mem));
	if((flags & FLTRAINER) != 0 && readn(fd, mem + 0x7000, 512) < 512)
			sysfatal("read: %r");
	prg = malloc(nprg * PRGSZ);
	if(prg == nil)
		sysfatal("malloc: %r");
	if(readn(fd, prg, nprg * PRGSZ) < nprg * PRGSZ)
		sysfatal("read: %r");
	chrram = nchr == 0;
	if(nchr != 0){
		chr = malloc(nchr * CHRSZ);
		if(chr == nil)
			sysfatal("malloc: %r");
		if(readn(fd, chr, nchr * CHRSZ) < nchr * CHRSZ)
			sysfatal("read: %r");
	}else{
		nchr = 16;
		chr = malloc(nchr * CHRSZ);
		if(chr == nil)
			sysfatal("malloc: %r");
	}
	if((flags & FLFOUR) != 0)
		mirr = MFOUR;
	else if((flags & FLMIRROR) != 0)
		mirr = MVERT;
	else
		mirr = MHORZ;
	mapper[map](-1, 0);
}

extern int trace;

void
keyproc(void *)
{
	int fd, k;
	static char buf[256];
	char *s;
	Rune r;

	fd = open("/dev/kbd", OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	for(;;){
		if(read(fd, buf, sizeof(buf) - 1) <= 0)
			sysfatal("read /dev/kbd: %r");
		if(buf[0] == 'c'){
			if(utfrune(buf, Kdel))
				threadexitsall(nil);
			if(utfrune(buf, KF|5))
				savereq = 1;
			if(utfrune(buf, KF|6))
				loadreq = 1;
			if(utfrune(buf, 't'))
				trace ^= 1;
		}
		if(buf[0] != 'k' && buf[0] != 'K')
			continue;
		s = buf + 1;
		k = 0;
		while(*s != 0){
			s += chartorune(&r, s);
			switch(r){
			case Kdel: threadexitsall(nil);
			case 'x': k |= 1<<0; break;
			case 'z': k |= 1<<1; break;
			case Kshift: k |= 1<<2; break;
			case 10: k |= 1<<3; break;
			case Kup: k |= 1<<4; break;
			case Kdown: k |= 1<<5; break;
			case Kleft: k |= 1<<6; break;
			case Kright: k |= 1<<7; break;
			case Kesc:
				if(paused)
					qunlock(&pauselock);
				else
					qlock(&pauselock);
				paused = !paused;
				break;
			}
		}
		keys = k;
	}
}

void
threadmain(int argc, char **argv)
{
	int t, h;
	Point p;
	uvlong old, new, diff;

	scale = 1;
	h = 240;
	ARGBEGIN {
	case '2':
		scale = 2;
		break;
	case '3':
		scale = 3;
		break;
	case 'o':
		oflag = 1;
		h -= 16;
		break;
	} ARGEND;

	if(argc < 1)
		sysfatal("missing argument");
	loadrom(argv[0]);
	if(initdraw(nil, nil, nil) < 0)
		sysfatal("initdraw: %r");
	mc = initmouse(nil, screen);
	if(mc == nil)
		sysfatal("initmouse: %r");
	proccreate(keyproc, nil, 8192);
	originwindow(screen, Pt(0, 0), screen->r.min);
	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = (Rectangle){subpt(p, Pt(scale * 128, scale * h/2)), addpt(p, Pt(scale * 128, scale * h/2))};
	if(screen->chan != XRGB32)
		tmp = allocimage(display, Rect(0, 0, scale * 256, scale * h), XRGB32, 0, 0);
	bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	draw(screen, screen->r, bg, nil, ZP);
	
	pc = memread(0xFFFC) | memread(0xFFFD) << 8;
	rP = FLAGI;
	syncfreq = FREQ / 30;
	old = nsec();
	for(;;){
		if(savereq){
			savestate("nes.save");
			savereq = 0;
		}
		if(loadreq){
			loadstate("nes.save");
			loadreq = 0;
		}
		if(paused){
			qlock(&pauselock);
			qunlock(&pauselock);
		}
		t = step() * 12;
		clock += t;
		ppuclock += t;
		syncclock += t;
		checkclock += t;
		while(ppuclock >= 4){
			ppustep();
			ppuclock -= 4;
		}
		if(syncclock >= syncfreq){
			sleep(10);
			sleeps++;
			syncclock = 0;
		}
		if(checkclock >= FREQ){
			new = nsec();
			diff = new - old - sleeps * 10 * MILLION;
			diff = BILLION - diff;
			if(diff <= 0)
				syncfreq = FREQ;
			else
				syncfreq = ((vlong)FREQ) * 10 * MILLION / diff;
			if(syncfreq < FREQ / 100)
				syncfreq = FREQ / 100;
			old = new;
			checkclock = 0;
			sleeps = 0;
		}
		if(msgclock > 0){
			msgclock -= t;
			if(msgclock <= 0){
				draw(screen, screen->r, bg, nil, ZP);
				msgclock = 0;
			}
		}
	}
}
