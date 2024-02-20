/* Playlist begins with "# x\n" where x is the total number of records.
 * Each record begins with "# x y\n" where x is record index, y is its size in bytes.
 * Records are sorted according to mkplist.c:/^cmpmeta function.
 * This makes it somewhat easy to just load the whole playlist into memory once,
 * map all (Meta*)->... fields to it, saving on memory allocations, and using the same
 * data to provide poor's man full text searching.
 * Encoding: mkplist.c:/^printmeta/.
 * Decoding: zuke.c:/^readplist/.
 */
enum
{
	Precord='#',

	Palbum=			'a',
	Partist=		'A',
	Pbasename=		'b',
	Pcomposer=		'C',
	Pdate=			'd',
	Pduration=		'D',
	Pfilefmt=		'f',
	Pimage=			'i',
	Ptitle=			't',
	Ptrack=			'T',
	Ppath=			'p',
	Prgtrack=		'r',
	Prgalbum=		'R',

	/* unused */
	Pchannels=		'c',
	Psamplerate=	's',

	Maxartist=16, /* max artists for a track */
};

typedef struct Meta Meta;

struct Meta
{
	char *artist[Maxartist];
	char *album;
	char *composer;
	char *title;
	char *date;
	char *track;
	char *path;
	char *basename;
	char *imagefmt;
	char *filefmt;
	double rgtrack;
	double rgalbum;
	uvlong duration;
	int numartist;
	int imageoffset;
	int imagesize;
	int imagereader; /* non-zero if a special reader required */
};

void printmeta(Biobuf *b, Meta *m);
