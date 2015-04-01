#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "dat.h"
#include "fns.h"

int cpuhalt;
int scale, profile;
Rectangle picr;
Image *bg, *tmp;
Mousectl *mc;
int keys, paused, framestep, backup;
QLock pauselock;
int savefd = -1, saveframes;
ulong clock;
int savereq, loadreq;
u8int mbc, feat, mode;
extern MBC3Timer timer, timerl;

void *
emalloc(ulong sz)
{
	void *v;
	
	v = malloc(sz);
	if(v == nil)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&sz));
	return v;
}

void
writeback(void)
{
	if(saveframes == 0)
		saveframes = 15;
}

void
timerload(char *buf)
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
timersave(char *buf)
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
	char buf[TIMERSIZ];

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
	char tim[TIMERSIZ];

	buf = emalloc(strlen(file) + 4);
	strcpy(buf, file);
	p = strchr(buf, '.');
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
		timerload(buf);
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
screeninit(void)
{
	Point p;

	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = (Rectangle){subpt(p, Pt(scale * PICW/2, scale * PICH/2)), addpt(p, Pt(scale * PICW/2, scale * PICH/2))};
	tmp = allocimage(display, Rect(0, 0, scale * PICW, scale > 1 ? 1 : scale * PICH), XRGB32, scale > 1, 0);
	bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	draw(screen, screen->r, bg, nil, ZP);	
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
		k = 0;
		while(*s != 0){
			s += chartorune(&r, s);
			switch(r){
			case Kdel: close(fd); threadexitsall(nil);
			case 'z': k |= 1<<5; break;
			case 'x': k |= 1<<4; break;
			case Kshift: k |= 1<<6; break;
			case 10: k |= 1<<7; break;
			case Kup: k |= 1<<2; break;
			case Kdown: k |= 1<<3; break;
			case Kleft: k |= 1<<1; break;
			case Kright: k |= 1<<0; break;
			case Kesc:
				if(paused)
					qunlock(&pauselock);
				else
					qlock(&pauselock);
				paused = !paused;
				break;
			case KF|1:	
				if(paused){
					qunlock(&pauselock);
					paused=0;
				}
				framestep = !framestep;
				break;
			}
		}
		k &= ~(k << 1 & 0x0a | k >> 1 & 0x05);
		keys = k;
	}

}

void
timing(void)
{
	static int fcount;
	static vlong old;
	static char buf[32];
	vlong new;
	
	if(++fcount == 60)
		fcount = 0;
	else
		return;
	new = nsec();
	if(new != old)
		sprint(buf, "%6.2f%%", 1e11 / (new - old));
	else
		buf[0] = 0;
	draw(screen, rectaddpt(Rect(10, 10, 200, 30), screen->r.min), bg, nil, ZP);
	string(screen, addpt(screen->r.min, Pt(10, 10)), display->black, ZP, display->defaultfont, buf);
	old = nsec();
}

void
flush(void)
{
	extern uchar pic[];
	Mouse m;
	static vlong old, delta;
	vlong new, diff;

	if(nbrecvul(mc->resizec) > 0){
		if(getwindow(display, Refnone) < 0)
			sysfatal("resize failed: %r");
		screeninit();
	}
	while(nbrecv(mc->c, &m) > 0)
		;
	if(scale == 1){
		loadimage(tmp, tmp->r, pic, PICW*PICH*4);
		draw(screen, picr, tmp, nil, ZP);
	} else {
		Rectangle r;
		uchar *s;
		int w;

		s = pic;
		r = picr;
		w = PICW*4*scale;
		while(r.min.y < picr.max.y){
			loadimage(tmp, tmp->r, s, w);
			s += w;
			r.max.y = r.min.y+scale;
			draw(screen, r, tmp, nil, ZP);
			r.min.y = r.max.y;
		}
	}
	flushimage(display, 1);
	if(profile)
		timing();
	if(audioout() < 0){
		new = nsec();
		diff = 0;
		if(old != 0){
			diff = BILLION/60 - (new - old) - delta;
			if(diff >= MILLION)
				sleep(diff/MILLION);
		}
		old = nsec();
		if(diff != 0){
			diff = (old - new) - (diff / MILLION) * MILLION;
			delta += (diff - delta) / 100;
		}
	}
	if(framestep){
		paused = 1;
		qlock(&pauselock);
		framestep = 0;
	}
	
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
	fprint(2, "usage: %s [-23aTcd] [-C col0,col1,col2,col3] rom\n", argv0);
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
	scale = 1;
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
	case 'T':
		profile++;
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
	default:
		usage();
	} ARGEND;
	if(argc < 1)
		usage();

	loadrom(argv[0]);
	
	if(initdraw(nil, nil, nil) < 0)
		sysfatal("initdraw: %r");
	mc = initmouse(nil, screen);
	if(mc == nil)
		sysfatal("initmouse: %r");
	proccreate(keyproc, nil, mainstacksize);
	screeninit();

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
