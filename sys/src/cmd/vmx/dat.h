typedef struct PCIDev PCIDev;
typedef struct PCICap PCICap;
typedef struct PCIBar PCIBar;
typedef struct Region Region;

extern int halt, irqactive;

enum {
	BY2PG = 4096
};

#define RPC "pc"
#define RSP "sp"
#define RAX "ax"
#define RBX "bx"
#define RCX "cx"
#define RDX "dx"

enum {
	MMIORD = 0,
	MMIOWRP = 1,
	MMIOWR = 2,
};

struct Region {
	uintptr start, end;
	enum { REGNO, REGMEM, REGFB } type;
	char *segname;
	uvlong segoff;
	void *v, *ve;
	Region *next;
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
