#include <u.h>
#include <libc.h>
#include <draw.h>

int
_stringnwidth(Font *f, char *s, Rune *r, int len)
{
	int wid, twid, n, max, try;
	enum { Max = 64 };
	ushort cbuf[Max];
	Rune rune, **rptr;
	char *subfontname, **sptr;
	Subfont *sf;

	if(s == nil){
		s = "";
		sptr = nil;
	}else
		sptr = &s;
	if(r == nil){
		r = L"";
		rptr = nil;
	}else
		rptr = &r;
	subfontname = nil;
	sf = nil;
	twid = 0;
	try = 0;
	while((*s || *r) && len > 0){
		max = Max;
		if(len < max)
			max = len;
		if(subfontname){
			freesubfont(sf);
			if((sf=_getsubfont(f->display, subfontname)) == nil){
				if(f->display == nil || f->display->defaultfont == nil || f->display->defaultfont == f)
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

		agefont(f);
		twid += wid;
		len -= n;
	}
	freesubfont(sf);
	return twid;
}

int
stringnwidth(Font *f, char *s, int len)
{
	return _stringnwidth(f, s, nil, len);
}

int
stringwidth(Font *f, char *s)
{
	return _stringnwidth(f, s, nil, 1<<24);
}

Point
stringsize(Font *f, char *s)
{
	return Pt(_stringnwidth(f, s, nil, 1<<24), f->height);
}

int
runestringnwidth(Font *f, Rune *r, int len)
{
	return _stringnwidth(f, nil, r, len);
}

int
runestringwidth(Font *f, Rune *r)
{
	return _stringnwidth(f, nil, r, 1<<24);
}

Point
runestringsize(Font *f, Rune *r)
{
	return Pt(_stringnwidth(f, nil, r, 1<<24), f->height);
}
