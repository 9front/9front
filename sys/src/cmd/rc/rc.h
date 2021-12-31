/*
 * Plan9 is defined for plan 9
 * V9 is defined for 9th edition
 * Sun is defined for sun-os
 * Please don't litter the code with ifdefs.  The three below (and one in
 * getflags) should be enough.
 */
#define	Plan9
#ifdef	Plan9
#include <u.h>
#include <libc.h>
#define	NSIG	32
#define	SIGINT	2
#define	SIGQUIT	3
#endif
#ifdef	V9
#include <signal.h>
#include <libc.h>
#endif
#ifdef Sun
#include <signal.h>
#endif
#define	YYMAXDEPTH	500
#ifndef PAREN
#include "x.tab.h"
#endif
typedef struct tree tree;
typedef struct word word;
typedef struct io io;
typedef union code code;
typedef struct var var;
typedef struct list list;
typedef struct lexer lexer;
typedef struct redir redir;
typedef struct thread thread;
typedef struct builtin builtin;

#pragma incomplete word
#pragma incomplete io

struct tree{
	int	type;
	int	rtype, fd0, fd1;	/* details of REDIR PIPE DUP tokens */
	int	line;
	char	glob;			/* 0=string, 1=glob, -1=pattern see globprop() and noglobs() */
	char	quoted;
	char	iskw;
	char	*str;
	tree	*child[3];
	tree	*next;
};
tree *newtree(void);
tree *token(char*, int), *klook(char*), *tree1(int, tree*);
tree *tree2(int, tree*, tree*), *tree3(int, tree*, tree*, tree*);
tree *mung1(tree*, tree*), *mung2(tree*, tree*, tree*);
tree *mung3(tree*, tree*, tree*, tree*), *epimung(tree*, tree*);
tree *simplemung(tree*);
tree *globprop(tree*);
char *fnstr(tree*);

/*
 * The first word of any code vector is a reference count
 * and the second word is a string for srcfile().
 * Code starts at pc 2. The last code word must be a zero
 * terminator for codefree().
 * Always create a new reference to a code vector by calling codecopy(.).
 * Always call codefree(.) when deleting a reference.
 */
union code{
	void	(*f)(void);
	int	i;
	char	*s;
};

#define	NTOK	8192

struct lexer{
	io	*input;
	char	*file;
	int	line;

	char	*prolog;
	char	*epilog;

	int	peekc;
	int	future;
	int	lastc;

	char	eof;
	char	inquote;
	char	incomm;
	char	lastword;	/* was the last token read a word or compound word terminator? */
	char	lastdol;	/* was the last token read '$' or '$#' or '"'? */
	char	iflast;		/* static `if not' checking */

	char	qflag;

	char	tok[NTOK];
};
extern lexer *lex;		/* current lexer */
lexer *newlexer(io*, char*);
void freelexer(lexer*);

#define	APPEND	1
#define	WRITE	2
#define	READ	3
#define	HERE	4
#define	DUPFD	5
#define	CLOSE	6
#define RDWR	7

struct var{
	var	*next;		/* next on hash or local list */
	word	*val;		/* value */
	code	*fn;		/* pointer to function's code vector */
	int	pc;		/* pc of start of function */
	char	fnchanged;
	char	changed;
	char	name[];
};
var *vlook(char*), *gvlook(char*), *newvar(char*, var*);
void setvar(char*, word*), freevar(var*);

#define	NVAR	521

var *gvar[NVAR];				/* hash for globals */

#define	new(type)	((type *)emalloc(sizeof(type)))

void *emalloc(long);
void *erealloc(void *, long);
char *estrdup(char*);

int mypid;

/*
 * Glob character escape in strings:
 *	In a string, GLOB must be followed by *?[ or GLOB.
 *	GLOB* matches any string
 *	GLOB? matches any single character
 *	GLOB[...] matches anything in the brackets
 *	GLOBGLOB matches GLOB
 */
#define	GLOB	((char)0x01)
/*
 * onebyte(c)
 * Is c the first character of a one-byte utf sequence?
 */
#define	onebyte(c)	((c&0x80)==0x00)

extern char **argp;
extern char **args;
extern int nerror;		/* number of errors encountered during compilation */
extern int doprompt;		/* is it time for a prompt? */

/*
 * Which fds are the reading/writing end of a pipe?
 * Unfortunately, this can vary from system to system.
 * 9th edition Unix doesn't care, the following defines
 * work on plan 9.
 */
#define	PRD	0
#define	PWR	1
extern char Rcmain[], Fdprefix[];
