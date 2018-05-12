extern u16int pc, curpc;
extern u8int rA, rX, rY, rS, rP;
extern u8int irq, nmi;
extern uchar mem[32768], ppuram[16384], oam[256];
extern u16int pput, ppuv;
extern u8int ppusx, vrambuf;
extern int mirr, ppux, ppuy, odd, vramlatch, keylatch, keylatch2;

extern int map, mmc3hack, oflag;
extern uchar *prg, *chr;
extern int nprg, nchr, map, chrram;

extern u8int apuseq, apuctr[13];
extern u16int dmcaddr, dmccnt;

extern int clock, ppuclock, apuclock, dmcclock, dmcfreq, saveclock;

extern void (*mapper[])(int, u8int);

enum {
	FLAGC = 1<<0,
	FLAGZ = 1<<1,
	FLAGI = 1<<2,
	FLAGD = 1<<3,
	FLAGB = 1<<4,
	FLAGV = 1<<6,
	FLAGN = 1<<7
};

enum {
	PPUCTRL = 0x2000,
	PPUMASK = 0x2001,
	PPUSTATUS = 0x2002,
	PPUSCROLL = 0x2005,
	DMCCTRL = 0x4010,
	DMCBUF = 0x4011,
	DMCADDR = 0x4012,
	DMCLEN = 0x4013,
	APUSTATUS = 0x4015,
	APUFRAME = 0x4017,

	PPUNMI = 1<<7,
	BIGSPRITE = 1<<5,
	BGTABLE = 1<<4,
	SPRTABLE = 1<<3,
	VRAMINC = 1<<2,

	GRAYSCALE = 1<<0,
	BG8DISP = 1<<1,
	SPRITE8DISP = 1<<2,
	BGDISP = 1<<3,
	SPRITEDISP = 1<<4,

	PPUVBLANK = 1<<7,
	SPRITE0HIT = 1<<6,
};

enum {
	HPRG = 4,
	HCHR = 5,
	HRAM = 8,
	HROMH = 9,
	
	FLMIRROR = 1<<0,
	FLBAT = 1<<1,
	FLTRAINER = 1<<2,
	FLFOUR = 1<<3,
	FLMAPPERL = 4,
	FLMAPPERH = 12,
	FLNES20M = 3<<10,
	FLNES20V = 2<<10,
	FLPC10 = 1<<9,
	FLVS = 1<<8,
	
	PRGSZ = 1<<14,
	CHRSZ = 1<<13,
};

enum {
	FREQ = 21477272,
	APUDIV = 89490,
	RATE = 44100,
	SAMPDIV = FREQ / RATE,
	SAVEFREQ = FREQ/5,
};

enum {
	MHORZ,
	MVERT,
	MSINGA,
	MSINGB,
	MFOUR
};

enum {
	INIT = -1,
	SAVE = -2,
	RSTR = -3,
	SCAN = -4,
};

enum {
	IRQFRAME = 1,
	IRQDMC = 2,
	IRQMMC = 4,
};
