#define GDTTYPE(x) ((uvlong)(x)<<40)
enum {
	GDTR	= GDTTYPE(0x10), /* read-only */
	GDTRW	= GDTTYPE(0x12), /* read-write *
	GDTX	= GDTTYPE(0x18), /* execute-only */
	GDTRX	= GDTTYPE(0x1A), /* read-execute */
	
	GDTTSS	= GDTTYPE(0x09),
	
	GDTA	= 1ULL<<40,	/* accessed */
	GDTE	= 1ULL<<42,	/* expand down (data only) */
	GDTC	= GDTE,		/* conforming (code only) */
	GDTP	= 1ULL<<47,	/* present */
	GDT64	= 1ULL<<53,	/* 64-bit code segment */
	GDT32	= 1ULL<<54,	/* 32-bit segment */
	GDTG	= 1ULL<<55,	/* granularity */
};
#define GDTLIM(l) ((l) & 0xffff | (uvlong)((l) & 0xf0000)<<32)
#define GDTBASE(l) (((uvlong)(l) & 0xffffff)<<16 | (uvlong)((l) & 0xff000000)<<32)
#define GDTDPL(l) ((uvlong)(l)<<45)

enum {
	Cr0Pg	= 1<<31,
	
	Cr4Pse		= 1<<4,
	Cr4Pae		= 1<<5,
	Cr4Osxsave	= 1<<18,
	
	EferLme	= 1<<8,
};

extern char *x86reg[16];
extern char *x86segreg[8];

enum {
	CF	= 1<<0,
	PF	= 1<<2,
	AF	= 1<<4,
	ZF	= 1<<6,
	SF	= 1<<7,
	OF	= 1<<11,
};
