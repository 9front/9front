typedef struct Line Line;

struct Line {
	int	serial;
	int	value;
};
extern Line *file[2];
extern int len[2];
extern long *ixold, *ixnew;
extern int *J;
extern char mode;
extern char bflag;
extern char rflag;
extern char mflag;
extern int anychange;
extern Biobuf	stdout;
extern int	binary;

#define MAXPATHLEN	1024

int mkpathname(char *, char *, char *);
void *emalloc(unsigned);
void *erealloc(void *, unsigned);
void diff(char *, char *, int);
void diffdir(char *, char *, int);
void diffreg(char *, char *, char *, char *);
Biobuf *prepare(int, char *, char *);
void panic(int, char *, ...);
void check(Biobuf *, Biobuf *);
void change(int, int, int, int);
void flushchanges(void);

