enum
{
	Elemlen	= 56,
	Pathlen	= 256,
};

#include "sys.h"
#include <String.h>

enum{
	Fields	= 18,

	/* flags */
	Fanswered	= 1<<0, /* a */
	Fdeleted		= 1<<1, /* D */
	Fdraft		= 1<<2, /* d */
	Fflagged		= 1<<3, /* f */
	Frecent		= 1<<4, /* r	we are the first fs to see this */
	Fseen		= 1<<5, /* s */
	Fstored		= 1<<6, /* S */
	Nflags		= 7,
};

/*
 * flag.c
 */
char	*flagbuf(char*, int);
int	buftoflags(char*);
char	*txflags(char*, uchar*);

/*
 *  routines in aux.c
 */
char	*mboxpathbuf(char*, int, char*, char*);
char	*basename(char*);
int	shellchars(char*);
String	*escapespecial(String*);
String	*unescapespecial(String*);
int	returnable(char*);

/* folder.c */
Biobuf	*openfolder(char*, long);
int	closefolder(Biobuf*);
int	appendfolder(Biobuf*, char*, int);
int	fappendfolder(char*, long, char *, int);
int	fappendfile(char*, char*, int);
char*	foldername(char*, char*, char*);
char*	ffoldername(char*, char*, char*);

/* fmt.c */
void	mailfmtinstall(void);	/* 'U' = 2047fmt */
#pragma varargck	type	"U"	char*

/* totm.c */
int	fromtotm(char*, Tm*);

/* a pipe between parent and child*/
typedef struct{
	Biobuf	bb;
	Biobuf	*fp;	/* parent process end*/
	int	fd;	/* child process end*/
} stream;

/* a child process*/
typedef struct{
	stream	*std[3];	/* standard fd's*/
	int	pid;		/* process identifier*/
	int	status;		/* exit status*/
	Waitmsg	*waitmsg;
} process;

stream	*instream(void);
stream	*outstream(void);
void	stream_free(stream*);
process	*noshell_proc_start(char**, stream*, stream*, stream*, int, char*);
process	*proc_start(char*, stream*, stream*, stream*, int, char*);
int	proc_wait(process*);
int	proc_free(process*);
//int	proc_kill(process*);
