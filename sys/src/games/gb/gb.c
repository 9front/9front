#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <cursor.h>
#include <keyboard.h>
#include "dat.h"
#include "fns.h"

uchar *cart, *ram;
int mbc, rombanks, rambanks, clock, ppuclock, divclock, timerclock, syncclock, syncfreq, sleeps, checkclock, msgclock, timerfreq, timer, keys, savefd, savereq, loadreq, scale, paused;
Rectangle picr;
Image *bg, *tmp;
Mousectl *mc;
QLock pauselock;

void
message(char *fmt, ...)
{
	va_list va;
	char buf[512];
	
	va_start(va, fmt);
	vsnprint(buf, sizeof buf, fmt, va);
	string(screen, Pt(10, 10), display->black, ZP, display->defaultfont, buf);
	msgclock = CPUFREQ;
	va_end(va);
}

void
loadrom(char *file)
{
	int fd, i;
	vlong len;
	u8int ck;
	char buf[512];
	char title[17];
	Point p;
	char *s;
	extern int battery, ramen;
	
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
	battery = 0;
	switch(cart[0x147]){
	case 0x09:
		battery = 1;
	case 0x08:
		ramen = 1;
	case 0x00:
		mbc = 0;
		break;
	case 0x03:
		battery = 1;
	case 0x01: case 0x02:
		mbc = 1;
		break;
	case 0x06:
		battery = 1;
	case 0x05:
		mbc = 2;
		break;
	case 0x0F: case 0x10: case 0x13:
		battery = 1;
	case 0x11: case 0x12:
		mbc = 3;
		break;
	case 0x1B: case 0x1E:
		battery = 1;
	case 0x19: case 0x1A: case 0x1C: case 0x1D:
		mbc = 5;
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
	default:
		sysfatal("header field 0x148 (%.2x) invalid", cart[0x148]);
	}
	switch(cart[0x149]){
	case 0:
		if(mbc != 2){
			rambanks = 0;
			break;
		}
		/*fallthrough*/
	case 1: case 2:
		rambanks = 1;
		break;
	case 3:
		rambanks = 4;
		break;
	default:
		sysfatal("header field 0x149 (%.2x) invalid", cart[0x149]);
	}
	if(rambanks > 0){
		ram = mallocz(rambanks * 8192, 1);
		if(ram == nil)
			sysfatal("malloc: %r");
	}
	if(len < rombanks * 0x4000)
		sysfatal("cartridge image is too small, %.4x < %.4x", (int)len, rombanks * 0x4000);
	initdraw(nil, nil, title);
	originwindow(screen, Pt(0, 0), screen->r.min);
	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = (Rectangle){subpt(p, Pt(scale * 80, scale * 72)), addpt(p, Pt(scale * 80, scale * 72))};
	bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	if(screen->chan != XRGB32 || screen->chan != XBGR32)
		tmp = allocimage(display, Rect(0, 0, scale * 160, scale * 144), XRGB32, 0, 0);
	draw(screen, screen->r, bg, nil, ZP);
	
	if(ram && battery){
		strncpy(buf, file, sizeof buf - 4);
		s = buf + strlen(buf) - 3;
		if(s < buf || strcmp(s, ".gb") != 0)
			s += 3;
		strcpy(s, ".gbs");
		savefd = create(buf, ORDWR|OEXCL, 0666);
		if(savefd < 0)
			savefd = open(buf, ORDWR);
		if(savefd < 0)
			message("open: %r");
		else
			readn(savefd, ram, rambanks * 8192);
		atexit(flushram);
	}
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
		if(buf[0] == 'c'){
			if(utfrune(buf, Kdel))
				threadexitsall(nil);
			if(utfrune(buf, KF|5))
				savereq = 1;
			if(utfrune(buf, KF|6))
				loadreq = 1;
		}
		if(buf[0] != 'k' && buf[0] != 'K')
			continue;
		s = buf + 1;
		keys = 0;
		while(*s != 0){
			s += chartorune(&r, s);
			switch(r){
			case Kesc:
				if(paused)
					qunlock(&pauselock);
				else
					qlock(&pauselock);
				paused = !paused;
				break;
			case Kdel:
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
	int t;
	vlong old, new, diff;
	Mouse m;
	Point p;

	scale = 1;
	ARGBEGIN{
	case 'a':
		initaudio();
		break;
	case '2':
		scale = 2;
		break;
	case '3':
		scale = 3;
		break;
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
	mc = initmouse(nil, screen);
	if(mc == nil)
		sysfatal("init mouse: %r");
	proccreate(keyproc, nil, 8192);
	syncfreq = CPUFREQ / 50;
	old = nsec();
	for(;;){
		if(savereq){
			savestate("gb.save");
			savereq = 0;
		}
		if(loadreq){
			loadstate("gb.save");
			loadreq = 0;
		}
		if(paused){
			qlock(&pauselock);
			qunlock(&pauselock);
		}
		t = step();
		clock += t;
		ppuclock += t;
		divclock += t;
		timerclock += t;
		syncclock += t;
		checkclock += t;
		if(ppuclock >= 456){
			ppustep();
			ppuclock -= 456;
			while(nbrecv(mc->c, &m) > 0)
				;
			if(nbrecvul(mc->resizec) > 0){
				if(getwindow(display, Refnone) < 0)
					sysfatal("resize failed: %r");
				p = divpt(addpt(screen->r.min, screen->r.max), 2);
				picr = (Rectangle){subpt(p, Pt(scale * 80, scale * 72)), addpt(p, Pt(scale * 80, scale * 72))};
				bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
			}
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
		if(syncclock >= syncfreq){
			sleep(10);
			sleeps++;
			syncclock = 0;
		}
		if(checkclock >= CPUFREQ){
			new = nsec();
			diff = new - old - sleeps * 10 * MILLION;
			diff = BILLION - diff;
			if(diff <= 0)
				syncfreq = CPUFREQ;
			else
				syncfreq = ((vlong)CPUFREQ) * 10 * MILLION / diff;
			old = new;
			checkclock = 0;
			sleeps = 0;
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
