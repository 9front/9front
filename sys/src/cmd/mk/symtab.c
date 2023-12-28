#include	"mk.h"

#define	NHASH	4099
#define	HASHMUL	79L	/* this is a good value */
static Symtab *hash[NHASH];

Symtab *
symlook(char *sym, int space, int install)
{
	long h;
	char *p;
	Symtab *s;

	for(p = sym, h = space; *p; h += *p++)
		h *= HASHMUL;
	if(h < 0)
		h = ~h;
	h %= NHASH;
	for(s = hash[h]; s; s = s->next)
		if(s->space == space && strcmp(s->name, sym) == 0)
			return s;
	if(install == 0)
		return 0;
	s = (Symtab *)Malloc(sizeof(Symtab) + (++p - sym));
	s->space = space;
	s->u.ptr = 0;
	memcpy(s->name, sym, p - sym);
	s->next = hash[h];
	hash[h] = s;
	return s;
}

void
symtraverse(int space, void (*fn)(Symtab*))
{
	Symtab **s, *ss;

	for(s = hash; s < &hash[NHASH]; s++)
		for(ss = *s; ss; ss = ss->next)
			if(ss->space == space)
				(*fn)(ss);
}
