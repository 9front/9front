#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <keyboard.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

extern uchar ppuram[16384];
int nprg, nchr, map, chrram;
uchar *prg, *chr;
int clock, ppuclock, apuclock, dmcclock, dmcfreq, sampclock, msgclock, saveclock;
int oflag, savefd = -1;
int mirr;

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
flushram(void)
{
	if(savefd >= 0)
		pwrite(savefd, mem + 0x6000, 0x2000, 0);
	saveclock = 0;
}

void
loadrom(char *file, int sflag)
{
	int fd;
	int nes20;
	char *s;
	static uchar header[16];
	static u32int flags;
	static char buf[512];
	
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
		nchr = 1;
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
	if(sflag){
		strncpy(buf, file, sizeof buf - 5);
		s = buf + strlen(buf) - 4;
		if(s < buf || strcmp(s, ".nes") != 0)
			s += 4;
		strcpy(s, ".sav");
		savefd = create(buf, ORDWR | OEXCL, 0666);
		if(savefd < 0)
			savefd = open(buf, ORDWR);
		if(savefd < 0)
			message("open: %r");
		else
			readn(savefd, mem + 0x6000, 0x2000);
		atexit(flushram);
	}
	mapper[map](INIT, 0);
}

void
usage(void)
{
	fprint(2, "usage: %s [-aos] [-x scale] rom\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	int t, sflag;

	sflag = 0;
	ARGBEGIN {
	case 'a':
		initaudio();
		break;
	case 'o':
		oflag = 1;
		break;
	case 's':
		sflag = 1;
		break;
	case 'x':
		fixscale = strtol(EARGF(usage()), nil, 0);
		break;
	default:
		usage();
	} ARGEND;
	if(argc < 1)
		usage();
	loadrom(argv[0], sflag);
	initemu(256, 240 - oflag * 16, 4, XRGB32, 1, nil);
	regkey("b", 'z', 1<<1);
	regkey("a", 'x', 1<<0);
	regkey("control", Kshift, 1<<2);
	regkey("start", '\n', 1<<3);
	regkey("up", Kup, 1<<4);
	regkey("down", Kdown, 1<<5);
	regkey("left", Kleft, 1<<6);
	regkey("right", Kright, 1<<7);

	pc = memread(0xFFFC) | memread(0xFFFD) << 8;
	rP = FLAGI;
	dmcfreq = 12 * 428;
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
		apuclock += t;
		sampclock += t;
		dmcclock += t;
		while(ppuclock >= 4){
			ppustep();
			ppuclock -= 4;
		}
		if(apuclock >= APUDIV){
			apustep();
			apuclock -= APUDIV;
		}
		if(sampclock >= SAMPDIV){
			audiosample();
			sampclock -= SAMPDIV;
		}
		if(dmcclock >= dmcfreq){
			dmcstep();
			dmcclock -= dmcfreq;
		}
		if(msgclock > 0){
			msgclock -= t;
			if(msgclock <= 0){
				extern Image *bg;
				draw(screen, screen->r, bg, nil, ZP);	
				msgclock = 0;
			}
		}
		if(saveclock > 0){
			saveclock -= t;
			if(saveclock <= 0)
				flushram();
		}
	}
}
