#include <u.h>
#include <libc.h>
#include <mp.h>
#include <sat.h>
#include "dat.h"
#include "fns.h"

extern SATSolve *sat;
extern int *assertvar, nassertvar;

int
printval(Symbol *s, Fmt *f)
{
	int i;
	
	if(s->type != SYMBITS) return 0;
	fmtprint(f, "%s = ", s->name);
	for(i = s->size - 1; i >= 0; i--)
		switch(satval(sat, s->vars[i])){
		case 1: fmtrune(f, '1'); break;
		case 0: fmtrune(f, '0'); break;
		case -1: fmtrune(f, '?'); break;
		default: abort();
		}
	fmtprint(f, "\n");
	return 0;
}

void
debugsat(void)
{
	int i, j, rc;
	int *t;
	int ta;
	Fmt f;
	char buf[256];
	
	ta = 0;
	t = nil;
	fmtfdinit(&f, 1, buf, 256);
	for(i = 0;;){
		rc = satget(sat, i, t, ta);
		if(rc < 0) break;
		if(rc > ta){
			ta = rc;
			t = realloc(t, ta * sizeof(int));
			continue;
		}
		i++;
		fmtprint(&f, "%d: ", i);
		for(j = 0; j < rc; j++)
			fmtprint(&f, "%s%d", j==0?"":" ∨ ", t[j]);
		fmtprint(&f, "\n");
	}
	free(t);
	fmtfdflush(&f);
}

void
tabheader(Fmt *f)
{
	Symbol *s;
	int l, first;
	
	first = 0;
	for(s = syms; s != nil; s = s->next){
		if(s->type != SYMBITS) continue;
		l = strlen(s->name);
		if(s->size > l) l = s->size;
		fmtprint(f, "%s%*s", first++ != 0 ? " " : "", l, s->name);
	}
	fmtrune(f, '\n');
}

void
tabrow(Fmt *f)
{
	Symbol *s;
	int i, l, first;
	
	first = 0;
	for(s = syms; s != nil; s = s->next){
		if(s->type != SYMBITS) continue;
		if(first++ != 0) fmtrune(f, ' ');
		l = strlen(s->name);
		if(s->size > l) l = s->size;
		for(i = l - 1; i > s->size - 1; i--)
			fmtrune(f, ' ');
		for(; i >= 0; i--)
			switch(satval(sat, s->vars[i])){
			case 1: fmtrune(f, '1'); break;
			case 0: fmtrune(f, '0'); break;
			case -1: fmtrune(f, '?'); break;
			default: abort();
			}
	}
	fmtrune(f, '\n');
}

void
go(int mflag)
{
	Fmt f;
	char buf[256];
	Symbol *s;

	if(nassertvar == 0)
		sysfatal("left as an exercise to the reader");
	satadd1(sat, assertvar, nassertvar);
//	debugsat();
	if(mflag){
		fmtfdinit(&f, 1, buf, sizeof(buf));
		tabheader(&f);
		fmtfdflush(&f);
		while(satmore(sat) > 0){
			tabrow(&f);
			fmtfdflush(&f);
		}
	}else{
		if(satsolve(sat) == 0)
			print("Proved.\n");
		else{
			fmtfdinit(&f, 1, buf, sizeof(buf));
			for(s = syms; s != nil; s = s->next)
				printval(s, &f);
			fmtfdflush(&f);
		}
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [ -m ] [ file ]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	typedef void init(void);
	init miscinit, cvtinit, parsinit;
	static int mflag;
	
	ARGBEGIN{
	case 'm': mflag++; break;
	default: usage();
	}ARGEND;
	
	if(argc > 1) usage();

	quotefmtinstall();
	fmtinstall('B', mpfmt);
	miscinit();
	cvtinit();
	parsinit();
	parse(argc > 0 ? argv[0] : nil);
	go(mflag);
}
