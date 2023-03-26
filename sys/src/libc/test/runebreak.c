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

static Rune*
check(Rune *r, Rune* (*fn)(Rune*), char* (*fn2)(char*))
{
	Rune *r2, *tmp;
	char *p, *p2;

	p = smprint("%S", r);
	r2 = fn(r);
	p2 = fn2(p);

	tmp = runesmprint("%.*s", (int)(p2-p), p);
	if(memcmp(r, tmp, r2-r) != 0)
		print("utf mismstach\n");
	
	free(p);
	free(tmp);
	return r2;
}

static void
run(char *file, Rune* (*fn)(Rune*), char* (*fn2)(char*))
{
	Biobuf *b;
	char *p, *dot;
	char *pieces[16];
	int i, j, n;
	Rune stack[16], ops[16];
	int nstack, nops;
	Rune r, *rp, *rp2;
	char *line;

	b = Bopen(file, OREAD);
	if(b == nil)
		sysfatal("could not load composition exclusions: %r");

	for(;(p = Brdline(b, '\n')) != nil; free(line)){
		p[Blinelen(b)-1] = 0;
		line = strdup(p);
		if(p[0] == 0 || p[0] == '#')
			continue;
		if((dot = strstr(p, "#")) != nil)
			*dot = 0;
		n = getfields(p, pieces, nelem(pieces), 0, " ");
		nstack = nops = 0;
		for(i = 0; i < n; i++){
			chartorune(&r, pieces[i]);
			if(r != L'÷' && r != L'×'){
				r = estrtoul(pieces[i]);
				stack[nstack++] = r;
				stack[nstack] = 0;
			} else {
				ops[nops++] = r;
				ops[nops] = 0;
			}
		}

		rp = stack;
		for(i = 1; i < nops-1;){
			rp2 = check(rp, fn, fn2);
			switch(ops[i]){
			case L'÷':
				if(rp2 != rp+1){
					print("break fail %X %X || %s\n", rp[0], rp[1], line);
					goto Break;
				}
				rp++;
				i++;
				break;
			case L'×':
				if(rp2 - rp == 0){
					for(j = i; j < nops - 1; j++)
						if(ops[j] !=  L'×')
							print("skipped %d %d %s\n", i, nops, line);
					goto Break;
				}
				for(; rp < (rp2-1); rp++, i++){
					if(ops[i] != L'×')
						print("skipped %d %d %s\n", i, nops, line);
				}
				rp = rp2;
				i++;
				break;
			}
		}
Break:
		;
	}
}

void
main(int, char)
{
	run("/lib/ucd/GraphemeBreakTest.txt", runegbreak, utfgbreak);
	run("/lib/ucd/WordBreakTest.txt", runewbreak, utfwbreak);
	exits(nil);
}
