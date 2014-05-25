typedef signed char s8int;
typedef signed short s16int;
typedef signed long s32int;

extern u32int curpc, irq;

extern u8int reg[32];
extern u8int dma;

extern u8int z80bus;

extern u16int ram[32768];
extern u16int *prg;
extern int nprg;

extern int keys, scale;

extern u16int vram[32768], vsram[40];
extern u32int cramc[64];
extern u16int vdpstat;
extern int vdpx, vdpy;

enum {
	MODE1   = 0x00,
	MODE2   = 0x01,
	PANT    = 0x02,
	PWNT    = 0x03,
	PBNT    = 0x04,
	SPRTAB  = 0x05,
	BGCOL   = 0x07,
	HORCTR  = 0x0a,
	MODE3   = 0x0b,
	MODE4   = 0x0c,
	HORSCR  = 0x0d,
	AUTOINC = 0x0f,
	PLSIZ   = 0x10,
	DMACL   = 0x13,
	DMACH   = 0x14,
	DMASRC0 = 0x15,
	DMASRC1 = 0x16,
	DMASRC2 = 0x17,

	IE0 = 0x20,
	IE1 = 0x10,
	DMAEN = 0x10,
	
	WIDE = 0x01,
	
	STATDMA = 0x02,
	STATHBL = 0x04,
	STATVBL = 0x08,
	STATFR  = 0x10,
	STATCOLL= 0x20,
	STATOVR = 0x40,
	STATINT = 0x80,
};

enum {
	BUSREQ = 1,
	BUSACK = 2,
	RESET = 4,
	
	INTVBL = 1,
	INTHOR = 2,
};
