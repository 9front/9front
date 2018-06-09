#pragma src "/sys/src/libttf"
#pragma lib "libttf.a"

typedef struct TTTable TTTable;
typedef struct TTChMap TTChMap;
typedef struct TTPoint TTPoint;
typedef struct TTGlyph TTGlyph;
typedef struct TTGlyphInfo TTGlyphInfo;
typedef struct TTFontU TTFontU;
typedef struct TTFont TTFont;
typedef struct TTFunction TTFunction;
typedef struct TTGState TTGState;
typedef struct TTBitmap TTBitmap;
typedef struct TTKern TTKern;
typedef struct Biobuf Biobuf;

struct TTTable {
	u32int tag;
	u32int csum;
	u32int offset;
	u32int len;
};
struct TTChMap {
	int start, end, delta;
	int *tab;
	enum {
		TTCDELTA16 = 1,
		TTCINVALID = 2,
	} flags;
	int temp;
};
struct TTPoint {
	int x, y;
	u8int flags;
};
struct TTBitmap {
	u8int *bit;
	int width, height, stride;
};
struct TTGlyph {
	TTBitmap;
	int idx;
	int xmin, xmax, ymin, ymax;
	int xminpx, xmaxpx, yminpx, ymaxpx;
	int advanceWidthpx;
	TTPoint *pt;
	TTPoint *ptorg;
	int npt;
	u16int *confst;
	int ncon;
	u8int *hint;
	int nhint;
	TTFont *font;
	TTGlyphInfo *info;
};
struct TTGlyphInfo {
	int loca;
	u16int advanceWidth;
	short lsb;
};
struct TTFunction {
	u8int *pgm;
	int npgm;
};
struct TTGState {
	int pvx, pvy;
	int dpvx, dpvy;
	int fvx, fvy;
	u32int instctrl;
	u32int scanctrl;
	u32int scantype;
	int rperiod, rphase, rthold;
	u8int zp;
	int rp[3];
	int cvci;
	int loop;
	int mindist;
	int deltabase, deltashift;
	u8int autoflip;
	u32int singlewval, singlewci;
};
struct TTKern {
	u32int idx;
	int val;
};
struct TTFontU {
	int ref;

	Biobuf *bin;

	TTTable *tab;
	u16int ntab;
	
	TTChMap *cmap;
	int ncmap;
	
	short *cvtu;
	int ncvtu;

	u16int flags;
	int emsize;
	short xmin, ymin, xmax, ymax;
	u16int longloca;
	
	TTGlyphInfo *ginfo;
	
	u16int numGlyphs;
	u16int maxPoints;
	u16int maxCountours;
	u16int maxComponentPoints;
	u16int maxComponentCountours;
	u16int maxZones;
	u16int maxTwilightPoints;
	u16int maxStorage;
	u16int maxFunctionDefs;
	u16int maxInstructionDefs;
	u16int maxStackElements;
	u16int maxSizeOfInstructions;
	u16int maxComponentElements;
	u16int maxComponentDepth;
	
	int ascent, descent;
	
	u16int advanceWidthMax;
	u16int minLeftSideBearing;
	u16int minRightSideBearing;
	u16int xMaxExtent;
	u16int numOfLongHorMetrics;
	
	TTKern *kern;
	int nkern;
};
struct TTFont {
	TTFontU *u;
	int ascentpx, descentpx;
	int ppem;
	TTGState;
	TTGState defstate;
	TTPoint *twilight, *twiorg;
	u32int *hintstack;
	TTFunction *func;
	u32int *storage;
	int *cvt;
	int ncvt;
};

TTFont *ttfopen(char *, int, int);
TTFont *ttfscale(TTFont *, int, int);
void ttfclose(TTFont *);
int ttffindchar(TTFont *, Rune);
int ttfenumchar(TTFont *, Rune, Rune *);
TTGlyph *ttfgetglyph(TTFont *, int, int);
void ttfputglyph(TTGlyph *);
int ttfgetcontour(TTGlyph *, int, float **, int *);

enum {
	TTFLALIGN = 0,
	TTFRALIGN = 1,
	TTFCENTER = 2,
	TTFMODE = 3,
	TTFJUSTIFY = 4,
};

TTBitmap *ttfrender(TTFont *, char *, char *, int, int, int, char **);
TTBitmap *ttfrunerender(TTFont *, Rune *, Rune *, int, int, int, Rune **);
TTBitmap *ttfnewbitmap(int, int);
void ttffreebitmap(TTBitmap *);
void ttfblit(TTBitmap *, int, int, TTBitmap *, int, int, int, int);
