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
	char fullname[NNAME];
	char basename[NNAME];
	char reltext[NNAME];
	char tag[NNAME];
	int map;		/* is this an image map? */
};
struct Www{
	Url *url;
	void *pix;
	void *form;
	char title[NTITLE];
	Rtext *text;
	int yoffs;
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

void update(Www *w);
void finish(Www *w);
void plrdhtml(char *, int, Www *);
void plrdplain(char *, int, Www *);
void htmlerror(char *, int, char *, ...);	/* user-supplied routine */
void seturl(Url *, char *, char *);
void getpix(Rtext *, Www *);
ulong countpix(void *p);
void freepix(void *p);
int pipeline(char *, int);
int urlopen(Url *, int, char *);
void getfonts(void);
void *emalloc(int);
void *emallocz(int, int);
void nstrcpy(char *to, char *from, int len);
void freeform(void *p);
void message(char *, ...);
int snooptype(int fd);
void mkfieldpanel(Rtext *);
void geturl(char *, int, char *, int, int);
char version[];
