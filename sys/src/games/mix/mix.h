#pragma varargck type "I" u32int

typedef struct Sym Sym;
typedef struct Refinst Refinst;
typedef struct Wval Wval;
typedef struct Con Con;

struct Sym {
	Avl;
	char *name;
	long lex;
	union {
		struct {
			int opc, f;	/* LOP LHERE LBACK LFORW */
		};
		u32int mval;	/* LSYMDEF */
		struct {
			int *refs, i, max;	/* LSYMREF */
		};
	};
	char nbuf[1];
};

struct Con {
	Sym *sym;
	u32int exp;
	Con *link;
};

int mixvm(int, int);
void repl(int);
u32int mixst(u32int, u32int, int);

void mixprint(int, int);

long yylex(void);
int yyparse(void);
void yyerror(char*, ...);
void vmerror(char*, ...);
void skipto(char);
Sym *sym(char*);
Sym *getsym(char*);
void sinit(void);
int asmfile(char*);
int V(u32int, int);
int Ifmt(Fmt*);

Rune mixtorune(int);
int runetomix(Rune);
void cinit(void);

void warn(char*, ...);
void *emalloc(ulong);
void *emallocz(ulong);
void *erealloc(void*, ulong);
char *estrdup(char*);
char *strskip(char*);
char *strim(char*);
void *bsearch(void*, void*, long, int, int(*)(void*, void*));

Avltree *syms;
int star, line, vmstart, yydone, curpc;
Con *cons;
char *filename;
extern int mask[5];
u32int cells[4000];
char bp[4000];
jmp_buf errjmp;
Biobuf bin;

u32int ra, rx, ri[7];
int ce, cl, cg, ot;

#define F(a, b) 8*(a)+(b)
#define UNF(a, b, f) ((a) = f/8, (b) = f%8)

enum {
	BITS = 6,
	MASK1 = 63,
	MASK2 = (63<<6) | MASK1,
	MASK3 = (63<<12) | MASK2,
	MASK4 = (63<<18) | MASK3,
	MASK5 = (63<<24) | MASK4,
	OVERB = 1<<30,
	SIGNB = 1<<31,
};
