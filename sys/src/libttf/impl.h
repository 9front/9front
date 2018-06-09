enum {
	ONCURVE = 1,
	TOUCHX = 2,
	TOUCHY = 4,
	TOUCH = 6,
};

int ttfgototable(TTFontU *, char *);
int ttfrounddiv(int, int);
int ttfvrounddiv(vlong, int);
int ttfhint(TTGlyph *);
int ttfparsecmap(TTFontU *);
int ttfrunfpgm(TTFont *);
int ttfruncvt(TTFont *);
void ttfunpack(TTFontU *, char *, ...);
void ttfscan(TTGlyph *);
void ttffreebitmap(TTBitmap *);
