extern u8int rP, dma, nmi, irq, emu, wai;
extern u16int rA, rX, rY, rS, rD, pc;
extern u32int rPB, rDB, curpc, hdma;

extern uchar *prg, *sram;
extern int nprg, nsram, hirom;
extern u32int keylatch, lastkeys;
extern u8int reg[32768], mem[131072], spcmem[65536], vram[65536], oam[544];
extern u16int cgram[256], vramlatch;
extern u8int mdr, mdr1, mdr2;

extern int ppux, ppuy, rx;
extern u16int vtime, htime, subcolor, oamaddr;
extern u16int hofs[5], vofs[5];
extern s16int m7[6];

extern u8int spcmem[65536], spctimer[4], dsp[256];
extern u8int sA, sX, sY, sP, sS;
extern u16int spc;
extern u8int dspstate;
extern u16int dspcounter, noise;

extern int ppuclock, spcclock, dspclock, stimerclock, cpupause;
extern int battery, saveclock, mouse;

enum {
	FLAGC = 1<<0,
	FLAGZ = 1<<1,
	FLAGI = 1<<2,
	FLAGD = 1<<3,
	FLAGX = 1<<4,
	FLAGM = 1<<5,
	FLAGV = 1<<6,
	FLAGN = 1<<7,
};

enum {
	FREQ = 21477272,
	SPCDIV = 21,
	SAVEFREQ = FREQ / 4,
	
	XLEFT = 22,
	XRIGHT = 22 + 255,
};

enum {
	INIDISP = 0x2100,
	FORBLANK = 0x80,
	OBSEL = 0x2101,
	OAMADDH = 0x2103,
	BGMODE = 0x2105,
	MOSAIC = 0x2106,
	M7SEL = 0x211a,
	WIN1 = 2,
	INVW1 = 1,
	WIN2 = 8,
	INVW2 = 4,
	TM = 0x212c,
	TS = 0x212d,
	TMW = 0x212e,
	TSW = 0x212f,
	CGWSEL = 0x2130,
	CGADSUB = 0x2131,
	DIRCOL = 1,
	SETINI = 0x2133,
	EXTBG = 1<<6,
	OVERSCAN = 1<<2,
	AUTOJOY = 1,
	NMITIMEN = 0x4200,
	MEMSEL = 0x420d,
	RDNMI = 0x4210,
	VBLANK = 1<<7,
	VCNTIRQ = 1<<5,
	HCNTIRQ = 1<<4,
};

enum {
	IRQPPU = 1<<7,
};

