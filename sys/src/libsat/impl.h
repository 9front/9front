/* note that internally, literals use a representation different from the API.
 * variables are numbered from 0 (not 1) and 2v and 2v+1 correspond to v
 * and ¬v, resp.  */
#define VAR(l) ((l)>>1)
#define NOT(l) ((l)^1)

/* l[0] and l[1] are special: they are the watched literals.
 * all clauses that have literal l on their watchlist form a linked list starting with s->lit[l].watch
 * and watch[i] having the next clause for l[i] */
struct SATClause {
	SATClause *next;
	SATClause *watch[2];
	double activity; /* activity is increased every time a clause is used to resolve a conflict (tiebreaking heuristic during purging) */
	int n; /* >= 2 for learned clauses and > 2 for input clauses (binary input clauses are kept in the bimp tables) */
	ushort range; /* heuristic used during purging, low range => keep clause (range 0..256) */
	int l[1];
};

struct SATLit {
	int *bimp; /* array of literals implied by this literal through binary clauses (Binary IMPlications) */
	SATClause *watch; /* linked list of watched clauses */
	int nbimp;
	char val; /* -1 = not assigned, 0 = false, 1 = true */
};

struct SATVar {
	double activity; /* activity is increased every time a variable shows up in a conflict */
	union {
		SATClause *reason; /* nil for decision and free literals */
		int binreason; /* used when isbinreason == 1: the reason is the clause l ∨ l->binreason */
	};
	int lvl; /* level at which this variable is defined, or -1 for free variables */
	int heaploc; /* location in binary heap or -1 when not in heap */
	uint stamp; /* "stamp" value used for conflict resolution etc. */
	uchar flags; /* see below */
	char isbinreason;
};

enum {
	VARPHASE = 1, /* for a free variables, VARPHASE is used as a first guess the next time it is picked */
	VARUSER = 0x80, /* user assigned variable (unit clause in input) */
};

/* records conflicts during purging */
struct SATConflict {
	union {
		SATClause *c;
		uvlong b;
	};
	int lvl; /* bit 31 denotes binary conflict */
};
#define CFLLVL(c) ((c).lvl & 0x7fffffff)

enum {
	SATBLOCKSZ = 65536,
	SATVARALLOC = 64,
	CLAUSEALIGN = 8,
	CFLCLALLOC = 16,
};

void saterror(SATSolve *, char *, ...);
void sataddtrail(SATSolve *, int);
void satdebuginit(SATSolve *);
void satprintstate(SATSolve *);
void satsanity(SATSolve *);
SATVar *satheaptake(SATSolve *);
void satheapput(SATSolve *, SATVar *);
void satreheap(SATSolve *, SATVar *);
void satheapreset(SATSolve *);
int satnrand(SATSolve *, int);
void *satrealloc(SATSolve *, void *, ulong);
SATClause *satnewclause(SATSolve *, int, int);
SATClause *satreplclause(SATSolve *, int);
void satcleanup(SATSolve *, int);
void satbackjump(SATSolve *, int);

#define signf(l) (((l)<<31>>31|1)*((l)+2>>1))
#pragma varargck type "Γ" SATClause*

#define ε 2.2250738585072014e-308
#define MAXACTIVITY 1e100
