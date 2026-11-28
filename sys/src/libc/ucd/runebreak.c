#include <u.h>
#include <libc.h>

#include "runebreakdata"

enum {
	OTHER, 
	Hebrew_Letter, Newline, Extend, Format,
	Katakana, ALetter, MidLetter, MidNum,
	MidNumLet, Numeric, ExtendNumLet, WSegSpace,
	PREPEND = 0x10, CONTROL = 0x20, EXTEND = 0x30, REGION = 0x40,
	L = 0x50, V = 0x60, T = 0x70, LV = 0x80, LVT = 0x90, SPACEMK = 0xA0,
	EMOJIEX = 0xB0, INLINK = 0xC0, INCONST = 0xD0,

	ZWJ = 0x200DU,
	LINETAB = 0xB,
};

#define IS(x, y) ((x&0xf) == y)
#define ISG(x, y) ((x&0xf0) == y)

/* Some InCB=Linker and EXTEND overlap */
#define ISGEX(x) ((x&0xf0) == EXTEND  || (x&0xf0) == (EXTEND|INLINK))
#define ISGIL(x) ((x&0xf0) == INLINK || (x&0xf0) == (EXTEND|INLINK))
#define ISGIE(r, x) (r == ZWJ || ((x&0xf0) == EXTEND && (x&0xf0) != INLINK && (x&0xf0) != INCONST && r != 0x200CU))

Rune*
runegbreak(Rune *s)
{
	int lt, rt;
	Rune l, r;
	Rune *lp, *rp;

	lp = s;
	if(*lp == 0)
		return nil;

	for(;; lp = rp){
		l = lp[0];
		rp = lp + 1;
		if((r = rp[0]) == 0)
			return nil;
		lt = breaklkup(l);
		rt = breaklkup(r);
		if(l == '\r' && r == '\n')
			continue;
		if(ISG(lt, CONTROL) || l == '\r' || l == '\n')
			return rp;
		if(ISG(rt, CONTROL) || r == '\r' || r == '\n')
			return rp;
		if(ISG(lt, L) && (ISG(rt, L) || ISG(rt, V) || ISG(rt, LV) || ISG(rt, LVT)))
			continue;
		if((ISG(lt, LV) || ISG(lt, V)) && (ISG(rt, V) || ISG(rt, T)))
			continue;
		if((ISG(lt, LVT) || ISG(lt, T)) && ISG(rt, T))
			continue;
		/* Out of order from the rules, GB11 is more specific than GB9... */
		if(ISG(lt, EMOJIEX)){
			while(ISGEX(rt)){
				if((r = rp[1]) == 0)
					return nil;
				rp++;
				rt = breaklkup(r);
			}
			if(r == ZWJ && breaklkup(rp[1]) == EMOJIEX){
				rp++;
				continue;
			}
		}
		/* Same as above, InCB=Extend overlaps with general EXTEND */
		if(ISGIL(lt)){
			while(ISGIE(r, rt)){
				if((r = rp[1]) == 0)
					return nil;
				rp++;
				rt = breaklkup(r);
			}
			if(ISG(rt, INCONST))
				continue;
		}
		if(ISGEX(rt) || r == ZWJ)
			continue;
		if(ISG(rt, SPACEMK) || ISG(lt, PREPEND))
			continue;
		if(ISG(lt, REGION) && ISG(rt, REGION)){
			if(ISG(breaklkup(rp[1]), REGION)){
				/* '\0' is not a REGION */
				return rp+1;
			}
			continue;
		}

		return rp;
	}
}

char*
utfgbreak(char *s)
{
	int lt, rt;
	Rune l, r, p;
	char *lp, *rp, *pp;

	lp = s;
	if(*lp == 0)
		return nil;

	for(;; lp = rp){
		rp = lp + chartorune(&l, lp);
		pp = rp + chartorune(&r, rp);
		if(r == 0)
			return nil;
		lt = breaklkup(l);
		rt = breaklkup(r);
		if(l == '\r' && r == '\n')
			continue;
		if(ISG(lt, CONTROL) || l == '\r' || l == '\n')
			return rp;
		if(ISG(rt, CONTROL) || r == '\r' || r == '\n')
			return rp;
		if(ISG(lt, L) && (ISG(rt, L) || ISG(rt, V) || ISG(rt, LV) || ISG(rt, LVT)))
			continue;
		if((ISG(lt, LV) || ISG(lt, V)) && (ISG(rt, V) || ISG(rt, T)))
			continue;
		if((ISG(lt, LVT) || ISG(lt, T)) && ISG(rt, T))
			continue;
		/* Out of order from the rules, GB11 is more specific than GB9... */
		if(ISG(lt, EMOJIEX)){
			while(ISGEX(rt)){
				rp = pp;
				pp = rp + chartorune(&r, rp);
				if(r == 0)
					return nil;
				rt = breaklkup(r);
			}
			chartorune(&p, pp);
			if(r == ZWJ && breaklkup(p) == EMOJIEX){
				rp = pp;
				continue;
			}
		}
		/* Same as above, InCB=Extend overlaps with general EXTEND */
		if(ISGIL(lt)){
			while(ISGIE(r, rt)){
				rp = pp;
				pp = rp + chartorune(&r, rp);
				if(r == 0)
					return nil;
				rt = breaklkup(r);
			}
			if(ISG(rt, INCONST))
				continue;
		}
		if(ISGEX(rt) || r == ZWJ)
			continue;
		if(ISG(rt, SPACEMK) || ISG(lt, PREPEND))
			continue;
		if(ISG(lt, REGION) && ISG(rt, REGION)){
			chartorune(&p, pp);
			if(ISG(breaklkup(p), REGION))
				return pp;
			continue;
		}

		return rp;
	}
}

#define AH(x) (IS(x, ALetter) || IS(x, Hebrew_Letter))
#define MNLQ(x, xt) (IS(xt, MidNumLet) || x == '\'')

static int
wbreak1to4(Rune l, int lt, Rune r, int rt)
{
	if(l == '\r' && r == '\n')
		return 0;
	if(l == '\r' || l == '\n' || l == LINETAB)
		return 1;
	if(r == '\r' || r == '\n' || r == LINETAB)
		return 1;
	if(l == ZWJ && ISG(rt, EMOJIEX))
		return 0;
	if(IS(lt, WSegSpace) && IS(rt, WSegSpace))
		return 0;
	return 2;
}

Rune*
runewbreak(Rune *s)
{
	Rune l, r, peek;
	Rune *lp, *rp, *pp;
	int lt, rt, pt;
	int w;

	lp = s;
	if(*lp == 0)
		return nil;
	for(;; lp = rp){
		l = lp[0];
		rp = lp + 1;
		if((r = rp[0]) == 0)
			return nil;
		peek = rp[1];
		lt = breaklkup(l);
		rt = breaklkup(r);
		/* A table lookup for '\0' is fine */
		pt = breaklkup(peek);
		if(!(w = wbreak1to4(l, lt, r, rt)))
			continue;
		else if(w == 1)
			return rp;
		while(IS(rt, Extend) || IS(rt, Format) || r == ZWJ){
			if(!wbreak1to4(r, rt, peek, pt))
				goto Next;
			if((r = *++rp) == 0)
				return nil;
			rt = breaklkup(r);
			peek = rp[1];
			pt = breaklkup(peek);
		}
		/* We also need to apply WB4 to our peek'd rune */
		pp = rp + 1;
		while(peek != 0 && (IS(pt, Extend) || IS(pt, Format) || peek == ZWJ)){
			peek = *(++pp);
			pt = breaklkup(peek);
		}
		if(AH(lt)){
			if(AH(rt))
				continue;
			/* If a check with peek catches, it needs to be our new LHS next go around */
			if((IS(rt, MidLetter) || MNLQ(r, rt)) && peek != 0 && AH(pt)){
				rp = pp;
				continue;
			}
			if(IS(lt, Hebrew_Letter) && r == '\'')
				continue;
			if(IS(lt, Hebrew_Letter) && r == '"' && peek != 0 && IS(pt, Hebrew_Letter)){
				rp = pp;
				continue;
			}
			if(IS(rt, Numeric))
				continue;
		}
		if(IS(lt, Numeric) && (AH(rt) || IS(rt, Numeric)))
			continue;
		if(IS(lt, Numeric) && (IS(rt, MidNum) || MNLQ(r, rt)) && peek != 0 && IS(pt, Numeric)){
			rp = pp;
			continue;
		}
		if(IS(lt, Katakana) && IS(rt, Katakana))
			continue;
		if(AH(lt) || IS(lt, Numeric) || IS(lt, Katakana) || IS(lt, ExtendNumLet))
			if(IS(rt, ExtendNumLet))
				continue;
		if(IS(lt, ExtendNumLet) && (AH(rt) || IS(rt, Numeric) || IS(rt, Katakana)))
			continue;
		if(ISG(lt, REGION) && ISG(rt, REGION)){
			if(ISG(pt, REGION))
				return pp;
			continue;
		}
		return rp;
Next:
		;
	}
}

char*
utfwbreak(char *s)
{
	Rune l, r, peek;
	char *lp, *rp, *pp;
	int lt, rt, pt;
	int w, pn;

	lp = s;
	if(*lp == 0)
		return nil;
	for(;; lp = rp){
		rp = lp + chartorune(&l, lp);
		pp = rp + chartorune(&r, rp);
		if(r == 0)
			return nil;
		lt = breaklkup(l);
		rt = breaklkup(r);
		pn = chartorune(&peek, pp);
		pt = breaklkup(peek);
		if(!(w = wbreak1to4(l, lt, r, rt)))
			continue;
		else if(w == 1)
			return rp;
		while(IS(rt, Extend) || IS(rt, Format) || r == ZWJ){
			if(!wbreak1to4(r, rt, peek, pt))
				goto Next;
			rp = pp;
			pp = rp + chartorune(&r, rp);
			if(r == 0)
				return nil;
			rt = breaklkup(r);
			pn = chartorune(&peek, pp);
			pt = breaklkup(peek);
		}
		/* We also need to apply WB4 to our peek'd rune */
		while(peek != 0 && (IS(pt, Extend) || IS(pt, Format) || peek == ZWJ)){
			pp += pn;
			pn = chartorune(&peek, pp);
			pt = breaklkup(peek);
		}
		if(AH(lt)){
			if(AH(rt))
				continue;
			/* If a check with peek catches, it needs to be our new LHS next go around */
			if((IS(rt, MidLetter) || MNLQ(r, rt)) && peek != 0 && AH(pt)){
				rp = pp;
				continue;
			}
			if(IS(lt, Hebrew_Letter) && r == '\'')
				continue;
			if(IS(lt, Hebrew_Letter) && r == '"' && peek != 0 && IS(pt, Hebrew_Letter)){
				rp = pp;
				continue;
			}
			if(IS(rt, Numeric))
				continue;
		}
		if(IS(lt, Numeric) && (AH(rt) || IS(rt, Numeric)))
			continue;
		if(IS(lt, Numeric) && (IS(rt, MidNum) || MNLQ(r, rt)) && peek != 0 && IS(pt, Numeric)){
			rp = pp;
			continue;
		}
		if(IS(lt, Katakana) && IS(rt, Katakana))
			continue;
		if(AH(lt) || IS(lt, Numeric) || IS(lt, Katakana) || IS(lt, ExtendNumLet))
			if(IS(rt, ExtendNumLet))
				continue;
		if(IS(lt, ExtendNumLet) && (AH(rt) || IS(rt, Numeric) || IS(rt, Katakana)))
			continue;
		if(ISG(lt, REGION) && ISG(rt, REGION)){
			if(ISG(pt, REGION))
				return pp;
			continue;
		}
		return rp;
Next:
		;
	}
}
