enum{
	NWWW=64,	/* # of pages we hold in the log */
	NXPROC=5,	/* # of parallel procs loading the pix */
	NPIXMB=8,	/* megabytes of image data to keep arround */
	NNAME=512,
	NLINE=256,
	NAUTH=128,
	NTITLE=81,	/* length of title (including nul at end) */
	NLABEL=50,	/* length of option name in forms */
	NREDIR=10,	/* # of redirections we'll tolerate before declaring a loop */
};

typedef struct Action Action;
typedef struct Url Url;
typedef struct Www Www;
typedef struct Field Field;
struct Action{
	char *image;
	Field *field;
	char *link;
	char *name;
	int ismap;
	int width;
	int height;
};
struct Url{
	char *basename;
	char *reltext;
	char fullname[NNAME];
	char tag[NNAME];
	char contenttype[NNAME];
	int map;		/* is this an image map? */
};
struct Www{
	Url *url;
	void *pix;
	void *form;
	char title[NTITLE];
	Rtext *text;
	int yoffs;
	int gottitle;		/* title got drawn */
	int changed;		/* reader sets this every time it updates page */
	int finished;		/* reader sets this when done */
	int alldone;		/* page will not change further -- used to adjust cursor */
};

enum{
	PLAIN,
	HTML,

	GIF,
	JPEG,
	PNG,
	BMP,
	ICO,

	PAGE,
};

/*
 *  authentication types
 */
enum{
	ANONE,
	ABASIC,
};

Image *hrule, *bullet, *linespace;
int chrwidth;		/* nominal width of characters in font */
Panel *text;		/* Panel displaying the current www page */
int debug;		/* command line flag */

/*
 * HTTP methods
 */
enum{
	GET=1,
	POST,
};

void finish(Www *w);
void plrdhtml(char *, int, Www *, int);
void plrdplain(char *, int, Www *);
void htmlerror(char *, int, char *, ...);	/* user-supplied routine */
void seturl(Url *, char *, char *);
void freeurl(Url *);
Url *selurl(char *);
void getpix(Rtext *, Www *);
ulong countpix(void *p);
void freepix(void *p);
void dupfds(int fd, ...);
int pipeline(int fd, char *fmt, ...);
void getfonts(void);
void *emalloc(int);
void nstrcpy(char *to, char *from, int len);
void freeform(void *p);
int Ufmt(Fmt *f);
#pragma	varargck type "U" char*
void message(char *, ...);
int filetype(int, char *, int);
int mimetotype(char *);
int snooptype(int);
void mkfieldpanel(Rtext *);
void geturl(char *, int, int, int);
char *urlstr(Url *);
int urlpost(Url*, char*);
int urlget(Url*, int);
int urlresolve(Url *);
Mouse mouse;
