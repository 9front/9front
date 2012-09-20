enum {
	Cmdn	= 1,
	Cmdf,			/* cfa only */
	Cmdp,			/* packet */
	Cmd5sc,			/* 512-byte sector size, set sector count */
};

typedef struct Dev Dev;
struct Dev {
	Sfis;
	int	fd;
	uint	secsize;
	uvlong	nsect;
	uvlong	wwn;
};

enum {
	Cmdsz	= 18,
	Replysz	= 18,
};

typedef struct Rcmd Rcmd;
struct Rcmd {
	uchar	sdcmd;		/* sd command; 0xff means ata passthrough */
	uchar	ataproto;	/* ata protocol.  non-data, pio, reset, dd, etc. */
	uchar	fis[Fissize];
};

typedef struct Req Req;
struct Req {
	int	rfd;
	int	wfd;
	uchar	fmtrw;
	uvlong	lba;
	uvlong	count;		/* bytes; allow long sectors to work */
	uint	nsect;
	char	haverfis;
	uint	fisbits;		/* bitmask of manually set fields */
	Rcmd	cmd;
	Rcmd	reply;
	uchar	*data;
	uchar	raw;
};

void	sigfmt(Req*);
void	idfmt(Req*);
void	iofmt(Req*);
void	sdfmt(Req*);
void	smfmt(Req*);
void	slfmt(Req*);
void	glfmt(Req*);

typedef struct Btab Btab;
struct Btab {
	int	bit;
	char	*name;
};
char	*sebtab(char*, char*, Btab*, int, uint);

typedef struct Txtab Txtab;
typedef struct Fetab Fetab;

struct Txtab {
	int	val;
	char	*name;
	Fetab	*fe;
};

struct Fetab {
	int	reg;
	Txtab	*tab;
	int	ntab;
};

/* sct “registers” */
enum {
	Sbase	= 1<<5,
	Sbyte	= 0<<6,
	Sw	= 1<<6,
	Sdw	= 2<<6,
	Sqw	= 3<<6,
	Ssz	= 3<<6,

	Saction	= 0 | Sbase | Sw,
	Sfn	= 1 | Sbase | Sw,
	Slba	= 2 | Sbase | Sqw,
	Scnt	= 6 | Sbase | Sqw,
	Spat	= 10 | Sbase | Sdw,
	Ssc	= 2 | Sbase | Sw,
	Stimer	= 3 | Sbase | Sw,
	Sfe	= 2 | Sbase | Sw,
	Sstate	= 3 | Sbase | Sw,
	Soptf	= 4 | Sbase | Sw,
	Stabid	= 2 | Sbase | Sw,

	Pbase	= 1<<6,
};

void	pw(uchar*, ushort);
void	pdw(uchar*, uint);
void	pqw(uchar*, uvlong);
ushort	w(uchar*);
uint	dw(uchar*);
uvlong	qw(uchar*);

/*
 * botch.  integrate with fis.h?
 */
enum {
	Psct	= 1<<6 + 8,
};

typedef struct Atatab Atatab;
struct Atatab {
	ushort	cc;
	uchar	flags;
	uchar	pktflags;
	ushort	protocol;
	Fetab	*tab;
	void	(*fmt)(Req*);
	char	*name;
};

int	eprint(char *, ...);
int	opendev(char*, Dev*);
int	probe(void);

extern	int	squelch;

#pragma	varargck	argpos	eprint	1
#pragma	varargck	type	"π"	char**
