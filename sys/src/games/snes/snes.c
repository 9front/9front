#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include <ctype.h>
#include "dat.h"
#include "fns.h"

uchar *prg, *sram;
int nprg, nsram, hirom, battery;

int ppuclock, spcclock, dspclock, stimerclock, saveclock, msgclock, paused, perfclock, cpupause;
Mousectl *mc;
QLock pauselock;
u32int keys;
int savefd, scale, profile, mouse, loadreq, savereq;
Rectangle picr;
Image *tmp, *bg;

void
flushram(void)
{
	if(savefd >= 0)
		pwrite(savefd, sram, nsram, 0);
	saveclock = 0;
}

void
loadrom(char *file)
{
	int fd;
	vlong size;

	fd = open(file, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	size = seek(fd, 0, 2);
	if(size < 0)
		sysfatal("seek: %r");
	if(size == 0)
		sysfatal("empty file");
	if((size & 1023) == 512){
		size -= 512;
		seek(fd, 512, 0);
	}else if((size & 1023) == 0)
		seek(fd, 0, 0);
	else
		sysfatal("invalid rom size");
	if(size >= 16*1048576)
		sysfatal("rom too big");
	nprg = (size + 32767) / 32768;
	prg = malloc(nprg * 32768);
	if(prg == nil)
		sysfatal("malloc: %r");
	if(readn(fd, prg, size) < size)
		sysfatal("read: %r");
	close(fd);
	if(hirom < 0){
		hirom = 0;
		if((memread(0xffd5) & ~0x10) != 0x20)
			if((memread(0x1ffd5) & ~0x10) == 0x21)
				hirom = 1;
			else
				sysfatal("invalid rom (ffd5 = %.2x, 1ffd5 = %.2x)", memread(0xffd5), memread(0x1ffd5));
	}
	if(hirom)
		nprg >>= 1;
	switch(memread(0xffd6)){
	case 0:
		break;
	case 2:
		battery++;
	case 1:
		nsram = memread(0xffd8);
		if(nsram == 0)
			break;
		if(nsram >= 0x0c)
			sysfatal("invalid rom (too much ram specified)");
		nsram = 1<<(nsram + 10);
		sram = malloc(nsram);
		if(sram == nil)
			sysfatal("malloc: %r");
		break;
	default:
		print("unknown rom type %d\n", memread(0xffd5));
	}
}

void
loadbat(char *file)
{
	static char buf[512];
	char *s;

	if(battery && nsram != 0){
		strncpy(buf, file, sizeof buf - 5);
		s = buf + strlen(buf) - 4;
		if(s < buf || strcmp(s, ".smc") != 0)
			s += 4;
		strcpy(s, ".sav");
		savefd = create(buf, ORDWR | OEXCL, 0666);
		if(savefd < 0)
			savefd = open(buf, ORDWR);
		if(savefd < 0)
			message("open: %r");
		else
			readn(savefd, sram, nsram);
		atexit(flushram);
	}
}

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
			if(utfrune(buf, KF|5))
				savereq = 1;
			if(utfrune(buf, KF|6))
				loadreq = 1;
			if(utfrune(buf, Kdel)){
				close(fd);
				threadexitsall(nil);
			}
			if(utfrune(buf, 't'))
				trace = !trace;
		}
		if(buf[0] != 'k' && buf[0] != 'K')
			continue;
		s = buf + 1;
		k = 0xffff;
		while(*s != 0){
			s += chartorune(&r, s);
			switch(r){
			case Kdel: close(fd); threadexitsall(nil);
			case 'z': k |= 1<<31; break;
			case 'x': k |= 1<<23; break;
			case 'a': k |= 1<<30; break;
			case 's': k |= 1<<22; break;
			case 'q': k |= 1<<21; break;
			case 'w': k |= 1<<20; break;
			case Kshift: k |= 1<<29; break;
			case 10: k |= 1<<28; break;
			case Kup: k |= 1<<27; break;
			case Kdown: k |= 1<<26; break;
			case Kleft: k |= 1<<25; break;
			case Kright: k |= 1<<24; break;
			case Kesc:
				if(paused)
					qunlock(&pauselock);
				else
					qlock(&pauselock);
				paused = !paused;
				break;
			}
		}
		if(!mouse)
			keys = k;
	}
}

void
screeninit(void)
{
	Point p;

	originwindow(screen, Pt(0, 0), screen->r.min);
	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = (Rectangle){subpt(p, Pt(scale * 128, scale * 112)), addpt(p, Pt(scale * 128, scale * 112))};
	tmp = allocimage(display, Rect(0, 0, scale * 256, scale > 1 ? 1 : scale * 239), RGB15, scale > 1, 0);
	bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	draw(screen, screen->r, bg, nil, ZP);	
}

void
timing(void)
{
	static vlong old;
	static char buf[32];
	vlong new;
	
	new = nsec();
	if(new != old)
		sprint(buf, "%6.2f%%", 1e11 / (new - old));
	else
		buf[0] = 0;
	draw(screen, Rect(10, 10, 200, 30), bg, nil, ZP);
	string(screen, Pt(10, 10), display->black, ZP, display->defaultfont, buf);
	old = nsec();
}

void
threadmain(int argc, char **argv)
{
	int t;
	extern u16int pc;

	scale = 1;
	hirom = -1;
	ARGBEGIN {
	case '2':
		scale = 2;
		break;
	case '3':
		scale = 3;
		break;
	case 'a':
		audioinit();
		break;
	case 's':
		battery++;
		break;
	case 'm':
		mouse++;
		keys = 1<<16;
		break;
	case 'h':
		hirom++;
		break;
	case 'T':
		profile++;
		break;
	default:
		goto usage;
	} ARGEND;
	
	if(argc != 1){
usage:
		fprint(2, "usage: %s [-23s] rom\n", argv0);
		threadexitsall("usage");
	}
	loadrom(argv[0]);
	if(initdraw(nil, nil, nil) < 0)
		sysfatal("initdraw: %r");
	mc = initmouse(nil, screen);
	if(mc == nil)
		sysfatal("initmouse: %r");
	screeninit();
	loadbat(argv[0]);
	proccreate(keyproc, 0, 8192);
	cpureset();
	memreset();
	spcreset();
	dspreset();
	for(;;){
		if(savereq){
			savestate("snes.save");
			savereq = 0;
		}
		if(loadreq){
			loadstate("snes.save");
			loadreq = 0;
		}
		if(paused){
			qlock(&pauselock);
			qunlock(&pauselock);
		}
		if(cpupause){
			t = 40;
			cpupause = 0;
		}else
			t = cpustep();
		spcclock -= t;
		stimerclock += t;
		ppuclock += t;
		dspclock += t;
		perfclock -= t;

		while(ppuclock >= 4){
			ppustep();
			ppuclock -= 4;
		}
		while(spcclock < 0)
			spcclock += spcstep() * SPCDIV;
		while(stimerclock >= SPCDIV*16){
			spctimerstep();
			stimerclock -= SPCDIV*16;
		}
		while(dspclock >= SPCDIV){
			dspstep();
			dspclock -= SPCDIV;
		}
		if(saveclock > 0){
			saveclock -= t;
			if(saveclock <= 0)
				flushram();
		}
		if(msgclock > 0){
			msgclock -= t;
			if(msgclock <= 0){
				draw(screen, screen->r, bg, nil, ZP);
				msgclock = 0;
			}
		}
		if(profile && perfclock <= 0){
			perfclock = FREQ;
			timing();
		}
	}
}

void
flush(void)
{
	extern uchar pic[256*240*2*3];
	Mouse m;
	Point p;

	if(nbrecvul(mc->resizec) > 0){
		if(getwindow(display, Refnone) < 0)
			sysfatal("resize failed: %r");
		screeninit();
	}
	while(nbrecv(mc->c, &m) > 0)
		if(mouse && ptinrect(m.xy, picr)){
			p = subpt(m.xy, picr.min);
			p.x /= scale;
			p.y /= scale;
			keys = keys & 0xff3f0000 | p.x | p.y << 8;
			if((m.buttons & 1) != 0)
				keys |= 1<<22;
			if((m.buttons & 4) != 0)
				keys |= 1<<23;
			if((m.buttons & 2) != 0)
				lastkeys = keys;
		}
	if(scale == 1){
		loadimage(tmp, tmp->r, pic, 256*239*2);
		draw(screen, picr, tmp, nil, ZP);
	} else {
		Rectangle r;
		uchar *s;
		int w;

		s = pic;
		r = picr;
		w = 256*2*scale;
		while(r.min.y < picr.max.y){
			loadimage(tmp, tmp->r, s, w);
			s += w;
			r.max.y = r.min.y+scale;
			draw(screen, r, tmp, nil, ZP);
			r.min.y = r.max.y;
		}
	}
	flushimage(display, 1);
	audioout();
}

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
