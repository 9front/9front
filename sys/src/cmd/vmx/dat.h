typedef struct PCIDev PCIDev;
typedef struct PCICap PCICap;
typedef struct PCIBar PCIBar;
typedef struct Region Region;

extern int irqactive;

enum {
	VMRUNNING,
	VMHALT,
	VMDEAD,
};
extern int state;
extern int debug;

enum {
	BY2PG = 4096
};

#define RPC "pc"
#define RSP "sp"
#define RAX "ax"
#define RBX "bx"
#define RCX "cx"
#define RDX "dx"
#define RBP "bp"
#define RSI "si"
#define RDI "di"
#define R8 "r8"
#define R9 "r9"
#define R10 "r10"
#define R11 "r11"
#define R12 "r12"
#define R13 "r13"
#define R14 "r14"
#define R15 "r15"
#define RFLAGS "flags"

enum {
	MMIORD = 0,
	MMIOWRP = 1,
	MMIOWR = 2,
};

struct Region {
	uintptr start, end;
	enum {
		REGALLOC = 1, /* allocate memory for region */
		REGR = 2, /* can read */
		REGW = 4, /* can write */
		REGX = 8, /* can execute */
		
		REGRWX = REGR|REGW|REGX,
		REGRX = REGR|REGX,
		
		/* E820 types, 0 == omitted from memory map */
		REGFREE = 1<<8, /* report to OS as free */
		REGRES = 2<<8, /* report to OS as reserved */
	} type;
	char *segname;
	uvlong segoff;
	void *v, *ve;
	Region *next;
	int (*mmio)(uintptr, void *, int, int);
};

extern Region *mmap;

#define BDF(b,d,f) ((b)<<16&0xff0000|(d)<<11&0xf800|(f)<<8&0x700)

struct PCIBar {
	PCIDev *d;
	u8int type;
	u32int addr, length;
	PCIBar *busnext, *busprev;
	u32int (*io)(int, u16int, u32int, int, void *);
	void *aux;
};

enum {
	/* type */
	BARIO = 1,
	BARMEM32 = 0,
	BARMEM64 = 4,
	BARPREF = 8,
};

struct PCIDev {
	u32int bdf, viddid, clrev, subid;
	u16int ctrl;
	u8int irqno, irqactive;
	PCIBar bar[6];
	PCIDev *next;
	PCICap *cap;
	u8int capalloc;
};

struct PCICap {
	PCIDev *dev;
	u8int length;
	u8int addr;
	u32int (*read)(PCICap *, u8int);
	void (*write)(PCICap *, u8int, u32int, u32int);
	PCICap *next;
};

enum {
	/* irqline argument */
	IRQLTOGGLE = -1,
	IRQLLOHI = -2,
	
	/* postexc */
	NOERRC = -1,
};

typedef struct VgaMode VgaMode;
struct VgaMode {
	u16int no;
	int w, h, hbytes, sz;
	u32int chan;
	VgaMode *next;
};

extern uchar cmos[0x30];

extern void (*kconfig)(void);

/* arguments for x86access */
enum {
	SEGCS,
	SEGDS,
	SEGES,
	SEGFS,
	SEGGS,
	SEGSS,
	SEGMAX,
};

enum {
	ACCR,
	ACCW,
	ACCX,
	ACCSAFE = 0x100, /* don't post exceptions on fault */
};

/* used to speed up consecutive x86access calls */
typedef struct TLB TLB;
struct TLB {
	int asz, seg, acc;
	uintptr start, end;
	uintptr pabase;
	Region *reg;
	uchar *base;
};
