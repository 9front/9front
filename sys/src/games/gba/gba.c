#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

int cpuhalt;
Image *tmp;
int backup;
int savefd, saveframes;
int clock;

char *biosfile = "/sys/games/lib/gbabios.bin";

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
	p = strrchr(buf, '.');
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
	if(sz <= 0 || sz > 32*1024*1024)
		sysfatal("invalid file size");
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
flush(void)
{
	int x;

	flushmouse(1);
	flushscreen();
	flushaudio(audioout);

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
	fprint(2, "usage: %s [-a] [-s savetype] [-b biosfile] [-x scale] rom\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	char *s;
	int t;

	ARGBEGIN {
	case 'a':
		audioinit();
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
	case 'x':
		fixscale = strtol(EARGF(usage()), nil, 0);
		break;
	default:
		usage();
	} ARGEND;
	if(argc < 1)
		usage();

	loadbios();
	loadrom(argv[0]);
	initemu(240, 160, 2, CHAN4(CIgnore, 1, CBlue, 5, CGreen, 5, CRed, 5), 1, nil);
	regkey("b", 'z', 1<<1);
	regkey("a", 'x', 1<<0);
	regkey("l1", 'a', 1<<9);
	regkey("r1", 's', 1<<8);
	regkey("control", Kshift, 1<<2);
	regkey("start", '\n', 1<<3);
	regkey("up", Kup, 1<<6);
	regkey("down", Kdown, 1<<7);
	regkey("left", Kleft, 1<<5);
	regkey("right", Kright, 1<<4);
	eventinit();
	memreset();
	reset();
	for(;;){
		if(savereq){
			savestate("gba.save");
			savereq = 0;
		}
		if(loadreq){
			loadstate("gba.save");
			loadreq = 0;
		}
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
		clock += t;
		if((elist->time -= t) <= 0)
			popevent();
	}
}
