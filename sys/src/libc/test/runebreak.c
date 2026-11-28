#include <u.h>
#include <libc.h>
#include <bio.h>

static int
estrtoul(char *s)
{
	char *epr;
	Rune code;

	code = strtoul(s, &epr, 16);
	if(s == epr)
		sysfatal("bad code point hex string");
	return code;
}

#pragma	   varargck    type  "V"   Rune*
static int
Vrunefmt(Fmt *f)
{
	Rune *s;
	int n;

	s = va_arg(f->args, Rune*);
	if(s == nil)
		return fmtprint(f, "<nil>");
	if(*s == '\0')
		return fmtprint(f, "(\0)");
	n = fmtprint(f, "%S(", s);
	for(; *s != 0; s++)
		n += fmtprint(f, "%X%s", *s, s[1] == 0 ? "" : " ");
	n += fmtprint(f, ")");
	return n;
}

#pragma	   varargck    type  "v"   char*
static int
vrunefmt(Fmt *f)
{
	Rune r;
	char *s;
	int n;

	s = va_arg(f->args, char*);
	if(s == nil)
		return fmtprint(f, "<nil>");
	if(*s == '\0')
		return fmtprint(f, "(\0)");
	n = fmtprint(f, "%s(", s);
	while(*s != 0){
		s += chartorune(&r, s);
		n += fmtprint(f, "%X%s", r, *s == 0 ? "" : " ");
	}
	n += fmtprint(f, ")");
	return n;
}

static Rune*
check(Rune *r, char *line, Rune* (*fn)(Rune*), char* (*fn2)(char*))
{
	Rune *r2, *tmp;
	char *p, *p2;

	p = smprint("%S", r);
	r2 = fn(r);
	p2 = fn2(p);

	if(r2 == nil){
		if(p2 != nil)
			sysfatal("utf broke when we shouldn't %v||%s\n", p2, line);
		return r2;
	}

	tmp = runesmprint("%.*s", (int)(p2-p), p);
	if(memcmp(r, tmp, r2-r) != 0)
		sysfatal("utf mismstach %S||%S (%d)\n", r, tmp, (int)(p2-p));
	
	free(p);
	free(tmp);
	return r2;
}

static void
run(char *file, Rune* (*fn)(Rune*), char* (*fn2)(char*))
{
	Biobuf *b;
	char *p, *dot;
	char *pieces[64];
	int i, n;
	Rune stack[16];
	Rune *exp[16];
	int nstack, nexp;
	Rune r, *rp;
	char *line;

	b = Bopen(file, OREAD);
	if(b == nil)
		sysfatal("could not load composition exclusions: %r");

	for(;(p = Brdline(b, '\n')) != nil; free(line)){
		p[Blinelen(b)-1] = 0;
		line = strdup(p);
		if(p[0] == 0 || p[0] == '#')
			continue;
		if((dot = strchr(p, '#')) != nil)
			*dot = 0;
		n = getfields(p, pieces, nelem(pieces), 0, " ");
		nexp = nstack = 0;
		for(i = 1; i < n-1; i++){
			chartorune(&r, pieces[i]);
			switch(r){
			default:
				r = estrtoul(pieces[i]);
				/*
				 * Unicode v17.0 onwards wants us to handle
				 * strings with nulls in it, for our interface,
				 * and C in general, this doesn't make much sense.
				 * This could be fixed with a change to the exposed interface,
				 * but we're not convinced of the utility of doing so.
				 */
				if(r == 0x0000)
					goto Break;
				stack[nstack++] = r;
				stack[nstack] = 0;
				break;
			case L'×':
				break;
			case L'÷':
				exp[nexp++] = stack + nstack;
				break;
			}
		}
		if(nexp == 0){
			if((rp = check(stack, line, fn, fn2)) != nil)
				sysfatal("%s\n\tbroke when we shouldn't index(%d) (rp=%V)\n", line, (int)(rp-stack), rp);
			continue;
		}

		rp = stack;
		for(i = 0; i < nexp; i++){
			if((rp = check(rp, line, fn, fn2)) != exp[i])
				sysfatal("%s\n\tbreak mismatch Got(%V) Exp(%V)\n", line, rp, exp[i]);
		}

	Break:
		;
	}
}

void
main(int, char)
{
	fmtinstall('V', Vrunefmt);
	fmtinstall('v', vrunefmt);
	run("/lib/ucd/GraphemeBreakTest.txt", runegbreak, utfgbreak);
	run("/lib/ucd/WordBreakTest.txt", runewbreak, utfwbreak);
	exits(nil);
}
