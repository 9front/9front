#pragma lib "libdtracy.a"
#pragma src "/sys/src/libdtracy"

/*
	triggered probes run in "probe context", which involves grabbing a per-CPU lock using dtmachlock/dtmachunlock.
	everything else in the library assumes that the calling code has grabbed a global lock (which is done in 9/port/devtracy.c).
*/

enum {
	DTSTRMAX = 256,
	DTRECMAX = 1024,
	
	DTMAXAGGBUF = 16,
	
	DTBUFSZ = 65536,
	DTANUMBUCKETS = 1024,
	DTABUCKETS = DTBUFSZ - 4 * DTANUMBUCKETS,
};
#define DTANIL ((u32int)-1)

typedef struct DTProbe DTProbe;
typedef struct DTExprState DTExprState;
typedef struct DTAct DTAct;
typedef struct DTActGr DTActGr;
typedef struct DTClause DTClause;
typedef struct DTEnab DTEnab;
typedef struct DTChan DTChan;
typedef struct DTExpr DTExpr;
typedef struct DTProvider DTProvider;
typedef struct DTAgg DTAgg;
typedef struct DTBuf DTBuf;
typedef struct DTTrigInfo DTTrigInfo;

/*
	we assign all pairs (probe,action-group) (called an enabling or DTEnab) a unique ID called EPID.
	we could also use probe IDs and action group IDs but using a single 32-bit ID for both is more flexible/efficient.
	
	epid == -1 indicates a fault record (see below)
*/
struct DTEnab {
	u32int epid;
	DTActGr *gr;
	DTEnab *probnext, *probprev;
	DTEnab *channext;
	DTProbe *prob;
};

/* probes are never freed */
struct DTProbe {
	int nenable;
	char *name;
	DTEnab enablist;
	DTProvider *prov;
	void *aux; /* for the provider */
	DTProbe *provnext;
};

struct DTProvider {
	char *name;
	/*
		provide() is called when the user first uses a provider.
		provide() should call dtpnew() to create probes.
	*/
	void (*provide)(DTProvider *);
	int (*enable)(DTProbe *); /* enable the probe. return >= 0 for success and < 0 for failure */
	void (*disable)(DTProbe *); /* disable the probe */
	
	/* for the library, not the provider */
	DTProbe *probes;
	int provided;
};

/*
	the dtracy vm is a simple RISC machine with (currently) 16 64-bit registers.
	all operations are 64-bit.

	instruction encoding:
	31:24 opcode
	23:16 a
	15:8 b
	7:0 c
*/

enum {
	/* Rc = Ra (op) Rb */
	DTE_ADD = 0x00,
	DTE_SUB = 0x01,
	DTE_MUL = 0x02,
	DTE_SDIV = 0x03,
	DTE_SMOD = 0x04,
	DTE_UDIV = 0x05,
	DTE_UMOD = 0x06,
	DTE_AND = 0x07,
	DTE_OR = 0x08,
	DTE_XOR = 0x09,
	DTE_XNOR = 0x0A,
	DTE_LSL = 0x0B, /* logical shift left */
	DTE_LSR = 0x0C, /* logical shift right */
	DTE_ASR = 0x0D, /* arithmetic shift right */
	DTE_SEQ = 0x10, /* set if equal */
	DTE_SNE = 0x11, /* set if not equal */
	DTE_SLT = 0x12, /* set if less than */
	DTE_SLE = 0x13, /* set if less or equal */
	
	/* immediate operations, const = 10 top bits of ab, sign extended and shifted left by 6 bottom bits */
	DTE_LDI = 0x20, /* Rc = const */
	DTE_XORI = 0x21, /* Rc = Rc ^ const */

	/* if(Ra (op) Rb) PC += c; */
	DTE_BEQ = 0x30,
	DTE_BNE = 0x31,
	DTE_BLT = 0x32,
	DTE_BLE = 0x33,
	
	DTE_LDV = 0x40, /* R[b] = Var[a] */
	
	DTE_ZXT = 0x50, /* R[c] = lower b bits of R[a], zero extended, 0 < b <= 64 */
	DTE_SXT = 0x51, /* R[c] = lower b bits of R[a], sign extended, 0 < b <= 64 */
	
	DTE_RET = 0xF0, /* RET Ra */
};
#define DTE(op,a,b,c) ((op)<<24|(a)<<16|(b)<<8|(c))

struct DTExpr {
	int n;
	u32int *b;
};

/*
	aggregation buffers are hashtables and use a different record format.
	there are DTANUMBUCKETS 4-byte buckets at the end of the buffer.
	each entry is (link,id,key,val) with a 4-byte link field for the hash chains and a 4-byte aggregation id.
	
	the aggregation id actually contains all the data in the DTAgg struct:
	4-bit type
	12-bit keysize in qwords
	16-bit unique id
	
	the struct is just for kernel convenience
*/

enum {
	AGGCNT,
	AGGSUM,
	AGGAVG,
	AGGSTD,
	AGGMIN,
	AGGMAX,
};
	
struct DTAgg {
	int id;
	u16int keysize; /* in bytes */
	u16int recsize;
	uchar type;
};

/* an action is an expression, plus info about what to do with the result */
struct DTAct {
	enum {
		ACTTRACE, /* record the result. size is the number of bytes used. 0 <= size <= 8 */
		ACTTRACESTR, /* take the result to be a pointer to a null-terminated string. store it as zero-padded char[size]. */
		/*
			ACTAGGKEY and ACTAGGVAL together record a value in an aggregation.
			they must occur as a pair and targ must point to an already allocated aggregation buffer.
			currently 0 <= size <= 8.
		*/
		ACTAGGKEY,
		ACTAGGVAL,
		ACTCANCEL, /* (must be last action) don't write anything into the main buffer. used to avoid pointless records when using aggregations. */
	} type;
	DTExpr *p;
	int size;
	DTAgg agg;
};

/* an action group is an optional predicate and a set of actions. */
struct DTActGr {
	u32int id;
	int ref;
	DTExpr *pred; /* if non-nil and evaluates to 0, skip the actions. */
	int nact;
	DTAct *acts;
	DTChan *chan;
	int reclen; /* record size, including 12-byte header */
};

/* a clause lists probe wildcard expressions and an action group. only used during set-up. */
struct DTClause {
	int nprob;
	char **probs;
	DTActGr *gr;
};

struct DTBuf {
	int wr;
	uchar data[DTBUFSZ];
};

/* a chan is the kernel representation of a client. */
struct DTChan {
	enum {
		DTCSTOP,
		DTCGO,
		DTCFAULT,
	} state;
	char errstr[64];
	u32int epidalloc; /* lowest unused EPID */
	
	/* we have 2 buffers per cpu, one for writing and one for reading. dtcread() swaps them if empty. */
	DTBuf **wrbufs;
	DTBuf **rdbufs;
	/* aggregations use separate buffers */
	DTBuf **aggwrbufs;
	DTBuf **aggrdbufs;
	
	/* list of enablings. */
	DTEnab *enab;
};

/* this struct collects the state during the execution of a probe */
struct DTTrigInfo {
	/* filled in by caller of dtptrigger */
	u64int arg[10];
	/* filled in by dtptrigger */
	int machno;
	int epid;
	u64int ts;
	DTChan *ch;
};

/* fault records are used to note when a probe had to be aborted (e.g. because of a page fault) */
enum {
	DTFILL = 1, /* illegal address */
};

void dtinit(int);
void dtsync(void);

/* probe functions */
DTProbe *dtpnew(char *, DTProvider *, void *aux);
int dtpmatch(char *, DTProbe ***);
int dtplist(DTProbe ***);
void dtptrigger(DTProbe *, DTTrigInfo *);

/* expression functions */
int dteverify(DTExpr *);
int dtefmt(Fmt *);
#pragma varargck type "I" u32int

/* action group functions */
void dtgpack(Fmt *, DTActGr *);
char *dtgunpack(char *, DTActGr **);
int dtgverify(DTChan *, DTActGr *);
void dtgfree(DTActGr *);

/* clause functions */
void dtclpack(Fmt *, DTClause *);
char *dtclunpack(char *, DTClause **);
void dtclfree(DTClause *);

/* chan functions */
DTChan *dtcnew(void);
void dtcfree(DTChan *);
int dtcaddgr(DTChan *, char *, DTActGr *);
int dtcaddcl(DTChan *, DTClause *);
int dtcread(DTChan *, void *, int);
int dtcaggread(DTChan *, void *, int);
void dtcreset(DTChan *);
void dtcrun(DTChan *, int);
int dtcfault(DTTrigInfo *, int, char *, ...);

/* aggbuf functions */
int dtaunpackid(DTAgg *);
void dtarecord(DTChan *, int, DTAgg *, uchar *, int, s64int);

extern DTProvider *dtproviders[];
extern int dtnmach;

/* helper */
char *dtstrdup(char *);

/* these functions are provided by the kernel interface */
uvlong dttime(void); /* return current timestamp */
void *dtrealloc(void *, ulong);
void dtfree(void *);
void *dtmalloc(ulong);
int dtmachlock(int); /* lock the per-cpu lock */
void dtmachunlock(int); /* unlock the per-cpu lock */
void dtcoherence(void); /* memory barrier */
uvlong dtgetvar(int); /* return the value of a variable */
int dtpeek(uvlong, void*, int); /* safe memmemove(). returns -1 on error. */

enum {
	DTV_ARG0,
	DTV_ARG1,
	DTV_ARG2,
	DTV_ARG3,
	DTV_ARG4,
	DTV_ARG5,
	DTV_ARG6,
	DTV_ARG7,
	DTV_ARG8,
	DTV_ARG9,
	DTV_PID,
	DTV_MACHNO,
	DTV_TIME,
	DTV_PROBE,
	DTNVARS,
};
extern char *dtvarnames[DTNVARS];
