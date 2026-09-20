#include <u.h>
#include <libc.h>
#include <draw.h>

enum
{
	Max = 100
};

Point
string(Image *dst, Point pt, Image *src, Point sp, Font *f, char *s)
{
	return _string(dst, pt, src, sp, f, s, nil, 1<<24, dst->clipr, nil, ZP, SoverD);
}

Point
stringop(Image *dst, Point pt, Image *src, Point sp, Font *f, char *s, Drawop op)
{
	return _string(dst, pt, src, sp, f, s, nil, 1<<24, dst->clipr, nil, ZP, op);
}

Point
stringn(Image *dst, Point pt, Image *src, Point sp, Font *f, char *s, int len)
{
	return _string(dst, pt, src, sp, f, s, nil, len, dst->clipr, nil, ZP, SoverD);
}

Point
stringnop(Image *dst, Point pt, Image *src, Point sp, Font *f, char *s, int len, Drawop op)
{
	return _string(dst, pt, src, sp, f, s, nil, len, dst->clipr, nil, ZP, op);
}

Point
runestring(Image *dst, Point pt, Image *src, Point sp, Font *f, Rune *r)
{
	return _string(dst, pt, src, sp, f, nil, r, 1<<24, dst->clipr, nil, ZP, SoverD);
}

Point
runestringop(Image *dst, Point pt, Image *src, Point sp, Font *f, Rune *r, Drawop op)
{
	return _string(dst, pt, src, sp, f, nil, r, 1<<24, dst->clipr, nil, ZP, op);
}

Point
runestringn(Image *dst, Point pt, Image *src, Point sp, Font *f, Rune *r, int len)
{
	return _string(dst, pt, src, sp, f, nil, r, len, dst->clipr, nil, ZP, SoverD);
}

Point
runestringnop(Image *dst, Point pt, Image *src, Point sp, Font *f, Rune *r, int len, Drawop op)
{
	return _string(dst, pt, src, sp, f, nil, r, len, dst->clipr, nil, ZP, op);
}

Point
_string(Image *dst, Point pt, Image *src, Point sp, Font *f, char *s, Rune *r, int len, Rectangle clipr, Image *bg, Point bgp, Drawop op)
{
	int n, wid, max, try;
	ushort cbuf[Max], *c, *ec;
	char *subfontname;
	char **sptr;
	Rune **rptr, rune;
	Subfont *sf;

	if(s == nil){
		s = "";
		sptr = nil;
	}else
		sptr = &s;
	if(r == nil){
		r = (Rune*) L"";
		rptr = nil;
	}else
		rptr = &r;
	subfontname = nil;
	sf = nil;
	try = 0;
	while((*s || *r) && len > 0){
		max = Max;
		if(len < max)
			max = len;
		if(subfontname){
			freesubfont(sf);
			if((sf=_getsubfont(f->display, subfontname)) == nil){
				if(f->display->defaultfont == nil || f->display->defaultfont == f)
					break;
				f = f->display->defaultfont;
			}
		}
		if((n = cachechars(f, sptr, rptr, cbuf, max, &wid, &subfontname)) <= 0){
			if(n == 0){
				if(++try > 10)
					break;
				continue;
			}
			if(*r)
				r++;
			else
				s += chartorune(&rune, s);
			len--;
			continue;
		}
		try = 0;

		/* encode in the right order so we can memmove */
		ec = &cbuf[n];
		for(c = cbuf; c < ec; c++)
			BPSHORT((uchar*)c, *c);

		_lockdisplay(dst->display);
		if(bg){
			if(drawcmd(dst->display, "OblllllRPslP<", op,
			    'x', dst->id, src->id, f->cacheimage->id, pt.x, pt.y+f->ascent,
			    &clipr, &sp, n, bg->id, &bgp, 2*n, cbuf) < 0){
				_unlockdisplay(dst->display);
				fprint(2, "stringbg: %r\n");
				break;
			}
		}else{
			if(drawcmd(dst->display, "OblllllRPs<", op,
			    's', dst->id, src->id, f->cacheimage->id, pt.x, pt.y+f->ascent,
			    &clipr, &sp, n, 2*n, cbuf) < 0){
				_unlockdisplay(dst->display);
				fprint(2, "string: %r\n");
				break;
			}
		}
		_unlockdisplay(dst->display);

		pt.x += wid;
		bgp.x += wid;
		agefont(f);
		len -= n;
	}
	freesubfont(sf);
	return pt;
}
