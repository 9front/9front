extern u32int curpc, irq;
extern int trace, debug;

extern ushort ram[128*1024];

extern int daddr;
extern ushort dstat;
extern uchar invert;

extern int mousex, mousey, mousebut;

extern int vblctr, uartrxctr;
extern int baud;

enum {
	INTKEY = 1,
	INTMOUSE = 2,
	INTUART = 4,
	INTVBL = 8,
};

enum {
	SX = 800,
	SY = 1024,
	FREQ = 8000*1000,
	VBLDIV = FREQ / 60,
};
