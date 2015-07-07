typedef struct Process Process;
typedef struct Segment Segment;
typedef struct Fdtable Fdtable;
typedef struct Fd Fd;

enum {
	STACKSIZE = 0x100000,
	NAMEMAX = 27,
	NNOTE = 5,
	SEGNUM = 8,
	Nfpregs = 16,

	flN = 1<<31,
	flZ = 1<<30,
	flC = 1<<29,
	flV = 1<<28,
	FLAGS = flN | flZ | flC | flV,
};

enum {
	SEGTEXT,
	SEGDATA,
	SEGBSS,
	SEGSTACK,
};

struct Process {
	Process *prev, *next;	/* linked list (for fs) */
	int pid;
	char name[NAMEMAX+1];	/* name for status file */
	Ref *path;		/* Ref + string data */

	Segment *S[SEGNUM];	/* memory */

	u32int lladdr;		/* LL/SC emulation */
	u32int llval;

	u32int R[16];		/* general purpose registers / PC (R15) */
	u32int CPSR;		/* status register */

	u32int FPSR;
	long double F[Nfpregs];

	char errbuf[ERRMAX];
	Fd *fd;			/* bitmap of OCEXEC files */
	
	/* note handling */
	u32int notehandler;
	int innote;
	jmp_buf notejmp;
	char notes[ERRMAX][NNOTE];
	long notein, noteout;
};

int vfp;

extern void **_privates;
extern int _nprivates;
#define P (*(Process**)_privates)
extern Ref nproc;
extern Process plist;
extern Lock plistlock;

enum {
	SEGFLLOCK = 1,
};

struct Segment {
	Ref;
	int flags;
	RWLock rw; /* lock for SEGFLLOCK segments */
	u32int start, size;
	void *data;
	Ref *dref;
};

struct Fd {
	RWLock;
	Ref;
	u8int *fds;
	int nfds;
};

#define fulltrace 0
#define havesymbols 0
#define ultraverbose 0
#define systrace 0
