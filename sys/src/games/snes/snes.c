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

int ppuclock, spcclock, stimerclock, saveclock, msgclock, paused;
Mousectl *mc;
QLock pauselock;
int keys, savefd;
int scale;
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
	static char buf[512];
	char *s;
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
		k = 0;
		while(*s != 0){
			s += chartorune(&r, s);
			switch(r){
			case Kdel: close(fd); threadexitsall(nil);
			case 'z': k |= 1<<15; break;
			case 'x': k |= 1<<7; break;
			case 'a': k |= 1<<14; break;
			case 's': k |= 1<<6; break;
			case 'q': k |= 1<<5; break;
			case 'w': k |= 1<<4; break;
			case Kshift: k |= 1<<13; break;
			case 10: k |= 1<<12; break;
			case Kup: k |= 1<<11; break;
			case Kdown: k |= 1<<10; break;
			case Kleft: k |= 1<<9; break;
			case Kright: k |= 1<<8; break;
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
screeninit(void)
{
	Point p;

	originwindow(screen, Pt(0, 0), screen->r.min);
	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = (Rectangle){subpt(p, Pt(scale * 128, scale * 112)), addpt(p, Pt(scale * 128, scale * 112))};
	tmp = allocimage(display, Rect(0, 0, scale * 256, scale * 239), RGB15, 0, 0);
	bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	draw(screen, screen->r, bg, nil, ZP);	
}

void
threadmain(int argc, char **argv)
{
	int t;
	extern u16int pc;

	scale = 1;
	ARGBEGIN {
	case 's':
		battery++;
		break;
	} ARGEND;
	
	if(argc != 1){
		fprint(2, "usage: %s rom\n", argv0);
		threadexitsall("usage");
	}
	loadrom(argv[0]);
	if(initdraw(nil, nil, nil) < 0)
		sysfatal("initdraw: %r");
	mc = initmouse(nil, screen);
	if(mc == nil)
		sysfatal("initmouse: %r");
	screeninit();
	proccreate(keyproc, 0, 8192);
	cpureset();
	memreset();
	spcreset();
	for(;;){
		if(paused){
			qlock(&pauselock);
			qunlock(&pauselock);
		}
		t = cpustep() * 8;
		spcclock -= t;
		stimerclock += t;
		ppuclock += t;

		while(ppuclock >= 4){
			ppustep();
			ppuclock -= 4;
		}
		while(spcclock < 0)
			spcclock += spcstep() * SPCDIV;
		if(stimerclock >= SPCDIV*16){
			spctimerstep();
			stimerclock -= SPCDIV*16;
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
	}
}

void
flush(void)
{
	extern uchar pic[256*240*2*9];
	Mouse m;

	while(nbrecv(mc->c, &m) > 0)
		;
	if(nbrecvul(mc->resizec) > 0){
		if(getwindow(display, Refnone) < 0)
			sysfatal("resize failed: %r");
		screeninit();
	}
	loadimage(tmp, tmp->r, pic, 256*239*2*scale*scale);
	draw(screen, picr, tmp, nil, ZP);
	flushimage(display, 1);
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
