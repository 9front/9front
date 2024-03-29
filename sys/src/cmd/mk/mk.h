#include	<u.h>
#include	<libc.h>
#include	<bio.h>
#include	<regexp.h>

extern Biobuf bout;

typedef struct Bufblock
{
	struct Bufblock *next;
	char 		*start;
	char 		*end;
	char 		*current;
} Bufblock;

typedef struct Word
{
	struct Word 	*next;
	char 		s[1];
} Word;

typedef struct Symtab
{
	union{
		void	*ptr;
		uintptr	value;
	} u;
	struct Symtab	*next;
	unsigned char	space;
	char		name[1];
} Symtab;

enum {
	S_VAR,		/* variable -> value */
	S_TARGET,	/* target -> rule */
	S_TIME,		/* file -> time */
	S_NODE,		/* target name -> node */
	S_AGG,		/* aggregate -> time */
	S_BITCH,	/* bitched about aggregate not there */
	S_NOEXPORT,	/* var -> noexport */
	S_OVERRIDE,	/* can't override */
	S_OUTOFDATE,	/* "cmp 'file1' 'file2'\n" -> 2(outofdate) or 1(not outofdate) */
	S_BULKED,	/* directory; we have bulked */
	S_WESET,	/* variable; we set in the mkfile */
	S_INTERNAL,	/* variable -> value; an internal mk variable (e.g., stem, target) */
};

typedef struct Rule
{
	Symtab 		*target;	/* one target */
	Word 		*tail;		/* constituents of targets */
	char 		*recipe;	/* do it ! */
	short 		attr;		/* attributes */
	short 		line;		/* source line */
	char 		*file;		/* source file */
	Word 		*alltargets;	/* all the targets */
	int 		rule;		/* rule number */
	Reprog		*pat;		/* reg exp goo */
	char		*prog;		/* to use in out of date */
	struct Rule	*chain;		/* hashed per target */
	struct Rule	*next;
} Rule;

extern Rule *rules, *metarules, *patrule;

/*	Rule.attr	*/
#define		META		0x0001
#define		UNUSED		0x0002
#define		UPD		0x0004
#define		QUIET		0x0008
#define		VIR		0x0010
#define		REGEXP		0x0020
#define		NOREC		0x0040
#define		DEL		0x0080
#define		NOVIRT		0x0100

#define		NREGEXP		10

typedef struct Arc
{
	short		flag;
	struct Node	*n;
	Rule		*r;
	char		*stem;
	char		*prog;
	char		*match[NREGEXP];
	struct Arc	*next;
} Arc;

	/* Arc.flag */
#define		TOGO		1

typedef struct Node
{
	char		*name;
	long		time;
	unsigned short	flags;
	Arc		*prereqs;
	struct Node	*next;		/* list for a rule */
} Node;

	/* Node.flags */
#define		VIRTUAL		0x0001
#define		CYCLE		0x0002
#define		READY		0x0004
#define		CANPRETEND	0x0008
#define		PRETENDING	0x0010
#define		NOTMADE		0x0020
#define		BEINGMADE	0x0040
#define		MADE		0x0080
#define		MADESET(n,m)	n->flags = (n->flags&~(NOTMADE|BEINGMADE|MADE))|(m)
#define		PROBABLE	0x0100
#define		VACUOUS		0x0200
#define		NORECIPE	0x0400
#define		DELETE		0x0800
#define		NOMINUSE	0x1000

typedef struct Job
{
	Rule		*r;	/* master rule for job */
	Node		*n;	/* list of node targets */
	char		*stem;
	char		**match;
	Word		*p;	/* prerequistes */
	Word		*np;	/* new prerequistes */
	Word		*t;	/* targets */
	Word		*at;	/* all targets */
	int		nproc;	/* slot number */
	struct Job	*next;
} Job;
extern Job *jobs;

extern	int	debug;
extern	int	nflag, tflag, iflag, kflag, aflag, mflag;
extern	int	mkinline;
extern	char	*mkinfile;
extern	int	nreps;
extern	char	*explain;
extern	char	termchars[];
extern	char 	shell[];
extern	char 	shellname[];

#define	SYNERR(l)	(fprint(2, "mk: %s:%d: syntax error; ", mkinfile, ((l)>=0)?(l):mkinline))
#define	NAMEBLOCK	1000
#define	BIGBLOCK	20000

#define	SEP(c)		(((c)==' ')||((c)=='\t')||((c)=='\n'))
#define WORDCHR(r)	((r) > ' ' && !utfrune("!\"#$%&'()*+,-./:;<=>?@[\\]^`{|}~", (r)))

#define	DEBUG(x)	(debug&(x))
#define		D_PARSE		0x01
#define		D_GRAPH		0x02
#define		D_EXEC		0x04

#define	LSEEK(f,o,p)	seek(f,o,p)

#define	PERCENT(ch)	(((ch) == '%') || ((ch) == '&'))

#include	"fns.h"
