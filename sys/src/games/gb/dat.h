typedef struct Event Event;
typedef struct MBC3Timer MBC3Timer;

extern u16int curpc;

extern uchar *rom, *back, reg[256], oam[256];
extern MBC3Timer timer;
extern uchar vram[16384];
extern int nrom, nback, nbackbank;
extern u32int pal[64];
extern u8int dma;
extern u32int divclock;

extern Event *elist;
extern ulong clock;

extern u8int ppuy, ppustate, ppuw;

extern u8int apustatus;

extern u8int mode;
extern u8int mbc, feat;

enum {
	JOYP = 0x00,
	SB = 0x01,
	SC = 0x02,
	DIV = 0x04,
	TIMA = 0x05,
	TMA = 0x06,
	TAC = 0x07,
	IF = 0x0F,
	NR10 = 0x10,
	NR11 = 0x11,
	NR12 = 0x12,
	NR13 = 0x13,
	NR14 = 0x14,
	NR21 = 0x16,
	NR22 = 0x17,
	NR23 = 0x18,
	NR24 = 0x19,
	NR30 = 0x1A,
	NR31 = 0x1B,
	NR32 = 0x1C,
	NR33 = 0x1D,
	NR34 = 0x1E,
	NR41 = 0x20,
	NR42 = 0x21,
	NR43 = 0x22,
	NR44 = 0x23,
	NR50 = 0x24,
	NR51 = 0x25,
	NR52 = 0x26,
	WAVE = 0x30,
	LCDC = 0x40,
	STAT = 0x41,
	SCY = 0x42,
	SCX = 0x43,
	LY = 0x44,
	LYC = 0x45,
	DMA = 0x46,
	BGP = 0x47,
	OBP0 = 0x48,
	OBP1 = 0x49,
	WY = 0x4A,
	WX = 0x4B,
	KEY1 = 0x4D,
	VBK = 0x4F,
	RP = 0x56,
	HDMASH = 0x51,
	HDMASL = 0x52,
	HDMADH = 0x53,
	HDMADL = 0x54,
	HDMAC = 0x55,
	
	BCPS = 0x68,
	BCPD = 0x69,
	OCPS = 0x6A,
	OCPD = 0x6B,
	SVBK = 0x70,
	IE = 0xFF
};

enum {
	LCDEN = 0x80,
	WINMAP = 0x40,
	WINEN = 0x20,
	BGTILE = 0x10,
	BGMAP = 0x08,
	SPR16 = 0x04,
	SPREN = 0x02,
	BGEN = 0x01,
	BGPRI = 0x01,
	
	IRQLYC = 0x40,
	IRQM2 = 0x20,
	IRQM1 = 0x10,
	IRQM0 = 0x08,
		
	IRQVBL = 1,
	IRQLCDS = 2,
	IRQTIM = 4,
	IRQSER = 8,
	IRQJOY = 16,
};

enum {
	CGB = 1,
	COL = 2,
	TURBO = 4,
	FORCEDMG = 8,

	FEATRAM = 1,
	FEATBAT = 2,
	FEATTIM = 4,

	DMAREADY = 1,
	DMAHBLANK = 2,
	
	INIT = -1,
	SAVE = -2,
	RSTR = -3,
	READ = -4,
};

enum {
	TIMERSIZ = 18,
	PICW = 160,
	PICH = 144,
	FREQ = 1<<23
};

struct Event {
	int time;
	void (*f)(void *);
	Event *next;
	void *aux;
};
extern Event *elist;

struct MBC3Timer {
	vlong ns;
	u8int sec, min, hr, dl, dh;
};

typedef struct Var Var;

struct Var {
	void *a;
	int s, n;
};
#define VAR(a) {&a, sizeof(a), 1}
#define ARR(a) {a, sizeof(*a), nelem(a)}
enum { NEVENT = 9 };
extern int (*mapper)(int, int);
extern u32int moncols[4];
