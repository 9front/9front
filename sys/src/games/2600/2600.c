#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int *rom, *rop;
u16int bnk[8];
int mask = 0xfff;

void
togdifc(void)
{
	p0difc ^= 1<<6;
}

void
togbw(void)
{
	bwmod ^= 1<<3;
}

static void
loadrom(char *name)
{
	int i, sz, fd;

	fd = open(name, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	sz = seek(fd, 0, 2);
	switch(sz){
	case 0x800: mask = 0x7ff;
	case 0x1000: break;
	case 0x3000: bnk[6] = 2<<12;
	case 0x2000: bnk[5] = 1<<12; break;
	case 0x4000: for(i=1; i<4; bnk[i+2] = i<<12, i++); break;
	case 0x8000: for(i=1; i<8; bnk[i] = i<<12, i++); break;
	default: sysfatal("unsupported ROM size");
	}
	rom = malloc(sz);
	if(rom == nil)
		sysfatal("malloc: %r");
	rop = rom;
	pread(fd, rom, sz, 0);
	close(fd);
}

void
usage(void)
{
	fprint(2, "usage: %s [-a] [-x scale] rom\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	ARGBEGIN {
	case 'a':
		initaudio();
		break;
	case 'x':
		fixscale = strtol(EARGF(usage()), nil, 0);
		break;
	default:
		usage();
	} ARGEND;
	if(argc != 1)
		usage();
	loadrom(argv[0]);
	initemu(PICW, PICH, 4, XRGB32, 1, nil);
	regkey("a", ' ', 1<<4);
	regkey("start", 'q', 1<<5);
	regkey("control", 'w', 1<<6);
	regkey("up", Kup, 1<<0);
	regkey("down", Kdown, 1<<1);
	regkey("left", Kleft, 1<<2);
	regkey("right", Kright, 1<<3);
	regkeyfn('e', togdifc);
	regkeyfn('r', togbw);

	pc = memread(0xFFFC) | memread(0xFFFD) << 8;
	rP = FLAGI;
	for(;;){
		if(paused){
			qlock(&pauselock);
			qunlock(&pauselock);
		}
		step();
	}
}

void
flush(void)
{
	flushmouse(1);
	flushscreen();
	flushaudio(audioout);
}
