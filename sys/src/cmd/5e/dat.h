typedef struct Process Process;
typedef struct Segment Segment;
typedef struct Fdtable Fdtable;
typedef struct Fd Fd;

enum {
	STACKTOP = 0x80000000UL,
	STACKSIZE = 0x10000,
	
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
	Segment* S[SEGNUM];
	u32int R[16];		/* general purpose registers / PC (R15) */
	u32int CPSR;		/* status register */
	char errbuf[ERRMAX];
	Fd *fd;
	int pid;
};

extern void **_privates;
extern int _nprivates;
#define P (*(Process**)_privates)

enum {
	SEGFLLOCK = 1,
};

struct Segment {
	Ref;
	int flags;
	RWLock rw; /* lock for SEGLOCK segments */
	Lock lock; /* atomic accesses */
	u32int start, size;
	void *data;
	Ref *ref;
};

struct Fd {
	RWLock;
	Ref ref;
	u8int *fds;
	int nfds;
};

#define fulltrace 0
#define havesymbols 0
#define ultraverbose 0
#define systrace 0

