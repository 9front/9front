#include <u.h>
#include <libc.h>
#include <thread.h>
#include <keyboard.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

uchar *cart;
int mbc, rombanks, clock, ppuclock, divclock, timerclock, syncclock, timerfreq, timer, keys;
Rectangle picr;
Image *bg;

void
loadrom(char *file)
{
	int fd, i;
	vlong len;
	u8int ck;
	char title[17];
	Point p;
	
	fd = open(file, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	len = seek(fd, 0, 2);
	if(len < 0)
		sysfatal("seek: %r");
	if(len == 0 || len > 16*1048576)
		sysfatal("are you sure this is a ROM?");
	cart = malloc(len);
	if(cart == nil)
		sysfatal("malloc: %r");
	seek(fd, 0, 0);
	if(readn(fd, cart, len) < len)
		sysfatal("read: %r");
	close(fd);

	ck = 0;
	for(i = 0x134; i <= 0x14C; i++)
		ck -= cart[i] + 1;
	if(ck != cart[0x14D])
		sysfatal("checksum mismatch: %.2x != %.2x", ck, cart[0x14D]);
	memcpy(mem, cart, 32768);
	memset(title, 0, sizeof(title));
	memcpy(title, cart+0x134, 16);
	switch(cart[0x147]){
	case 0x00:
		mbc = 0;
		break;
	case 0x01:
		mbc = 1;
		break;
	case 0x13:
		mbc = 3;
		break;
	default:
		sysfatal("%s: unknown cartridge type %.2x", file, cart[0x147]);
	}
	switch(cart[0x148]){
	case 0: case 1: case 2:
	case 3: case 4: case 5:
	case 6: case 7:
		rombanks = 2 << (uint)cart[0x148];
		break;
	case 52:
		rombanks = 72;
		break;
	case 53:
		rombanks = 80;
		break;
	case 54:
		rombanks = 96;
		break;
	}
	if(len < rombanks * 0x4000)
		sysfatal("cartridge image is too small, %.4x < %.4x", (int)len, rombanks * 0x4000);

	initdraw(nil, nil, title);
	open("/dev/mouse", OREAD);
	originwindow(screen, Pt(0, 0), screen->r.min);
	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = (Rectangle){subpt(p, Pt(80, 72)), addpt(p, Pt(80, 72))};
	bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	draw(screen, screen->r, bg, nil, ZP);
}

void
keyproc(void *)
{
	int fd;
	char buf[256], *s;
	Rune r;
	
	fd = open("/dev/kbd", OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	for(;;){
		if(read(fd, buf, 256) <= 0)
			sysfatal("read /dev/kbd: %r");
		if(buf[0] == 'c' && strchr(buf, 'q'))
			threadexitsall(nil);
		if(buf[0] != 'k' && buf[0] != 'K')
			continue;
		s = buf + 1;
		keys = 0;
		while(*s != 0){
			s += chartorune(&r, s);
			switch(r){
			case 'q':
				threadexitsall(nil);
			case Kdown:
				keys |= 1<<3;
				break;
			case Kup:
				keys |= 1<<2;
				break;
			case Kleft:
				keys |= 1<<1;
				break;
			case Kright:
				keys |= 1<<0;
				break;
			case 'x':
				keys |= 1<<4;
				break;
			case 'z':
				keys |= 1<<5;
				break;
			case Kshift:
				keys |= 1<<6;
				break;
			case 10:
				keys |= 1<<7;
				break;
			}
		}
	}
}

void
threadmain(int argc, char** argv)
{
	int t, count;
	vlong old, new, diff;

	ARGBEGIN{
	default:
		sysfatal("unknown flag -%c", ARGC());
	}ARGEND;
	if(argc == 0)
		sysfatal("argument missing");
	pc = 0x100;
	sp = 0xFFFE;
	R[rA] = 0x01;
	R[rC] = 0x13;
	R[rE] = 0xD8;
	R[rL] = 0x4D;
	R[rH] = 0x01;
	Fl = 0xB0;
	loadrom(argv[0]);
	proccreate(keyproc, nil, 8192);
	count = 0;
	old = nsec();
	for(;;){
		if(pc == 0x231 && count++)
			break;
		t = step();
		clock += t;
		ppuclock += t;
		divclock += t;
		timerclock += t;
		syncclock += t;
		if(ppuclock >= 456){
			ppustep();
			ppuclock -= 456;
		}
		if(divclock >= 256){
			mem[DIV]++;
			divclock = 0;
		}
		if(timer && timerclock >= timerfreq){
			mem[TIMA]++;
			if(mem[TIMA] == 0){
				mem[TIMA] = mem[TMA];
				interrupt(INTTIMER);
			}
			timerclock = 0;
		}
		if(syncclock >= CPUFREQ / 100){
			new = nsec();
			diff = new - old;
			diff = 10000000 - diff;
			diff /= 1000000;
			if(diff > 0)
				sleep(diff);
			old = new;
			syncclock = 0;
		}
	}
}
