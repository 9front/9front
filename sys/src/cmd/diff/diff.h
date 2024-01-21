typedef struct Line	Line;
typedef struct Cand	Cand;
typedef struct Diff	Diff;
typedef struct Change	Change;

struct Line {
	int	serial;
	int	value;
};

struct Cand {
	int x;
	int y;
	int pred;
};

struct Change
{
	int oldx;
	int oldy;
	int newx;
	int newy;
};

struct Diff {
	Cand cand;
	Line *file[2], line;
	int len[2];
	int binary;
	int bindiff;
	Line *sfile[2];	/*shortened by pruning common prefix and suffix*/
	int slen[2];
	int pref, suff;	/*length of prefix and suffix*/
	int *class;	/*will be overlaid on file[0]*/
	int *member;	/*will be overlaid on file[1]*/
	int *klist;	/*will be overlaid on file[0] after class*/
	Cand *clist;	/* merely a free storage pot for candidates */
	int clen;
	int *J;		/*will be overlaid on class*/
	long *ixold;	/*will be overlaid on klist*/
	long *ixnew;	/*will be overlaid on file[1]*/
	char *file1;
	char *file2;
	Biobuf *input[2];
	Biobuf *b0;
	Biobuf *b1;
	int firstchange;
	Change *changes;
	int nchanges;
};

extern char mode;
extern char bflag;
extern char rflag;
extern char mflag;
extern int anychange;
extern Biobuf	stdout;

#define MAXPATHLEN	1024
#define MAXLINELEN	4096

#define	DIRECTORY(s)		((s)->qid.type&QTDIR)
#define	REGULAR_FILE(s)		((s)->type == 'M' && !DIRECTORY(s))

int mkpathname(char *, char *, char *);
char *mktmpfile(int, Dir **);
char *statfile(char *, Dir **);
void *emalloc(unsigned);
void *erealloc(void *, unsigned);
void diff(char *, char *, int);
void diffreg(char*, char*, char*, char*);
void diffdir(char *, char *, int);
void calcdiff(Diff *, char *, char *, char *, char *);
Biobuf *prepare(Diff*, int, char *, char *);
void check(Diff *, Biobuf *, Biobuf *);
void change(Diff *, int, int, int, int);
void freediff(Diff *);
void flushchanges(Diff *);
void fetch(Diff *d, long *f, int a, int b, Biobuf *bp, char *s);
int readline(Biobuf*, char*, int);
