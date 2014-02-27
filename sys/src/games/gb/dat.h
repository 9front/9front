extern u16int pc, curpc, sp;
extern u8int R[8], Fl;
extern int halt, IME, keys;
extern int clock, ppuclock, divclock, timerclock, timerfreq, timer;
extern int rombank, rambank, ramen, battery, ramrom;

extern uchar mem[], *ram;

extern uchar *cart;
extern int mbc, rombanks, rambanks;

extern int scale;

enum {
	rB,
	rC,
	rD,
	rE,
	rH,
	rL,
	rHL,
	rA
};

enum {
	FLAGC = 0x10,
	FLAGH = 0x20,
	FLAGN = 0x40,
	FLAGZ = 0x80,
};

enum {
	/* interrupt types */
	INTVBLANK  = 0,
	INTLCDC    = 1,
	INTTIMER   = 2,
	INTSIO     = 3,
	INTIRQ     = 4,

	/* I/O registers */
	DIV   = 0xFF04,
	TIMA  = 0xFF05,
	TMA   = 0xFF06,
	TAC   = 0xFF07,
	LY    = 0xFF44,
	LCDC  = 0xFF40,
	STAT  = 0xFF41,
	SCY   = 0xFF42,
	SCX   = 0xFF43,
	LYC   = 0xFF45,
	BGP   = 0xFF47,
	OBP0  = 0xFF48,
	OBP1  = 0xFF49,
	WY    = 0xFF4A,
	WX    = 0xFF4B,

	/* LCDC */
	BGDISP      = 1,
	SPRITEDISP  = 2,
	SPRITE16    = 4,
	BGTILEMAP   = 8,
	BGTILEDATA  = 16,
	WINDOWDISP  = 32,
	WINDOWTILEMAP  = 64,
	LCDOP       = 128,

	/* LCD STAT */
	MODEHBLANK  = 0,
	MODEVBLANK  = 1,
	MODEOAM     = 2,
	MODELCD     = 3,
	
	/* others */
	IF	= 0xFF0F,
	IE	= 0xFFFF,
	CPUFREQ = 4194304,
	
	MILLION = 1000000,
	BILLION = 1000000000,
	SAMPLE = 44100,
};
