#pragma lib "libsat.a"

typedef struct SATParam SATParam;
typedef struct SATClause SATClause;
typedef struct SATSolve SATSolve;
typedef struct SATBlock SATBlock;
typedef struct SATVar SATVar;
typedef struct SATLit SATLit;
typedef struct SATConflict SATConflict;
#pragma incomplete SATClause
#pragma incomplete SATVar
#pragma incomplete SATLit
#pragma incomplete SATConflict

/* user adjustable parameters */
struct SATParam {
	void (*errfun)(char *, void *);
	void *erraux;
	long (*randfn)(void *);
	void *randaux;
	
	uint goofprob; /* probability of making a random decision, times 2**31 */
	double varρ; /* Δactivity is multiplied by this after a conflict */
	double clauseρ; /* Δclactivity is multiplied by this after a conflict */
	int trivlim; /* number of extra literals we're willing to tolerate before substituting the trivial clause */
	int purgeΔ; /* initial purge interval (number of conflicts before a purge) */
	int purgeδ; /* increase in purge interval at purge */
	double purgeα; /* α weight factor for purge heuristic */
	u32int flushψ; /* agility threshold for restarts */
};

/* each block contains multiple SATClauses consecutively in its data region. each clause is 8 byte aligned and the total size is SATBLOCKSZ (64K) */
struct SATBlock {
	SATBlock *next, *prev;
	SATClause *last; /* last clause, ==nil for empty blocks */
	void *end; /* first byte past the last clause */
	uchar data[1];
};

struct SATSolve {
	SATParam;

	uchar unsat; /* ==1 if unsatisfiable. don't even try to solve. */
	uchar scratched; /* <0 if error happened, state undefined */

	SATBlock bl[2]; /* two doubly linked list heads: list bl[0] contains user clauses, list bl[1] contains learned clauses */
	SATBlock *lastbl; /* the last block we added a learned clause to */
	SATClause *cl; /* all clauses are linked together; this is the first user clause */
	SATClause *learncl; /* first learned clause */
	SATClause **lastp[2]; /* this points towards the last link in each linked list */
	int ncl; /* total number of clauses */
	int ncl0; /* number of user clauses */
	SATVar *var; /* all variables (array with nvar elements) */
	SATLit *lit; /* all literals (array with 2*nvar elements) */
	int nvar;
	int nvaralloc; /* space allocated for variables */
	int *trail; /* the trail contains all literals currently assumed true */
	int ntrail;
	int *decbd; /* decision boundaries. trail[decbd[i]] has the first literal of level i */
	int lvl; /* current decision level */
	SATVar **heap; /* binary heap with free variables, sorted by activity (nonfree variables are removed lazily and so may also be in it) */
	int nheap;
	
	uint *lvlstamp; /* used to "stamp" levels during conflict resolution and purging */
	uint stamp; /* current "stamp" counter */
	
	int forptr; /* trail[forptr] is the first literal we haven't explored the implications of yet */
	int binptr; /* ditto for binary implications */
	
	int *cflcl; /* during conflict resolution we build the learned clause in here */
	int ncflcl;
	int cflclalloc; /* space allocated for cflcl */
	int cfllvl; /* the maximum level of the literals in cflcl, cflcl[0] excluded */
	int cflctr; /* number of unresolved literals during conflict resolution */
	
	double Δactivity; /* activity increment for variables */
	double Δclactivity; /* activity increment for clauses */
	
	uvlong conflicts; /* total number of conflicts so far */
	
	uvlong nextpurge; /* purge happens when conflicts >= nextpurge */
	uint purgeival; /* increment for nextpurge */
	/* during a purge we do a "full run", assigning all variables and recording conflicts rather than resolving them */
	SATConflict *fullrcfl; /* conflicts found thus */
	int nfullrcfl;
	int fullrlvl; /* minimum cfllvl for conflicts found during purging */
	int *fullrlits; /* literals implied by conflicts at level fullrlvl */
	int nfullrlits;
	int rangecnt[256]; /* number of clauses with certain range values */
	
	u64int nextflush; /* flush happens when conflicts >= nextflush */
	u32int flushu, flushv, flushθ; /* variables for flush scheduling algorithm */
	u32int agility; /* agility is a measure how quickly variables are being flipped. high agility inhibits flushes */
	
	void *scrap; /* auxiliary memory that may need to be freed after a fatal error */
};

SATSolve *satnew(void);
SATSolve *satadd1(SATSolve *, int *, int);
SATSolve *sataddv(SATSolve *, ...);
SATSolve *satrange1(SATSolve *, int *, int, int, int);
SATSolve *satrangev(SATSolve *, int, int, ...);
int satsolve(SATSolve *);
int satmore(SATSolve *);
int satval(SATSolve *, int);
void satfree(SATSolve *);
void satreset(SATSolve *);
int satget(SATSolve *, int, int *, int);
void satvafix(va_list);
