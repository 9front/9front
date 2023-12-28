#include	"mk.h"

enum {
	ENVQUANTA=64
};

static Symtab **envy;
static int nextv;

static char	*myenv[] =
{
	"target",
	"stem",
	"prereq",
	"pid",
	"nproc",
	"newprereq",
	"alltarget",
	"newmember",
	"stem0",		/* must be in order from here */
	"stem1",
	"stem2",
	"stem3",
	"stem4",
	"stem5",
	"stem6",
	"stem7",
	"stem8",
	"stem9",
	0,
};

void
initenv(void)
{
	char **p;

	for(p = myenv; *p; p++)
		symlook(*p, S_INTERNAL, 1)->u.ptr = 0;
	readenv();				/* o.s. dependent */
}

static void
envupd(char *name, Word *value)
{
	Symtab *sym = symlook(name, S_INTERNAL, 0);
	assert(sym != 0);
	delword(sym->u.ptr);
	sym->u.ptr = value;
}

static void
envinsert(Symtab *sym)
{
	static int envsize;

	if (nextv >= envsize) {
		envsize += ENVQUANTA;
		envy = (Symtab **) Realloc(envy, envsize*sizeof(Symtab*));
	}
	envy[nextv++] = sym;
}

static void
ereset(Symtab *s)
{
	delword(s->u.ptr);
	s->u.ptr = 0;
	envinsert(s);
}

static void
ecopy(Symtab *s)
{
	if(symlook(s->name, S_NOEXPORT, 0))
		return;
	if(symlook(s->name, S_INTERNAL, 0))
		return;
	envinsert(s);
}

Symtab**
execinit(void)
{
	nextv = 0;
	symtraverse(S_INTERNAL, ereset);
	symtraverse(S_VAR, ecopy);
	envinsert(0);
	return envy;
}

Symtab**
buildenv(Job *j, int slot)
{
	char **p, *cp, *qp;
	Word *w, *v, **l;
	char num[16];
	int i;

	envupd("target", wdup(j->t));
	if(j->r->attr&REGEXP)
		envupd("stem",newword(""));
	else
		envupd("stem", newword(j->stem));
	envupd("prereq", wdup(j->p));
	snprint(num, sizeof num, "%d", getpid());
	envupd("pid", newword(num));
	snprint(num, sizeof num, "%d", slot);
	envupd("nproc", newword(num));
	envupd("newprereq", wdup(j->np));
	envupd("alltarget", wdup(j->at));
	l = &v;
	v = w = wdup(j->np);
	while(w){
		cp = strchr(w->s, '(');
		if(cp){
			qp = strchr(cp+1, ')');
			if(qp){
				*qp = 0;
				strcpy(w->s, cp+1);
				l = &w->next;
				w = w->next;
				continue;
			}
		}
		*l = w = popword(w);
	}
	envupd("newmember", v);
		/* update stem0 -> stem9 */
	for(p = myenv; *p; p++)
		if(strcmp(*p, "stem0") == 0)
			break;
	for(i = 0; *p; i++, p++){
		if((j->r->attr&REGEXP) && j->match[i])
			envupd(*p, newword(j->match[i]));
		else 
			envupd(*p, newword(""));
	}
	return envy;
}
