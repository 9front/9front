enum{
	NWWW=64,	/* # of pages we hold in the log */
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
typedef struct Scheme Scheme;
typedef struct Field Field;
struct Scheme{
	char *name;
	int type;
	int flags;
	int port;
};
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
	char fullname[NNAME];
	Scheme *scheme;
	char ipaddr[NNAME];
	char reltext[NNAME];
	char tag[NNAME];
	char redirname[NNAME];
	char autharg[NAUTH];
	char authtype[NTITLE];
	char charset[NNAME];
	int port;
	int access;
	int type;
	int map;			/* is this an image map? */
	int ssl;
};
struct Www{
	Url *url;
	Url *base;
	void *pix;
	void *form;
	char title[NTITLE];
	Rtext *text;
	int yoffs;
	int changed;		/* reader sets this every time it updates page */
	int finished;		/* reader sets this when done */
	int alldone;		/* page will not change further -- used to adjust cursor */
};

/*
 * url reference types -- COMPRESS and GUNZIP are flags that can modify any other type
 * Changing these in a non-downward compatible way spoils cache entries
 */
enum{
	GIF=1,
	HTML,
	JPEG,
	PIC,
	TIFF,
	AUDIO,
	PLAIN,
	XBM,
	POSTSCRIPT,
	FORWARD,
	PDF,
	SUFFIX,
	ZIP,
	PNG,

	COMPRESS=16,
	GUNZIP=32,
	COMPRESSION=16+32,
};

/*
 * url access types
 */
enum{
	HTTP=1,
	FTP,
	FILE,
	TELNET,
	MAILTO,
	GOPHER,
};

/*
 *  authentication types
 */
enum{
	ANONE,
	ABASIC,
};

Image *hrule, *bullet, *linespace;
char home[512];		/* where to put files */
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

void plrdhtml(char *, int, Www *);
void plrdplain(char *, int, Www *);
void htmlerror(char *, int, char *, ...);	/* user-supplied routine */
void crackurl(Url *, char *, Url *);
void getpix(Rtext *, Www *);
int pipeline(char *, int);
int urlopen(Url *, int, char *);
void getfonts(void);
void *emalloc(int);
void *emallocz(int, int);
void setbitmap(Rtext *);
void message(char *, ...);
int ftp(Url *);
int http(Url *, int, char *);
int gopher(Url *);
int cistrcmp(char *, char *);
int cistrncmp(char *, char *, int);
int suffix2type(char *);
int content2type(char *, char *);
int encoding2type(char *);
void mkfieldpanel(Rtext *);
void geturl(char *, int, char *, int, int);
int dir2html(char *, int);
int auth(Url*, char*, int);
char version[];
