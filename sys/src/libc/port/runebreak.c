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
	EMOJIEX = 0xB0,

	ZWJ = 0x200DU,
	LINETAB = 0xB,
};

#define IS(x, y) ((x&0xf) == y)
#define ISG(x, y) ((x&0xf0) == y)

Rune*
runegbreak(Rune *s)
{
	Rune l, r;
	uchar lt, rt;
	Rune *p;

	p = s;
	if((l = *p++) == 0)
		return s;
	if((r = *p) == 0)
		return s;
	lt = breaklkup(l);
	rt = breaklkup(r);
	if(l == '\r' && r == '\n')
		goto Done;
	if(ISG(lt, CONTROL) || l == '\r' || l == '\n')
		return p;
	if(ISG(rt, CONTROL) || r == '\r' || r == '\n')
		return p;
	if(ISG(lt, L) && (ISG(rt, L) || ISG(rt, V) || ISG(rt, LV) || ISG(rt, LVT)))
		goto Done;
	if((ISG(lt, LV) || ISG(lt, V)) && (ISG(rt, V) || ISG(rt, T)))
		goto Done;
	if((ISG(lt, LVT) || ISG(lt, T)) && (ISG(rt, T) || ISG(rt, T)))
		goto Done;
	if(ISG(rt, SPACEMK) || ISG(lt, PREPEND))
		goto Done;
	if(ISG(lt, EMOJIEX) && (ISG(rt, EXTEND) || r == ZWJ)){
		while(ISG(rt, EXTEND)){
			p++;
			if((r = *p) == 0)
				return s;
			rt = breaklkup(r);
		}
		if(r != ZWJ)
			return p;
		p++;
		if((r = *p) == 0)
			return s;
		rt = breaklkup(r);
		if(ISG(rt, EMOJIEX))
			goto Done;
		return p;
	}
	if(ISG(rt, EXTEND) || r == ZWJ)
		goto Done;
	if(ISG(lt, REGION) && ISG(rt, REGION))
		goto Done;

	return p;

Done:
	if(p[1] == 0)
		return s;
	return p + 1;
}

char*
utfgbreak(char *s)
{
	Rune l, r;
	uchar lt, rt;
	char *p;

	p = s;
	p += chartorune(&l, p);
	if(l == 0)
		return s;
	chartorune(&r, p);
	if(r == 0)
		return s;
	lt = breaklkup(l);
	rt = breaklkup(r);
	if(l == '\r' && r == '\n')
		goto Done;
	if(ISG(lt, CONTROL) || l == '\r' || l == '\n')
		return p;
	if(ISG(rt, CONTROL) || r == '\r' || r == '\n')
		return p;
	if(ISG(lt, L) && (ISG(rt, L) || ISG(rt, V) || ISG(rt, LV) || ISG(rt, LVT)))
		goto Done;
	if((ISG(lt, LV) || ISG(lt, V)) && (ISG(rt, V) || ISG(rt, T)))
		goto Done;
	if((ISG(lt, LVT) || ISG(lt, T)) && (ISG(rt, T) || ISG(rt, T)))
		goto Done;
	if(ISG(rt, SPACEMK) || ISG(lt, PREPEND))
		goto Done;
	if(ISG(lt, EMOJIEX) && (ISG(rt, EXTEND) || r == ZWJ)){
		while(ISG(rt, EXTEND)){
			p += chartorune(&r, p);
			chartorune(&r, p);
			if(r == 0)
				return s;
			rt = breaklkup(r);
		}
		if(r != ZWJ)
			return p;

		p += chartorune(&r, p);
		chartorune(&r, p);
		if(r == 0)
			return s;
		rt = breaklkup(r);
		if(ISG(rt, EMOJIEX))
			goto Done;
		return p;
	}
	if(ISG(rt, EXTEND) || r == ZWJ)
		goto Done;
	if(ISG(lt, REGION) && ISG(rt, REGION))
		goto Done;

	return p;

Done:
	p += chartorune(&r, p);
	chartorune(&r, p);
	if(r == 0)
		return s;
	return p;
}

#define AH(x) (IS(x, ALetter) || IS(x, Hebrew_Letter))
#define MNLQ(x) (IS(x, MidNumLet) || x == '\'')

Rune*
runewbreak(Rune *s)
{
	Rune l, r;
	uchar lt, rt;
	Rune *p;

	p = s;
	if((l = *p++) == 0)
		return s;
	if((r = *p) == 0)
		return s;
	lt = breaklkup(l);
	rt = breaklkup(r);
	if(l == '\r' && r == '\n')
		goto Done;
	if(l == '\r' || l == '\n' || l == LINETAB)
		return p;
	if(r == '\r' || r == '\n' || l == LINETAB)
		return p;
	if(IS(lt, WSegSpace) && IS(rt, WSegSpace))
		goto Done;
	if(IS(rt, Format) || IS(rt, Extend))
		goto Done;
	if(AH(lt)){
		if(AH(rt))
			goto Done;
		if((IS(rt, MidLetter) || MNLQ(rt)) && p[1] != 0 && AH(breaklkup(p[1])))
			goto Done;
		if(IS(lt, Hebrew_Letter) && r == '\'')
			goto Done;
		if(IS(lt, Hebrew_Letter) && r == '"' && p[1] != 0 && IS(breaklkup(p[1]), Hebrew_Letter))
			goto Done;
		if(IS(rt, Numeric))
			goto Done;
	}
	if(IS(lt, Numeric) && (AH(rt) || IS(rt, Numeric)))
		goto Done;
	if(IS(lt, Numeric) && (IS(rt, MidNum) || MNLQ(rt)) && p[1] != 0 && IS(breaklkup(p[1]), Numeric))
		goto Done;
	if(IS(lt, Katakana) && IS(rt, Katakana))
		goto Done;
	if(AH(lt) || IS(lt, Numeric) || IS(lt, Katakana) || IS(lt, ExtendNumLet))
		if(IS(rt, ExtendNumLet))
			goto Done;
	if(IS(lt, ExtendNumLet) && (AH(rt) || IS(rt, Numeric) || IS(rt, Katakana)))
		goto Done;
	if(ISG(lt, REGION)){
		if(ISG(rt, REGION))
			goto Done;
		if(r != ZWJ)
			return p;
		p++;
		if((r = *p) == 0)
			return s;
		rt = breaklkup(r);
		if(ISG(rt, REGION))
			goto Done;
	}

	return p;

Done:
	if(p[1] == 0)
		return s;
	return p + 1;
}

char*
utfwbreak(char *s)
{
	Rune l, r;
	Rune peek;
	uchar lt, rt;
	char *p;

	p = s;
	p += chartorune(&l, p);
	if(l == 0)
		return s;
	chartorune(&peek, p+chartorune(&r, p));
	if(r == 0)
		return s;
	lt = breaklkup(l);
	rt = breaklkup(r);
	if(l == '\r' && r == '\n')
		goto Done;
	if(l == '\r' || l == '\n' || l == LINETAB)
		return p;
	if(r == '\r' || r == '\n' || l == LINETAB)
		return p;
	if(IS(lt, WSegSpace) && IS(rt, WSegSpace))
		goto Done;
	if(IS(rt, Format) || IS(rt, Extend))
		goto Done;
	if(AH(lt)){
		if(AH(rt))
			goto Done;
		if(IS(rt, MidLetter) || MNLQ(rt))
		if(peek != 0 && AH(breaklkup(peek)))
			goto Done;

		if(IS(lt, Hebrew_Letter) && r == '\'')
			goto Done;

		if(IS(lt, Hebrew_Letter) && r == '"')
		if(peek != 0 && IS(breaklkup(peek), Hebrew_Letter))
			goto Done;

		if(IS(rt, Numeric))
			goto Done;
	}
	if(IS(lt, Numeric) && (AH(rt) || IS(rt, Numeric)))
		goto Done;
	if(IS(lt, Numeric) && (IS(rt, MidNum) || MNLQ(rt)) && peek != 0 && IS(breaklkup(peek), Numeric))
		goto Done;
	if(IS(lt, Katakana) && IS(rt, Katakana))
		goto Done;
	if(AH(lt) || IS(lt, Numeric) || IS(lt, Katakana) || IS(lt, ExtendNumLet))
		if(IS(rt, ExtendNumLet))
			goto Done;
	if(IS(lt, ExtendNumLet) && (AH(rt) || IS(rt, Numeric) || IS(rt, Katakana)))
		goto Done;
	if(ISG(lt, REGION)){
		if(ISG(rt, REGION))
			goto Done;
		if(r != ZWJ)
			return p;
		p += chartorune(&r, p);
		chartorune(&r, p);
		if(r == 0)
			return s;
		rt = breaklkup(r);
		if(ISG(rt, REGION))
			goto Done;
	}

	return p;

Done:
	p += chartorune(&r, p);
	chartorune(&r, p);
	if(r == 0)
		return s;
	return p;
}
