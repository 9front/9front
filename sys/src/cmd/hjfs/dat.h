enum {
	/* affects on-disk structure */
	BLOCK = 4096,
	RBLOCK = BLOCK - 1,
	SUPERMAGIC = 0x6E0DE51C,
	SUPERBLK = 0,
	
	NAMELEN = 256,
	NDIRECT = 15,
	NINDIRECT = 4,
	
	ROOTQID = 1,
	DUMPROOTQID = 2,
	
	/* affects just run-time behaviour */
	SYNCINTERVAL = 10000,
	FREELISTLEN = 256,
	BUFHASHBITS = 8,
	BUFHASH = (1<<BUFHASHBITS)-1,
	NWORKERS = 5,
	EXCLDUR = 300,

	NOUID = (short)0x8000,
	USERLEN = 64,
};

typedef struct Fs Fs;
typedef struct Buf Buf;
typedef struct Dev Dev;
typedef struct BufReq BufReq;
typedef struct ThrData ThrData;
typedef struct Superblock Superblock;
typedef struct Dentry Dentry;
typedef struct Chan Chan;
typedef struct FLoc FLoc;
typedef struct Loc Loc;
typedef struct User User;

#pragma incomplete struct User
#pragma varargck type "T" int
#pragma varargck type "T" uint
enum {
	TRAW,
	TSUPERBLOCK,
	TDENTRY,
	TINDIR,
	TREF,
	TDONTCARE = -1,
};

struct Superblock {
	ulong magic;
	uvlong size;
	uvlong fstart;
	uvlong fend;
	uvlong root;
	uvlong qidpath;
};

enum {
	DALLOC = 1<<15,
	DGONE = 1<<14,
};

struct Dentry {
	char name[NAMELEN];
	short uid;
	short muid;
	short gid;
	ushort mode;
	Qid;
	uvlong size; /* bytes for files and blocks for dirs */
	uvlong db[NDIRECT];
	uvlong ib[NINDIRECT];
	vlong atime;
	vlong mtime;
};

enum {
	DENTRYSIZ = NAMELEN + 4 * sizeof(ushort) + 13 + (3 + NDIRECT + NINDIRECT) * sizeof(uvlong),
	DEPERBLK = RBLOCK / DENTRYSIZ,
	/* Given any opportunity to make a breaking change to hjfs,
	 * make this 12 an 8. Indirect offsets to blocks used to
	 * hold an incrementing  4 byte generation number. That
	 * design has changed.
	 */
	OFFPERBLK = RBLOCK / 12,
	REFSIZ = 3,
	REFPERBLK = RBLOCK / REFSIZ,
	REFSENTINEL = (1 << 8*REFSIZ) - 1,
};

struct BufReq {
	Dev *d;
	uvlong off;
	int nodata;
	Channel *resp;
	BufReq *next;
};

enum {
	BWRITE = 1, /* used only for the worker */
	BWRIM = 2, /* write immediately after putbuf */
	BDELWRI = 4, /* write delayed */
};

struct Buf {
	uchar op, type;
	union {
		uchar data[RBLOCK];
		Superblock sb;
		Dentry de[DEPERBLK];
		uvlong offs[OFFPERBLK];
		ulong refs[REFPERBLK];
	};

	/* do not use anything below (for the bufproc only) */
	uchar busy;
	char *error;
	Buf *dnext, *dprev;
	Buf *fnext, *fprev;
	BufReq;
	BufReq *last;
	ulong callerpc; /* debugging */
	
	Buf *wnext, *wprev;
};

struct ThrData {
	Channel *resp;
};

struct Dev {
	char *name;
	int size;
	Buf buf[BUFHASH+1]; /* doubly-linked list */
	Dev *next;
	int fd;
	Rendez workr;
	QLock workl;
	Buf work;
};

extern Dev *devs;

struct FLoc {
	uvlong blk;
	int deind;
	Qid;
};

enum {
	LGONE = 1,
	LDUMPED = 2,
};

struct Loc {
	FLoc;
	int ref, flags;
	Loc *next, *child;
	Loc *cnext, *cprev;
	Loc *gnext, *gprev;
	
	QLock ex;
	Chan *exlock;
	ulong lwrite;
};

enum {
	FSNOAUTH = 1,
	FSNOPERM = 2,
	FSCHOWN = 4,
};

struct Fs {
	RWLock;
	Dev *d;
	int flags;
	uvlong root, fstart;
	
	Channel *freelist;
	Loc *rootloc, *dumprootloc;
	QLock loctree;

	User *udata;
	int nudata;
	RWLock udatal;
};

enum {
	CHREAD = 1,
	CHWRITE = 2,
	CHRCLOSE = 4,

	CHFDUMP = 1,
	CHFNOLOCK = 2,
	CHFRO = 4,
	CHFNOPERM = 8,
	
	CHWBUSY = 1,
	CHWCLUNK = 2,
};


struct Chan {
	Fs *fs;
	Loc *loc;
	uchar open;
	uchar flags;
	ushort uid;

	/* dir walk */
	uvlong dwloff;
	uvlong dwblk;
	int dwind;
	
	/* workers */
	void *freq, *lreq;
	Chan *qnext, *qprev;
	int wflags;
};

extern QLock chanqu;
extern Rendez chanre;
extern Chan readych;

extern char Eio[];
extern char Enotadir[];
extern char Enoent[];
extern char Einval[];
extern char Eperm[];
extern char Eexists[];
extern char Elocked[];

enum { /* getblk modes */
	GBREAD = 0,
	GBWRITE = 1,
	GBCREATE = 2,
	GBOVERWR = 3,
};

#define HOWMANY(a) (((a)+(RBLOCK-1))/RBLOCK)
