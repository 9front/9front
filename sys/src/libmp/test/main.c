#include <u.h>
#include <libc.h>
#include <mp.h>
#include "dat.h"
#include "fns.h"

static int
mpdetfmt(Fmt *f)
{
	mpint *a;
	int i, j;
	
	a = va_arg(f->args, mpint *);
	fmtprint(f, "(sign=%d,top=%d,size=%d,", a->sign, a->top, a->size);
	for(i=0;i<a->top;){
		fmtprint(f, "%ullx", (uvlong)a->p[i]);
		if(++i == a->top) break;
		fmtrune(f, ',');
		for(j = i+1; j < a->top;  j++)
			if(a->p[i] != a->p[j])
				goto next;
		fmtprint(f, "...");
		break;
	next:;
	}
	fmtrune(f, '|');
	for(i=a->top;i<a->size;){
		fmtprint(f, "%ullx", (uvlong)a->p[i]);
		if(++i == a->size) break;
		fmtrune(f, ',');
		for(j = i+1; j < a->top;  j++)
			if(a->p[i] != a->p[j])
				goto next2;
		fmtprint(f, "...");
		break;
	next2:;
	}
	fmtrune(f, ')');
	return 0;
}

void
main()
{
	extern int ldfmt(Fmt *);

	fmtinstall('B', mpfmt);
	fmtinstall(L'β', mpdetfmt);
	fmtinstall('L', ldfmt);
	
	convtests();
	tests();
	exits(nil);
}
