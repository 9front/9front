typedef struct Process Process;
typedef struct Segment Segment;
typedef struct Fdtable Fdtable;
typedef struct Fd Fd;

enum {
	STACKSIZE = 0x100000,
	
	NAMEMAX = 27,
	
	FDBLOCK = 16,
	SEGNUM = 8,

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
	Segment *S[SEGNUM];
	u32int R[16];		/* general purpose registers / PC (R15) */
	u32int CPSR;		/* status register */
	char errbuf[ERRMAX];
	char name[NAMEMAX+1];
	Ref *path; /* Ref + string data */
	Fd *fd;
	int pid;
	Process *prev, *next;
};

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
	Lock lock; /* atomic accesses */
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

