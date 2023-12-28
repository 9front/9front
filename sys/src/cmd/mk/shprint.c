#include	"mk.h"

static char *vexpand(char*, Bufblock*);

void
shprint(char *s, Bufblock *buf)
{
	int n;
	Rune r;

	while(*s) {
		n = chartorune(&r, s);
		if (r == '$')
			s = vexpand(s, buf);
		else {
			rinsert(buf, r);
			s += n;
			s = copyq(s, r, buf);	/*handle quoted strings*/
		}
	}
	insert(buf, 0);
}

static Symtab*
mygetenv(char *name)
{
	Symtab *s;

	/* only resolve internal variables and variables we've set */
	s = symlook(name, S_INTERNAL, 0);
	if(s == 0){
		s = symlook(name, S_VAR, 0);
		if(s == 0 || !symlook(name, S_WESET, 0))
			return  0;
	}
	return s;
}

static char *
vexpand(char *w, Bufblock *buf)
{
	char carry, *p, *q;
	Symtab *s;

	assert(/*vexpand no $*/ *w == '$');
	p = w+1;	/* skip dollar sign */
	if(*p == '{') {
		p++;
		q = utfrune(p, '}');
		if (!q)
			q = strchr(p, 0);
	} else
		q = shname(p);
	carry = *q;
	*q = 0;
	s = mygetenv(p);
	*q = carry;
	if (carry == '}')
		q++;
	if(s)
		bufcpyw(buf, s->u.ptr);
	else 		/* copy $name intact */
		bufncpy(buf, w, q-w);
	return(q);
}

void
front(char *s)
{
	char *t, *q;
	int i, j;
	char *flds[512];

	q = Strdup(s);
	i = getfields(q, flds, nelem(flds), 0, " \t\n");
	if(i > 5){
		flds[4] = flds[i-1];
		flds[3] = "...";
		i = 5;
	}
	t = s;
	for(j = 0; j < i; j++){
		for(s = flds[j]; *s; *t++ = *s++);
		*t++ = ' ';
	}
	*t = 0;
	free(q);
}
