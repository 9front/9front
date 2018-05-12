extern u8int reg[47], crom[4096], krom[8192], brom[8192], cram[1024], cart[16384];

extern u16int pc, curpc;
extern u8int rP;
extern int nrdy, irq, nmi, irqen, nmien;

extern u8int pla;

extern uchar *tape, tapever, tapeplay;
extern ulong tapelen;

extern u16int ppux, ppuy, picw, pich;
extern u16int joys;
extern int region;

enum {
	FLAGC = 1<<0,
	FLAGZ = 1<<1,
	FLAGI = 1<<2,
	FLAGD = 1<<3,
	FLAGB = 1<<4,
	FLAGV = 1<<6,
	FLAGN = 1<<7,
};

enum {
	IRQRASTER = 1<<0,
	IRQBGCOLL = 1<<1,
	IRQSPRCOLL = 1<<2,
	IRQLIGHT = 1<<3,
	IRQTIMERA = 1<<4,
	IRQTIMERB = 1<<5,
	IRQTOD = 1<<6,
	IRQSDR = 1<<7,
	IRQFLAG = 1<<8,
	IRQRESTORE = 1<<9,
};

enum{
	MSBX = 0x10,
	CTRL1 = 0x11,
	RASTER = 0x12,
	SPREN = 0x15,
	CTRL2 = 0x16,
	SPRYE = 0x17,
	MEMP = 0x18,
	IRQLATCH = 0x19,
	IRQEN = 0x1a,
	SPRDP = 0x1b,
	SPRMC = 0x1c,
	SPRXE = 0x1d,
	SPRSPR = 0x1e,
	SPRBG = 0x1f,
	EC = 0x20,
	BG0 = 0x21,
	BG1 = 0x22,
	BG2 = 0x23,
	BG3 = 0x24,
	SPRMC0 = 0x25,
	SPRMC1 = 0x26,
	SPRCOL = 0x27
};

enum {
	LORAM = 1,
	HIRAM = 2,
	CHAREN = 4,
	GAME = 8,
	EXROM = 16,
	
	ECM = 0x40,
	BMM = 0x20,
	DEN = 0x10,
	RSEL = 0x08,
	CSEL = 0x08,
	
	MCM = 0x10,
};

enum {
	HZ = 3579545,
	RATE = 44100,
	SAMPDIV = HZ / 3 / RATE,
};

enum {
	NTSC,
	NTSC0,
	PAL
};
