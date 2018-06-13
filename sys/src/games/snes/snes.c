#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

uchar *prg, *sram;
int nprg, nsram, hirom, battery;

int ppuclock, spcclock, dspclock, stimerclock, saveclock, msgclock, cpupause;
Channel *msgc;
int savefd, mouse;

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
		buf[sizeof buf - 1] = 0;
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
usage(void)
{
	fprint(2, "usage: %s [-23ahmsT] [-x scale] rom\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	int t;
	extern u16int pc;

	hirom = -1;
	ARGBEGIN {
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
	case 'x':
		fixscale = strtol(EARGF(usage()), nil, 0);
		break;
	default:
		usage();
	} ARGEND;
	if(argc < 1)
		usage();
	loadrom(argv[0]);
	initemu(256, 239, 2, RGB15, !mouse, nil);
	regkey("b", 'z', 1<<31);
	regkey("a", 'x', 1<<23);
	regkey("y", 'a', 1<<30);
	regkey("x", 's', 1<<22);
	regkey("l1", 'q', 1<<21);
	regkey("r1", 'w', 1<<20);
	regkey("control", Kshift, 1<<29);
	regkey("start", '\n', 1<<28);
	regkey("up", Kup, 1<<27);
	regkey("down", Kdown, 1<<26);
	regkey("left", Kleft, 1<<25);
	regkey("right", Kright, 1<<24);
	msgc = chancreate(sizeof(char*), 1);
	loadbat(argv[0]);
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
				sendp(msgc, nil);	/* clear message */
				msgclock = 0;
			}
		}
	}
}

void
flush(void)
{
	char *s;
	Mouse m;
	Point p;

	extern Rectangle picr;
	extern Mousectl *mc;
	flushmouse(!mouse);
	while(mouse && nbrecv(mc->c, &m) > 0){
		if(ptinrect(m.xy, picr)){
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
	}
	flushscreen();
	while(nbrecv(msgc, &s) > 0){
		if(s != nil){
			string(screen, addpt(screen->r.min, Pt(10, 10)), display->black, ZP, 
				display->defaultfont, s);
			free(s);
			flushimage(display, 1);
		}
	}
	flushaudio(audioout);
}

void
message(char *fmt, ...)
{
	va_list va;
	
	va_start(va, fmt);
	sendp(msgc, vsmprint(fmt, va));
	msgclock = FREQ;
	va_end(va);
}
