#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include "ndbhf.h"

static int
needquote(char *s)
{
	int c;

	while((c = *s++) != '\0'){
		if(ISWHITE(c) || c == '#')
			return 1;
	}
	return 0;
}

int
ndbvalfmt(Fmt *f)
{
	char *s = va_arg(f->args, char*);
	if(s == nil)
		s = "";
	if(needquote(s))
		return fmtprint(f, "\"%s\"", s);
	return fmtstrcpy(f, s);
}
