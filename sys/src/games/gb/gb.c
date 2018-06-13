#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

int cpuhalt;
int backup;
int savefd = -1, saveframes;
ulong clock;
u8int mbc, feat, mode;
extern MBC3Timer timer, timerl;

extern double TAU; 
void
tauup(void)
{
	TAU += 5000;
}
void
taudn(void)
{
	TAU -= 5000;
}

void
writeback(void)
{
	if(saveframes == 0)
		saveframes = 15;
}

void
timerload(uchar *buf)
{
	timer.ns = buf[0] | buf[1] << 8 | buf[2] << 16 | buf[3] << 24 | (uvlong)buf[4] << 32 | (uvlong)buf[5] << 40 | (uvlong)buf[6] << 48LL | (uvlong)buf[7] << 56LL;
	timer.sec = buf[8];
	timer.min = buf[9];
	timer.hr = buf[10];
	timer.dl = buf[11];
	timer.dh = buf[12];
	timerl.sec = buf[13];
	timerl.min = buf[14];
	timerl.hr = buf[15];
	timerl.dl = buf[16];
	timerl.dh = buf[17];
}

void
timersave(uchar *buf)
{
	buf[0] = timer.ns;
	buf[1] = timer.ns >> 8;
	buf[2] = timer.ns >> 16;
	buf[3] = timer.ns >> 24;
	buf[4] = timer.ns >> 32;
	buf[5] = timer.ns >> 40;
	buf[6] = timer.ns >> 48;
	buf[7] = timer.ns >> 56;
	buf[8] = timer.sec;
	buf[9] = timer.min;
	buf[10] = timer.hr;
	buf[11] = timer.dl;
	buf[12] = timer.dh;
	buf[13] = timerl.sec;
	buf[14] = timerl.min;
	buf[15] = timerl.hr;
	buf[16] = timerl.dl;
	buf[17] = timerl.dh;
}

void
flushback(void)
{
	uchar buf[TIMERSIZ];

	if(savefd >= 0){
		pwrite(savefd, back, nback, 0);
		timersave(buf);
		pwrite(savefd, buf, TIMERSIZ, nback);
	}
	saveframes = 0;
}

void
loadsave(char *file)
{
	char *buf, *p;
	uchar tim[TIMERSIZ];

	buf = emalloc(strlen(file) + 4);
	strcpy(buf, file);
	p = strrchr(buf, '.');
	if(p == nil)
		p = buf + strlen(buf);
	strcpy(p, ".sav");
	savefd = open(buf, ORDWR);
	if(savefd < 0){
		savefd = create(buf, OWRITE, 0664);
		if(savefd < 0){
			fprint(2, "create: %r");
			free(buf);
			return;
		}
		back = emalloc(nback);
		memset(back, 0, nback);
		write(savefd, back, nback);
		free(buf);
		if((feat & FEATTIM) != 0){
			timer.ns = nsec();
			timersave(tim);
			write(savefd, tim, TIMERSIZ);
		}
		atexit(flushback);
		return;
	}
	back = emalloc(nback);
	readn(savefd, back, nback);
	if((feat & FEATTIM) != 0){
		readn(savefd, tim, TIMERSIZ);
		timerload(tim);
	}
	atexit(flushback);
	free(buf);
}

void
loadrom(char *file)
{
	int fd;
	vlong sz;
	static uchar nintendo[24] = {
		0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
		0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E
	};
	static u8int mbctab[31] = {
		0, 1, 1, 1, -1, 2, 2, -1,
		0, 0, -1, 6, 6, 6, -1, 3,
		3, 3, 3, 3, -1, 4, 4, 4,
		-1, 5, 5, 5, 5, 5, 5};
	static u8int feattab[31] = {
		0, 0, FEATRAM, FEATRAM|FEATBAT, 0, FEATRAM, FEATRAM|FEATBAT, 0,
		FEATRAM, FEATRAM|FEATBAT, 0, 0, FEATRAM, FEATRAM|FEATBAT, 0, FEATTIM|FEATBAT,
		FEATTIM|FEATRAM|FEATBAT, 0, FEATRAM, FEATRAM|FEATBAT, 0, 0, FEATRAM, FEATRAM|FEATBAT,
		0, 0, FEATRAM, FEATRAM|FEATBAT, 0, FEATRAM, FEATRAM|FEATBAT
	};
	
	fd = open(file, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	sz = seek(fd, 0, 2);
	if(sz <= 0 || sz > 32*1024*1024)
		sysfatal("invalid file size");
	seek(fd, 0, 0);
	nrom = sz;
	rom = emalloc(nrom);
	if(readn(fd, rom, sz) < sz)
		sysfatal("read: %r");
	close(fd);
	if(memcmp(rom + 0x104, nintendo, 24) != 0)
		sysfatal("not a gameboy rom");
	if(rom[0x147] > 0x1f)
		sysfatal("unsupported mapper ([0x147] = %.2ux)", rom[0x147]);
	mbc = mbctab[rom[0x147]];
	feat = feattab[rom[0x147]];
	if((feat & FEATRAM) != 0){
		switch(rom[0x149]){
		case 0:
			if(mbc == 2)
				nback = 512;
			else
				feat &= ~FEATRAM;
			break;
		case 1: nback = 2048; break;
		case 2: nback = 8192; break;
		case 3: nback = 32768; break;
		default: sysfatal("invalid ram size");
		}
	}
	if(nback == 0)
		nbackbank = 1;
	else
		nbackbank = nback + 8191 >> 13;
	if((feat & (FEATRAM|FEATTIM)) == 0)
		feat &= ~FEATBAT;
	if((rom[0x143] & 0x80) != 0 && (mode & FORCEDMG) == 0)
		mode = CGB|COL;
	if((feat & FEATBAT) != 0)
		loadsave(file);	
	switch(mbc){
	case 0: case 1: case 2: case 3: case 5: break;
	default: sysfatal("unsupported mbc %d", mbc);
	}

}

void
flush(void)
{
	extern uchar pic[];
	static vlong old, delta;

	flushmouse(1);
	flushscreen();
	flushaudio(audioout);
	if(saveframes > 0 && --saveframes == 0)
		flushback();
	if(savereq){
		savestate("gb.save");
		savereq = 0;
	}
	if(loadreq){
		loadstate("gb.save");
		loadreq = 0;
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-aTcd] [-C col0,col1,col2,col3] [-x scale] rom\n", argv0);
	exits("usage");
}

void
colinit(void)
{
	int i;
	union { u8int c[4]; u32int l; } c;
	
	c.c[3] = 0;
	for(i = 0; i < 4; i++){
		c.c[0] = c.c[1] = c.c[2] = i * 0x55;
		moncols[i] = c.l;
	}
}

void
colparse(char *p)
{
	int i;
	union { u8int c[4]; u32int l; } c;
	u32int l;
	
	c.c[3] = 0;
	for(i = 0; i < 4; i++){
		l = strtol(p, &p, 16);
		if(*p != (i == 3 ? 0 : ',') || l > 0xffffff)
			usage();
		p++;
		c.c[0] = l;
		c.c[1] = l >> 8;
		c.c[2] = l >> 16;
		moncols[i] = c.l;
	}
}

void
threadmain(int argc, char **argv)
{
	int t;

	colinit();
	ARGBEGIN {
	case 'a':
		audioinit();
		break;
	case 'c':
		mode |= CGB;
		break;
	case 'd':
		mode |= FORCEDMG;
		break;
	case 'C':
		colparse(EARGF(usage()));
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
	initemu(PICW, PICH, 4, XRGB32, 1, nil);
	regkey("b", 'z', 1<<5);
	regkey("a", 'x', 1<<4);
	regkey("control", Kshift, 1<<6);
	regkey("start", '\n', 1<<7);
	regkey("up", Kup, 1<<2);
	regkey("down", Kdown, 1<<3);
	regkey("left", Kleft, 1<<1);
	regkey("right", Kright, 1<<0);
	regkeyfn(KF|9, tauup);
	regkeyfn(KF|10, taudn);

	eventinit();
	meminit();
	ppuinit();
	reset();
	for(;;){
		if(paused){
			qlock(&pauselock);
			qunlock(&pauselock);
		}
		if(dma > 0)
			t = dmastep();
		else
			t = step();
		if((mode & TURBO) == 0)
			t += t;
		clock += t;
		if((elist->time -= t) <= 0)
			popevent();
	}
}
