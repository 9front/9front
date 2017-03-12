#include "common.h"
#include <libsec.h>
#include "dat.h"

/* all the data that's fit to cache */

typedef struct{
	char	*s;
	int	l;
	ulong	ref;
}Refs;

Refs	*rtab;
int	nrtab;
int	nralloc;

int
newrefs(char *s)
{
	int l, i;
	Refs *r;

	l = strlen(s);
	for(i = 0; i < nrtab; i++){
		r = rtab + i;
		if(r->ref == 0)
			goto enter;
		if(l == r->l && strcmp(r->s, s) == 0){
			r->ref++;
			return i;
		}
	}
	if(nrtab == nralloc)
		rtab = erealloc(rtab, sizeof *rtab*(nralloc += 50));
	nrtab = i + 1;
enter:
	r = rtab + i;
	r->s = strdup(s);
	r->l = l;
	r->ref = 1;
	return i;
}

void
delrefs(int i)
{
	Refs *r;

	r = rtab + i;
	if(--r->ref > 0)
		return;
	free(r->s);
	memset(r, 0, sizeof *r);
}

void
refsinit(void)
{
	newrefs("");
}

static char *sep = "--------\n";

int
prrefs(Biobuf *b)
{
	int i, n;

	n = 0;
	for(i = 1; i < nrtab; i++){
		if(rtab[i].ref == 0)
			continue;
		Bprint(b, "%s ", rtab[i].s);
		if(n++%8 == 7)
			Bprint(b, "\n");
	}
	if(n%8 != 7)
		Bprint(b, "\n");
	Bprint(b, sep);
	return 0;
}

int
rdrefs(Biobuf *b)
{
	char *f[10], *s;
	int i, n;

	while(s = Brdstr(b, '\n', 1)){
		if(strcmp(s, sep) == 0){
			free(s);
			return 0;
		}
		n = tokenize(s, f, nelem(f));
		for(i = 0; i < n; i++)
			newrefs(f[i]);
		free(s);
	}
	return -1;
}
