#include <u.h>
#include <libc.h>

#include "runenormdata"

//Unicode Standard: Section 3.12 Conjoining Jamo Behavior
enum {
	SBase = 0xAC00,
	LBase = 0x1100,
	VBase = 0x1161,
	TBase = 0x11A7,

	LCount = 19,
	VCount = 21,
	TCount = 28,
	NCount = VCount * TCount,
	SCount = LCount * NCount,

	LLast = LBase + LCount - 1,
	SLast = SBase + SCount - 1,
	VLast = VBase + VCount - 1,
	TLast = TBase + TCount - 1,
};

static void
_runedecomp(Rune dst[2], Rune c)
{
	uint x;

	if(c >= SBase && c <= SLast){
		c -= SBase;
		x = c % TCount;
		if(x){
			dst[0] = SBase + ((c / TCount) * TCount);
			dst[1] = TBase + x;
			return;
		}
		dst[0] = LBase + (c / NCount);
		dst[1] = VBase + ((c % NCount) / TCount);
		return;
	}
	x = decomplkup(c);
	if((x & 0xFFFF) != 0){
		dst[0] = x>>16;
		dst[1] = x & 0xFFFF;
		return;
	}
	x >>= 16;
	if(x >= 0xEEEE && x <0xF8FF){
		memmove(dst, _decompexceptions[x - 0xEEEE], sizeof(Rune)*2);
		return;
	}
	dst[0] = x;
	dst[1] = 0;
}

static Rune
_runerecomp(Rune r[2])
{
	uint x, y, *p, next;

	if(r[0] >= LBase && r[0] <= LLast){
		if(r[1] < VBase || r[1] > VLast)
			return 0;
		x = (r[0] - LBase) * NCount + (r[1] - VBase) * TCount;
		return SBase + x;
	}
	if(r[0] >= SBase && r[0] <= SLast && (r[0] - SBase) % TCount == 0){
		if(r[1] > TBase && r[1] <= TLast)
			return r[0] + (r[1] - TBase);
		return 0;
	}
	if(r[0] > 0xFFFF || r[1] > 0xFFFF){
		for(x = 0; x < nelem(_recompexceptions); x++)
			if(r[0] == _recompexceptions[x][1] && r[1] == _recompexceptions[x][2])
				return  _recompexceptions[x][0];
		return 0;
	}
	y = x = r[0]<<16 | r[1];
	x ^= x >> 16;
	x *= 0x21f0aaad;
	x ^= x >> 15;
	x *= 0xd35a2d97;
	x ^= x >> 15;
	p = _recompdata + (x%512)*2;
	while(p[0] != y){
		next = p[1]>>16;
		if(!next)
			return 0;
		p = _recompcoll + (next-1)*2;
	}
	return p[1] & 0xFFFF;
}

static void
runecccsort(Rune *a, int len)
{
	Rune r;
	int i;
	int fail;

	do {
		fail = 0;
		for(i = 0; i < len - 1; i++){
			if(ccclkup(a[i]) > ccclkup(a[i+1]) > 0){
				r = a[i];
				a[i] = a[i+1];
				a[i + 1] = r;
				fail = 1;
			}
		}
	} while(fail);
}

char*
fullutfnorm(char *s, int n)
{
	Rune r, peek;
	char *p, *p2;

	p = s;
	if(fullrune(p, n) == 0)
		return s;

	p += chartorune(&r, p);
	n -= (p - s);

	if((r >= LBase && r <= LLast) || (r >= SBase && r <= SLast)){
		do {
			if(fullrune(p, n) == 0)
				return s;
			p2 = p + chartorune(&peek, p);
			n -= (p2 - p);
			p = p2;
		} while(n > 0 && (peek >= VBase && peek <= VLast) || (peek > TBase && peek <= TLast));
		if(n <= 0)
			return s;
		return p;
	}

	do {
		if(fullrune(p, n) == 0)
			return s;
		p2 = p + chartorune(&peek, p);
		n -= (p2 - p);
		p = p2;
		if(ccclkup(peek) == 0)
			return p;
	} while(n > 0);

	return s;
}

Rune*
fullrunenorm(Rune *r, int n)
{
	Rune *e, *p;

	p = r;
	e = p + n;

	if((*p >= LBase && *p <= LLast) || (*p >= SBase && *p <= SLast)){
		p++;
		while(p < e && (*p >= VBase && *p <= VLast) || (*p > TBase && *p <= TLast))
			p++;

		if(p >= e)
			return r;
		return p;
	}

	for(; p < e && p + 1 < e; p++)
		if(ccclkup(p[1]) == 0)
			return p + 1;

	return r;
}

static int
runenorm(Rune *dst, Rune *src, char *sdst, char *ssrc, int max, int compose)
{
	Rune c, r[2], _stack[32];
	Rune *p, *stack, *sp, *tp;
	char *strp, *strstop;
	Rune *rp, *rrp;
	Rune *stop;
	Rune peek;
	int w, w2, size;
	int mode;

	if(src){
		mode = 1;
		p = src;
		stop = dst + (max - 1);
		strp = "";
		strstop = nil;
	} else {
		mode = 0;
		p = L"";
		stop = nil;
		strp = ssrc;
		strstop = sdst + (max - 1);
	}

	stack = _stack + nelem(_stack)/2;
	size = 0;
	w = w2 = 0;
	while(*strp || *p){
		if(mode)
			c = *p;
		else
			w = chartorune(&c, strp);

		sp = stack - 1;
		tp = stack;
		_runedecomp(r, c);
		while(r[0] != 0){
			c = r[0];
			if(r[1] != 0){
				*sp-- = r[1];
				if(sp == _stack)
					break;
			}
			_runedecomp(r, c);
		}

		*sp = c;
		if(mode)
			peek = p[1];
		else
			w2 = chartorune(&peek, strp+w);

		if((*sp >= LBase && *sp <= LLast) || (*sp >= SBase && *sp <= SLast)){
			while(peek != 0 && (peek >= VBase && peek <= VLast) || (peek > TBase && peek <= TLast)){
				*tp++ = peek;
				if(mode){
					p++;
					peek = p[1];
				} else {
					strp += w;
					w = w2;
					w2 = chartorune(&peek, strp+w);
				}
				if(tp == _stack + nelem(_stack))
					break;
			}
		}
		while(peek != 0 && ccclkup(peek) != 0){
			_runedecomp(r, peek);
			if(r[1] != 0){
				if(tp+1 >= _stack + nelem(_stack))
					break;
				*tp++ = r[0];
				*tp++ = r[1];
			} else if(r[0] != 0)
				*tp++ = r[0];
			else
				*tp++ = peek;

			if(mode){
				p++;
				peek = p[1];
			} else {
				strp += w;
				w = w2;
				w2 = chartorune(&peek, strp+w);
			}
			if(tp == _stack + nelem(_stack))
				break;
		}
		runecccsort(sp, tp - sp);

		if(compose && ccclkup(*sp) == 0){
			for(rp = sp + 1; rp < tp; rp++){
				r[0] = *sp;
				r[1] = *rp;
				c = _runerecomp(r);
				if(c != 0){
					*sp = c;
					for(rrp = rp; rrp > sp; rrp--)
						*rrp = rrp[-1];
					sp++;
				} else while(rp + 1 < tp && ccclkup(*rp) == ccclkup(*(rp+1)))
					rp++;
			}
		}

		for(; sp < tp; sp++){
			if(mode){
				if(dst < stop)
					*dst++ = *sp;
				size++;
			} else {
				w2 = runelen(*sp);
				if(sdst+w2 < strstop)
					sdst += runetochar(sdst, sp);
				size += w2;
			}
		}
		if(mode)
			p++;
		else
			strp += w;
	}
	if(mode)
		*dst = 0;
	else
		*sdst = 0;
	return size;
}

int
runecomp(Rune *dst, Rune *src, int max)
{
	return runenorm(dst, src, nil, nil, max, 1);
}

int
runedecomp(Rune *dst, Rune *src, int max)
{
	return runenorm(dst, src, nil, nil, max, 0);
}

int
utfcomp(char *dst, char *src, int max)
{
	return runenorm(nil, nil, dst, src, max, 1);
}

int
utfdecomp(char *dst, char *src, int max)
{
	return runenorm(nil, nil, dst, src, max, 0);	
}
