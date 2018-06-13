#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u16int *prg;
int nprg;
u8int *sram;
u32int sramctl, nsram, sram0, sram1;
int savefd = -1;

int dmaclock, vdpclock, z80clock, audioclock, ymclock, saveclock;

void
flushram(void)
{
	if(savefd >= 0)
		pwrite(savefd, sram, nsram, 0);
	saveclock = 0;
}

static void
loadbat(char *file)
{
	static char buf[512];
	
	strncpy(buf, file, sizeof buf - 5);
	strcat(buf, ".sav");
	savefd = create(buf, ORDWR | OEXCL, 0666);
	if(savefd < 0)
		savefd = open(buf, ORDWR);
	if(savefd < 0)
		print("open: %r\n");
	else
		readn(savefd, sram, nsram);
}

static void
loadrom(char *file)
{
	static uchar hdr[512], buf[4096];
	u32int v;
	u16int *p;
	int fd, rc, i;
	
	fd = open(file, OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	if(readn(fd, hdr, 512) < 512)
		sysfatal("read: %r");
	if(memcmp(hdr + 0x100, "SEGA MEGA DRIVE ", 16) != 0 && memcmp(hdr + 0x100, "SEGA GENESIS    ", 16) != 0)
		sysfatal("invalid rom");
	v = hdr[0x1a0] << 24 | hdr[0x1a1] << 16 | hdr[0x1a2] << 8 | hdr[0x1a3];
	if(v != 0)
		sysfatal("rom starts at nonzero address");
	v = hdr[0x1a4] << 24 | hdr[0x1a5] << 16 | hdr[0x1a6] << 8 | hdr[0x1a7];
	nprg = v = v+2 & ~1;
	if(nprg == 0)
		sysfatal("invalid rom");
	p = prg = malloc(v);
	if(prg == nil)
		sysfatal("malloc: %r");
	seek(fd, 0, 0);
	while(v != 0){
		rc = readn(fd, buf, sizeof buf);
		if(rc == 0)
			break;
		if(rc < 0)
			sysfatal("read: %r");
		if(rc > v)
			rc = v;
		for(i = 0; i < rc; i += 2)
			*p++ = buf[i] << 8 | buf[i+1];
		v -= rc;
	}
	close(fd);
	if(hdr[0x1b0] == 0x52 && hdr[0x1b1] == 0x41){
		sramctl = SRAM | hdr[0x1b2] >> 1 & ADDRMASK;
		if((hdr[0x1b2] & 0x40) != 0)
			sramctl |= BATTERY;
		sram0 = hdr[0x1b4] << 24 | hdr[0x1b5] << 16 | hdr[0x1b6] << 8 | hdr[0x1b7] & 0xfe;
		sram1 = hdr[0x1b8] << 24 | hdr[0x1b9] << 16 | hdr[0x1ba] << 8 | hdr[0x1bb] | 1;
		if(sram1 <= sram0){
			print("SRAM of size <= 0?\n");
			sramctl = 0;
		}else{
			nsram = sram1 - sram0;
			if((sramctl & ADDRMASK) != ADDRBOTH)
				nsram >>= 1;
			sram = malloc(nsram);
			if(sram == nil)
				sysfatal("malloc: %r");
			if((sramctl & BATTERY) != 0){
				loadbat(file);
				atexit(flushram);
			}
		}
	}
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
	int t;

	ARGBEGIN{
	case 'a':
		initaudio();
		break;
	case 'x':
		fixscale = strtol(EARGF(usage()), nil, 0);
		break;
	default:
		usage();
	} ARGEND;
	if(argc < 1)
		usage();
	loadrom(*argv);
	initemu(320, 224, 4, XRGB32, 1, nil);
	regkey("a", 'c', 1<<5);
	regkey("b", 'x', 1<<4);
	regkey("y", 'z', 1<<12);
	regkey("start", '\n', 1<<13);
	regkey("up", Kup, 0x101);
	regkey("down", Kdown, 0x202);
	regkey("left", Kleft, 1<<2);
	regkey("right", Kright, 1<<3);
	cpureset();
	vdpmode();
	ymreset();
	for(;;){
		if(paused != 0){
			qlock(&pauselock);
			qunlock(&pauselock);
		}
		if(dma != 1){
			t = step() * CPUDIV;
			if(dma != 0)
				dmastep();
		}else{
			t = CPUDIV;
			dmastep();
		}
		z80clock -= t;
		vdpclock -= t;
		audioclock += t;
		ymclock += t;

		while(vdpclock < 0){
			vdpstep();
			vdpclock += 8;
		}
		while(z80clock < 0)
			z80clock += z80step() * Z80DIV;
		while(audioclock >= SAMPDIV){
			audiosample();
			audioclock -= SAMPDIV;
		}
		while(ymclock >= YMDIV){
			ymstep();
			ymclock -= YMDIV;
		}
		if(saveclock > 0){
			saveclock -= t;
			if(saveclock <= 0){
				saveclock = 0;
				flushram();
			}
		}
	}
}

void
flush(void)
{
	flushmouse(1);
	flushscreen();
	flushaudio(audioout);
}
