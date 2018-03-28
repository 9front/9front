typedef struct Line Line;
typedef struct Node Node;
typedef struct Symbol Symbol;
typedef struct Trie Trie;
typedef struct TrieHead TrieHead;

struct Line {
	char *filen;
	int lineno;
};

struct TrieHead {
	uvlong hash;
	int l;
};

enum { TRIEB = 4 };
struct Trie {
	TrieHead;
	Trie *n[1<<TRIEB];
};

struct Symbol {
	TrieHead;
	char *name;
	int type;
	int size;
	int flags;
	int *vars;
	Symbol *next;
};
enum {
	SYMNONE,
	SYMBITS,
};
enum {
	SYMFSIGNED = 1,
};

struct Node {
	int type;
	Line;
	int op;
	mpint *num;
	Symbol *sym;
	Node *n1, *n2, *n3;
	int size;
	int *vars;
};
enum {
	ASTINVAL,
	ASTSYM,
	ASTNUM,
	ASTBIN,
	ASTUN,
	ASTIDX,
	ASTTERN,
	ASTTEMP,
};
enum {
	OPINVAL,
	OPABS,
	OPADD,
	OPAND,
	OPASS,
	OPCOM,
	OPCOMMA,
	OPDIV,
	OPEQ,
	OPEQV,
	OPGE,
	OPGT,
	OPIMP,
	OPLAND,
	OPLE,
	OPLOR,
	OPLSH,
	OPLT,
	OPMOD,
	OPMUL,
	OPNEG,
	OPNEQ,
	OPNOT,
	OPOR,
	OPRSH,
	OPSUB,
	OPXOR,
};

extern Symbol *syms;

#pragma varargck type "ε" Node*
#pragma varargck type "α" int
#pragma varargck type "O" int
