#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

char *bindir = "/sys/lib/c64";
Image *red;
Rectangle progr;
u8int *rom;
int nrom;
u16int joys;
uchar *tape, tapever, tapeplay;
ulong tapelen;
int joymode;

void
progress(int a, int b)
{
	static int cur;
	int w;

	extern Image *bg;
	if(b == 0 || a == 0){
		if(cur != 0){
			draw(screen, progr, bg, nil, ZP);
			cur = 0;
		}
		return;
	}
	w = a * Dx(progr) / b;
	if(cur == w)
		return;
	draw(screen, Rect(progr.min.x, progr.min.y, progr.min.x + w, progr.max.y), red, nil, ZP);
	cur = w;
}

static void
loadsys(char *name, u8int *p, int n)
{
	static char buf[256];
	int fd;

	snprint(buf, sizeof(buf), "%s/%s", bindir, name);
	fd = open(buf, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	if(readn(fd, p, n) < n)
		sysfatal("readn: %r");
	close(fd);
}

static void
loadrom(char *name)
{
	int fd;
	
	fd = open(name, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	nrom = seek(fd, 0, 2);
	if(nrom > 4096)
		sysfatal("large ROM not supported");
	if((nrom & nrom-1) != 0)
		sysfatal("non-power-of-two ROM size");
	rom = malloc(nrom);
	if(rom == nil)
		sysfatal("malloc: %r");
	pread(fd, rom, nrom, 0);
	close(fd);
}

static void
loadcart(char *name)
{
	int fd;
	u16int t, l;
	u8int buf[80];

	fd = open(name, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	read(fd, buf, 80);
	if(memcmp(buf, "C64 CARTRIDGE   ", 16) != 0)
		sysfatal("not a c64 cartridge");
	t = buf[0x16] << 8 | buf[0x17];
	if(t != 0)
		sysfatal("unsupported type %d", t);
	if(buf[0x18] == 0) pla &= ~EXROM;
	if(buf[0x19] == 0) pla &= ~GAME;
	t = buf[0x4c] << 8 | buf[0x4d];
	if(t < 0x8000 || t >= 0xc000 && t < 0xe000)
		sysfatal("odd starting address %x", t);
	if(t >= 0xe000)
		t -= 0x4000;
	t -= 0x8000;
	l = buf[0x4e] << 8 | buf[0x4f];
	if(l + t > 16384)
		sysfatal("cart too large");
	read(fd, cart + t, l);
	close(fd);
}

static void
loadtape(char *name)
{
	int fd;
	uchar buf[20];
	
	fd = open(name, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	read(fd, buf, 20);
	if(memcmp(buf, "C64-TAPE-RAW", 12) != 0)
		sysfatal("not a c64 raw tape");
	tapever = buf[12];
	if(tapever > 1)
		sysfatal("unsupported tape version %d", tapever);
	tapelen = buf[16] | buf[17] << 8 | buf[18] << 16 | buf[19] << 24;
	tape = malloc(tapelen);
	readn(fd, tape, tapelen);
	close(fd);
}

static void
keyproc(void *)
{
	int fd, i, n, setnmi;
	u16int j;
	u64int k;
	static Rune keymap[64] = {
		Kbs, '\n', Kleft, KF|7, KF|1, KF|3, KF|5, Kup,
		'3', 'w', 'a', '4', 'z', 's', 'e', Kshift,
		'5', 'r', 'd', '6', 'c', 'f', 't', 'x',
		'7', 'y', 'g', '8', 'b', 'h', 'u', 'v',
		'9', 'i', 'j', '0', 'm', 'k', 'o', 'n',
		'\'', 'p', 'l', '-', '.', '\\', '@', ',',
		'[', '*', ';', Khome, Kalt, '=', ']', '/',
		'1', Kins, '\t', '2', ' ', Kctl, 'q', Kdel
	};
	static char buf[256];
	char *s;
	Rune r;

	fd = open("/dev/kbd", OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	for(;;){
		if(buf[0] != 0){
			n = strlen(buf)+1;
			memmove(buf, buf+n, sizeof(buf)-n);
		}
		if(buf[0] == 0){
			n = read(fd, buf, sizeof(buf)-1);
			if(n <= 0)
				sysfatal("read /dev/kbd: %r");
			buf[n-1] = 0;
			buf[n] = 0;
		}
		if(buf[0] == 'c'){
			if(utfrune(buf, Kend)){
				close(fd);
				threadexitsall(nil);
			}
			if(utfrune(buf, KF|12))
				trace ^= 1;
		}
		if(buf[0] != 'k' && buf[0] != 'K')
			continue;
		s = buf + 1;
		j = 0;
		k = 0;
		setnmi = 0;
		while(*s != 0){
			s += chartorune(&r, s);
			switch(r){
			case Kend: close(fd); threadexitsall(nil);
			case Kesc:
				if(paused)
					qunlock(&pauselock);
				else
					qlock(&pauselock);
				paused = !paused;
				break;
			case '`':
				setnmi = 1;
				break;
			case Kleft: if(joymode) j |= 1<<2+5*(joymode-1); break;
			case Kright: if(joymode) j |= 1<<3+5*(joymode-1); break;
			case Kup: if(joymode) j |= 1<<0+5*(joymode-1); break;
			case Kdown: if(joymode) j |= 1<<1+5*(joymode-1); break;
			case Kctl: if(joymode) j |= 1<<4+5*(joymode-1); break;
			}
			for(i = 0; i < 64; i++)
				if(keymap[i] == r)
					k |= 1ULL<<i;
		}
		if(setnmi)
			nmi |= IRQRESTORE;
		else
			nmi &= ~IRQRESTORE;
		keys = k;
		joys = j;
	}

}

static void
usage(void)
{
	fprint(2, "usage: %s [-Nap] [-c cart] [-t tape] [-d bindir] [-x scale] rom\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	memreset();

	ARGBEGIN {
	case 'c':
		loadcart(EARGF(usage()));
		break;
	case 't':
		loadtape(EARGF(usage()));
		break;
	case 'N':
		region = NTSC0;
		break;
	case 'p':
		region = PAL;
		break;
	case 'd':
		bindir = strdup(EARGF(usage()));
		break;
	case 'x':
		fixscale = strtol(EARGF(usage()), nil, 0);
		break;
	default:
		usage();
	} ARGEND;
	if(argc >= 2)
		usage();
	loadsys("kernal.bin", krom, 8192);
	loadsys("basic.bin", brom, 8192);
	loadsys("crom.bin", crom, 4096);
	
	vicreset();
	initemu(picw, pich, 4, XRGB32, 1, keyproc);
	red = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xFF0000FF);

	nmien = IRQRESTORE;
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

static void
menu(void)
{
	enum { JOY, TAPE };
	static char joystr[32] = "joy: none";
	static char tapestr[32] = "tape: play";
	static char *items[] = {
		[JOY] joystr,
		[TAPE] tapestr,
		nil
	};
	static Menu m = {
		items, nil, 0
	};

	extern Mousectl *mc;
	switch(menuhit(3, mc, &m, nil)){
	case JOY:
		joymode = (joymode + 1) % 3;
		if(joymode == 0)
			strcpy(joystr, "joy: none");
		else
			sprint(joystr, "joy: %d", joymode);
		break;
	case TAPE:
		tapeplay ^= 1;
		if(tapeplay == 0){
			strcpy(tapestr, "tape: play");
			progress(0, 0);
		}else
			strcpy(tapestr, "tape: stop");
		break;
	}
}

void
flush(void)
{
	extern Mousectl *mc;
	flushmouse(0);
	while(nbrecv(mc->c, &mc->Mouse) > 0)
		if((mc->buttons & 4) != 0)
			menu();
	flushscreen();
	flushaudio(nil);
}
