typedef struct Node Node;
typedef struct Symbol Symbol;
typedef struct SymTab SymTab;
typedef struct Clause Clause;
typedef struct Enab Enab;
typedef struct Stat Stat;
typedef struct Type Type;
typedef struct Agg Agg;

enum {
	SYMHASH = 256,
};

struct Type {
	enum {
		TYPINVAL,
		TYPINT,
		TYPPTR,
		TYPSTRING,
	} type;
	int size;
	uchar sign;
	Type *ref;
	Type *typenext;
};

struct Symbol {
	enum {
		SYMNONE,
		SYMVAR,
	} type;
	char *name;
	int idx;
	Symbol *next;
	Type *typ;
};

struct SymTab {
	Symbol *sym[SYMHASH];
};

struct Node {
	enum {
		OINVAL,
		OSYM,
		ONUM,
		OSTR,
		OBIN,
		OLNOT,
		OTERN,
		ORECORD,
		OCAST,
	} type;
	enum {
		OPINVAL,
		OPADD,
		OPSUB,
		OPMUL,
		OPDIV,
		OPMOD,
		OPAND,
		OPOR,
		OPXOR,
		OPLSH,
		OPRSH,
		OPEQ,
		OPNE,
		OPLT,
		OPLE,
		OPLAND,
		OPLOR,
		OPXNOR,
	} op;
	Node *n1, *n2, *n3;
	Symbol *sym;
	char *str;
	s64int num;
	
	/* used by elidecasts() */
	char databits;
	enum {UPZX, UPSX} upper;
	
	int recsize;
	
	Type *typ;
};

struct Stat {
	enum {
		STATEXPR,
		STATPRINT,
		STATPRINTF,
		STATAGG,
	} type;
	/* STATEXPR */
	Node *n;
	/* STATPRINT, STATPRINTF */
	int narg;
	Node **arg;
	/* STATAGG */
	struct {
		Symbol *name;
		int type;
		Node *key, *value;
	} agg;
};

struct Clause {
	int id;
	Stat *stats;
	int nstats;
	char **probs;
	int nprob;
	DTExpr *pred;
};

struct Enab {
	int epid;
	int reclen;
	char *probe;
	Clause *cl;
	Enab *next;
};

struct Agg {
	DTAgg;
	char *name;
};

extern int errors;

#pragma	varargck	type	"α"	int
#pragma varargck	type	"t"	int
#pragma varargck	type	"τ"	Type *
#pragma varargck	type	"ε"	Node *
#pragma varargck	argpos error 1

extern int dflag;
extern DTAgg noagg;
extern int aggid;
extern Agg *aggs;
