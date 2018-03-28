typedef struct SATSolve SATSolve;

void *emalloc(ulong);
void *erealloc(void *, ulong);
void parse(char *);
void error(Line *, char *, ...);
Node *node(int t, ...);
Symbol *symget(char *);
void convert(Node *, uint);
void obviously(Node *);
void go(int);
void assume(Node *);
int satand1(SATSolve *, int *, int);
int satandv(SATSolve *, ...);
int sator1(SATSolve *, int *, int);
int satorv(SATSolve *, ...);
int satlogic1(SATSolve *, u64int, int *, int);
int satlogicv(SATSolve *, u64int, ...);
