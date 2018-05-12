extern int cpuhalt;

extern u32int curpc;
extern int irq;

extern int dmaact;
extern uchar vram[];
extern u16int pram[], oam[];
extern u16int reg[];
extern uchar *rom, *back;
extern int nrom, nback, backup;

extern int hblank, ppuy;

extern int clock;

typedef struct Event Event;
struct Event {
	int time;
	void (*f)(void *);
	Event *next;
	void *aux;
};
extern Event *elist;

enum {
	DISPCNT = 0x0/2,
	DISPSTAT = 0x4/2,
	BG0CNT = 0x8/2,
	BG0HOFS = 0x10/2,
	BG0VOFS = 0x12/2,

	BG2PA = 0x20/2,
	BG2PB = 0x22/2,
	BG2PC = 0x24/2,
	BG2PD = 0x26/2,
	BG2XL = 0x28/2,
	BG2XH = 0x2a/2,
	BG2YL = 0x2c/2,
	BG2YH = 0x2e/2,
	BG3XL = 0x38/2,
	BG3XH = 0x3a/2,
	BG3YL = 0x3c/2,
	BG3YH = 0x3e/2,
	
	WIN0H = 0x40/2,
	WIN1H = 0x42/2,
	WIN0V = 0x44/2,
	WIN1V = 0x46/2,
	WININ = 0x48/2,
	WINOUT = 0x4a/2,
	MOSAIC = 0x4c/2,
	BLDCNT = 0x50/2,
	BLDALPHA = 0x52/2,
	BLDY = 0x54/2,
	
	SOUNDCNTL = 0x80/2,
	SOUNDCNTH = 0x82/2,
	SOUNDBIAS = 0x88/2,
	
	FIFOAH = 0xa2/2,
	FIFOBH = 0xa6/2,
	
	DMA0CNTH = 0xba/2,
	DMA1CNTH = 0xc6/2,
	DMA2CNTH = 0xd2/2,
	DMA3CNTH = 0xde/2,
	
	TM0CNTH = 0x102/2,
	KEYCNT = 0x132/2,

	IE = 0x200/2,
	IF = 0x202/2,
	WAITCNT = 0x204/2,
	IME = 0x208/2,
	
};

enum {
	/* DISPCNT */
	FRAME = 1<<4,
	HBLFREE = 1<<5,
	OBJNOMAT = 1<<6,
	FBLANK = 1<<7,

	/* DISPSTAT */
	IRQVBLEN = 1<<3,
	IRQHBLEN = 1<<4,
	IRQVCTREN = 1<<5,

	/* BGnCNT */
	BGMOSAIC = 1<<6,
	BG8 = 1<<7,
	DISPWRAP = 1<<13,
	
	/* TIMERnCNTH */
	PRESC = 3,
	COUNTUP = 1<<2,
	TIMERIRQ = 1<<6,
	TIMERON = 1<<7,
	
	/* DMAnCNTH */
	DMADCNT = 5,
	DMASCNT = 7,
	DMAREP = 1<<9,
	DMAWIDE = 1<<10,
	DMAWHEN = 12,
	DMAIRQ = 1<<14,
	DMAEN = 1<<15,

	DMAINC = 0,
	DMADEC = 1,
	DMAFIX = 2,
	DMAINCREL = 3,

	DMANOW = 0,
	DMAVBL = 1,
	DMAHBL = 2,
	DMASPEC = 3,
	DMASOUND = 4,
	DMAVIDEO = 5,
	
	IRQVBL = 1<<0,
	IRQHBL = 1<<1,
	IRQVCTR = 1<<2,
	IRQTIM0 = 1<<3,
	IRQDMA0 = 1<<8,
	IRQKEY = 1<<12,
	
	NOBACK = 0,
	SRAM = 1,
	EEPROM = 2,
	FLASH = 3,
	
	KB = 1024,
	BACKTYPELEN = 64,
	HZ = 16777216,
};

typedef struct Var Var;

struct Var {
	void *a;
	int s, n;
};
#define VAR(a) {&a, sizeof(a), 1}
#define ARR(a) {a, sizeof(*a), nelem(a)}
enum { NEVENT = 6 };
