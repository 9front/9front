extern u16int pc, curpc;
extern u8int rP;
extern int nrdy;
extern int p0difc;
extern int bwmod;

extern int ppux, ppuy;
extern u8int p0x, p1x, m0x, m1x, blx;
extern u16int coll;

extern u8int *rom, *rop;
extern u16int bnk[];
extern int mask;
extern u8int reg[64];

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
	VSYNC,
	VBLANK,
	WSYNC,
	RSYNC,
	NUSIZ0,
	NUSIZ1,
	COLUP0,
	COLUP1,
	COLUPF,
	COLUBK,
	CTRLPF,
	REFP0,
	REFP1,
	PF0,
	PF1,
	PF2,
	RESP0,
	RESP1,
	RESM0,
	RESM1,
	RESBL,
	AUDC0,
	AUDC1,
	AUDF0,
	AUDF1,
	AUDV0,
	AUDV1,
	GRP0,
	GRP1,
	ENAM0,
	ENAM1,
	ENABL,
	HMP0,
	HMP1,
	HMM0,
	HMM1,
	HMBL,
	VDELP0,
	VDELP1,
	VDELBL,
	RESMP0,
	RESMP1,
	HMOVE,
	HMCLR,
	CXCLR,
};

enum {
	PICW = 320,
	PICH = 222,
	HZ = 3579545,
	RATE = 44100,
	SAMPDIV = HZ / 3 / RATE,
};
