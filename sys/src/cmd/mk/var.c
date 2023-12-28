#include	"mk.h"

Word*
getvar(char *name)
{
	Symtab *sym = symlook(name, S_VAR, 0);
	if(sym)
		return sym->u.ptr;
	return 0;
}

void
setvar(char *name, Word *value)
{
	Symtab *sym = symlook(name, S_VAR, 1);
	delword(sym->u.ptr);
	sym->u.ptr = value;
}

static void
print1(Symtab *s)
{
	Word *w;

	Bprint(&bout, "\t%s=", s->name);
	for (w = s->u.ptr; w; w = w->next)
		Bprint(&bout, "'%s'", w->s);
	Bprint(&bout, "\n");
}

void
dumpv(char *s)
{
	Bprint(&bout, "%s:\n", s);
	symtraverse(S_VAR, print1);
}

char *
shname(char *a)
{
	Rune r;
	int n;

	while (*a) {
		n = chartorune(&r, a);
		if (!WORDCHR(r))
			break;
		a += n;
	}
	return a;
}
