typedef struct Ident Ident;
typedef struct Win Win;
typedef struct Wintab Wintab;
typedef struct Pal Pal;
typedef struct Spr Spr;
typedef struct File File;

enum {
	BORDSIZ = 5,
	MINSIZ = 3 * BORDSIZ,
	SELSIZ = 2,
	SCRBSIZ = 11,
	SCRTSIZ = 14,
	RUNEBLK = 4096,
};

enum {
	DISB = NCOL,
	NCOLS
};

enum {
	CMD,
	PAL,
	SPR,
	NTYPES
};

struct Wintab {
	int (*init)(Win *);
	void (*die)(Win *);
	void (*click)(Win *, Mousectl *);
	void (*menu)(Win *, Mousectl *);
	int (*rmb)(Win *, Mousectl *);
	void (*key)(Win *, Rune);
	void (*draw)(Win *);
	void (*zerox)(Win *, Win *);
	u32int hexcols[NCOLS];
	Image *cols[NCOLS];
};

struct Win {
	Rectangle entire;
	Rectangle inner;
	Image *im;
	Win *next, *prev;
	Win *wnext, *wprev;
	int type;
	Wintab *tab;
	
	Frame fr;
	Rune *runes;
	int nrunes, arunes, opoint;
	int toprune;
	
	int zoom;
	Point scr;
	File *f;
	Rectangle sprr;
};

struct Ident {
	uint type, dev;
	Qid;
};

struct File {
	int type;
	Ref;
	File *next, *prev;
	char *name;
	int change;
	Ident id;
	Win wins;
};

struct Pal {
	File;
	int ncol;
	u32int *cols;
	Image **ims;
	int sel;
};

struct Spr {	
	File;
	Pal *pal;
	int w, h;
	u32int *data;
	char *palfile;
};

extern Win wlist;
extern File flist;
extern Win *actw, *actf, *cmdw;
extern Screen *scr;
extern Image *invcol;
extern int quitok;
