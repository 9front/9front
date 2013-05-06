enum {
	ACata,
	ACconfig,
	ACmask,
	ACres,
};

enum {
	AQCread,
	AQCtest,
	AQCprefix,
	AQCset,
	AQCfset,
};

enum {
	AEunk,
	AEcmd,				/* bad command */
	AEarg,				/* bad argument */
	AEoff,				/* device offline */
	AEcfg,				/* config string already set */
	AEver,				/* unsupported version */
	AEres,				/* target reserved */
};

enum {
	/* mask commands */
	Mread		= 0,
	Medit,

	/* mask directives */
	MDnop		= 0,
	MDadd,
	MDdel,

	/* mask errors */
	MEunk		= 1,
	MEbad,
	MEfull,

	/* reserve / release */
	Rrread		= 0,
	Rrset,
	Rrforce,
};

enum {
	Aoetype		= 0x88a2,
	Aoesectsz 	= 512,
	Aoemaxcfg	= 1024,

	Aoehsz		= 24,
	Aoeatasz	= 12,
	Aoecfgsz		= 8,
	Aoerrsz		= 2,
	Aoemsz		= 4,
	Aoemdsz	= 8,

	Aoever		= 1,

	AFerr		= 1<<2,
	AFrsp		= 1<<3,

	AAFwrite	= 1,
	AAFext		= 1<<6,
};

typedef	struct	Aoehdr	Aoehdr;
typedef	struct	Aoeata	Aoeata;
typedef	struct	Aoecfg	Aoecfg;
typedef	struct	Aoemd	Aoemd;
typedef	struct	Aoem	Aoem;
typedef	struct	Aoerr	Aoerr;

struct Aoehdr {
	uchar	dst[Eaddrlen];
	uchar	src[Eaddrlen];
	uchar	type[2];
	uchar	verflag;
	uchar	error;
	uchar	major[2];
	uchar	minor;
	uchar	cmd;
	uchar	tag[4];
};

struct Aoeata {
	uchar	aflag;
	uchar	errfeat;
	uchar	scnt;
	uchar	cmdstat;
	uchar	lba[6];
	uchar	res[2];
};

struct Aoecfg {
	uchar	bufcnt[2];
	uchar	fwver[2];
	uchar	scnt;
	uchar	verccmd;
	uchar	cslen[2];
};

struct Aoemd {
	uchar	dres;
	uchar	dcmd;
	uchar	ea[Eaddrlen];
};

struct Aoem {
	uchar	mres;
	uchar	mcmd;
	uchar	merr;
	uchar	mcnt;
};

typedef struct Aoerr {
	uchar	rcmd;
	uchar	nea;
	uchar	ea0[];
};

extern char Echange[];
extern char Enotup[];
