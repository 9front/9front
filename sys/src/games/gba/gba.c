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
int savefd, saveframes;

char *biosfile = "/sys/games/lib/gbabios.bin";

int ppuclock;

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
flushback(void)
{
	if(savefd >= 0)
		pwrite(savefd, back, nback, BACKTYPELEN);
	saveframes = 0;
}

void
loadbios(void)
{
	extern uchar bios[16384];

	int fd;
	
	fd = open(biosfile, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	readn(fd, bios, 16384);
	close(fd);
}

int
romtype(int *size)
{
	u32int *p, n, v;
	union {char a[4]; u32int u;} s1 = {"EEPR"}, s2 = {"SRAM"}, s3 = {"FLAS"};
	
	p = (u32int *) rom;
	n = nrom / 4;
	do{
		v = *p++;
		if(v == s1.u && memcmp(p - 1, "EEPROM_V", 8) == 0){
			print("backup type is either eeprom4 or eeprom64 -- can't detect which one\n");
			return NOBACK;
		}
		if(v == s2.u && memcmp(p - 1, "SRAM_V", 6) == 0){
			*size = 32*KB;
			return SRAM;
		}
		if(v == s3.u){
			if(memcmp(p - 1, "FLASH_V", 7) == 0 || memcmp(p - 1, "FLASH512_V", 10) == 0){
				*size = 64*KB;
				return FLASH;
			}
			if(memcmp(p - 1, "FLASH1M_V", 9) == 0){
				*size = 128*KB;
				return FLASH;
			}
		}
	}while(--n);
	return NOBACK;
}

int
parsetype(char *s, int *size)
{
	if(strcmp(s, "eeprom4") == 0){
		*size = 512;
		return EEPROM;
	}else if(strcmp(s, "eeprom64") == 0){
		*size = 8*KB;
		return EEPROM;
	}else if(strcmp(s, "sram256") == 0){
		*size = 32*KB;
		return SRAM;
	}else if(strcmp(s, "flash512") == 0){
		*size = 64*KB;
		return FLASH;
	}else if(strcmp(s, "flash1024") == 0){
		*size = 128*KB;
		return FLASH;
	}else
		return NOBACK;
}

void
typename(char *s, int type, int size)
{
	char *st;
	switch(type){
	case EEPROM:
		st = "eeprom";
		break;
	case FLASH:
		st = "flash";
		break;
	case SRAM:
		st = "sram";
		break;
	default:
		sysfatal("typestr: unknown type %d -- shouldn't happen", type);
		return;
	}
	snprint(s, BACKTYPELEN, "%s%d", st, size/128);
}

void
loadsave(char *file)
{
	char *buf, *p;
	char tstr[BACKTYPELEN];
	int type, size;
	
	buf = emalloc(strlen(file) + 4);
	strcpy(buf, file);
	p = strchr(buf, '.');
	if(p == nil)
		p = buf + strlen(buf);
	strcpy(p, ".sav");
	savefd = open(buf, ORDWR);
	if(savefd < 0){
		if(backup == NOBACK){
			backup = romtype(&nback);
			if(backup == NOBACK){
				fprint(2, "failed to autodetect save format\n");
				free(buf);
				return;
			}
		}
		savefd = create(buf, OWRITE, 0664);
		if(savefd < 0){
			fprint(2, "create: %r");
			free(buf);
			return;
		}
		memset(tstr, 0, sizeof(tstr));
		typename(tstr, backup, nback);
		write(savefd, tstr, sizeof(tstr));
		back = emalloc(nback);
		memset(back, 0, nback);
		write(savefd, back, nback);
		free(buf);
		atexit(flushback);
		return;
	}
	readn(savefd, tstr, sizeof(tstr));
	tstr[31] = 0;
	type = parsetype(tstr, &size);
	if(type == NOBACK || backup != NOBACK && (type != backup || nback != size))
		sysfatal("%s: invalid format", buf);
	backup = type;
	nback = size;
	back = emalloc(nback);
	readn(savefd, back, nback);
	atexit(flushback);
	free(buf);
}

void
loadrom(char *file)
{
	int fd;
	vlong sz;
	
	fd = open(file, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	sz = seek(fd, 0, 2);
	if(sz <= 0 || sz >= 32*1024*1024)
		sysfatal("nope.jpg");
	seek(fd, 0, 0);
	nrom = sz;
	rom = emalloc(nrom);
	if(readn(fd, rom, sz) < sz)
		sysfatal("read: %r");
	close(fd);
	loadsave(file);
	if(nrom == 32*KB*KB && backup == EEPROM)
		nrom -= 256;
}

void
screeninit(void)
{
	Point p;

	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = (Rectangle){subpt(p, Pt(scale * 120, scale * 80)), addpt(p, Pt(scale * 120, scale * 80))};
	tmp = allocimage(display, Rect(0, 0, scale * 240, scale > 1 ? 1 : scale * 160), CHAN4(CIgnore, 1, CBlue, 5, CGreen, 5, CRed, 5), scale > 1, 0);
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
			/*if(utfrune(buf, KF|5))
				savereq = 1;
			if(utfrune(buf, KF|6))
				loadreq = 1;*/
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
			case 'z': k |= 1<<1; break;
			case 'x': k |= 1<<0; break;
			case 'a': k |= 1<<9; break;
			case 's': k |= 1<<8; break;
			case Kshift: k |= 1<<2; break;
			case 10: k |= 1<<3; break;
			case Kup: k |= 1<<6; break;
			case Kdown: k |= 1<<7; break;
			case Kleft: k |= 1<<5; break;
			case Kright: k |= 1<<4; break;
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
	int x;

	if(nbrecvul(mc->resizec) > 0){
		if(getwindow(display, Refnone) < 0)
			sysfatal("resize failed: %r");
		screeninit();
	}
	while(nbrecv(mc->c, &m) > 0)
		;
	if(scale == 1){
		loadimage(tmp, tmp->r, pic, 240*160*2);
		draw(screen, picr, tmp, nil, ZP);
	} else {
		Rectangle r;
		uchar *s;
		int w;

		s = pic;
		r = picr;
		w = 240*2*scale;
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
	if(framestep){
		paused = 1;
		qlock(&pauselock);
		framestep = 0;
	}
	
	if(saveframes > 0 && --saveframes == 0)
		flushback();
	
	if((reg[KEYCNT] & 1<<14) != 0){
		x = reg[KEYCNT] & keys;
		if((reg[KEYCNT] & 1<<15) != 0){
			if(x == (reg[KEYCNT] & 0x3ff))
				setif(IRQKEY);
		}else
			if(x != 0)
				setif(IRQKEY);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-23T] [-s savetype] [-b biosfile] rom\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	char *s;
	int t;

	scale = 1;
	ARGBEGIN {
	case '2':
		scale = 2;
		break;
	case '3':
		scale = 3;
		break;
	case 's':
		s = EARGF(usage());
		backup = parsetype(s, &nback);
		if(backup == NOBACK)
			sysfatal("unknown save type '%s'", s);
		break;
	case 'b':
		biosfile = strdup(EARGF(usage()));
		break;
	case 'T':
		profile++;
		break;
	default:
		usage();
	} ARGEND;
	if(argc < 1)
		usage();

	loadbios();
	loadrom(argv[0]);
	
	if(initdraw(nil, nil, nil) < 0)
		sysfatal("initdraw: %r");
	mc = initmouse(nil, screen);
	if(mc == nil)
		sysfatal("initmouse: %r");
	proccreate(keyproc, nil, mainstacksize);
	screeninit();
	
	memreset();
	reset();
	for(;;){
		if(paused){
			qlock(&pauselock);
			qunlock(&pauselock);
		}
		if(dmaact)
			t = dmastep();
		else if(cpuhalt)
			t = 8;
		else
			t = step();
		ppuclock += t;
		while(ppuclock >= 4){
			ppustep();
			ppuclock -= 4;
		}
		timerstep(t);
	}
}
