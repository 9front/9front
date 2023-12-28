#include	"mk.h"

Job *
newjob(Rule *r, Node *nlist, char *stem, char **match, Word *pre, Word *npre, Word *tar, Word *atar)
{
	Job *j;

	j = (Job *)Malloc(sizeof(Job));
	j->r = r;
	j->n = nlist;
	j->stem = stem;
	j->match = match;
	j->p = pre;
	j->np = npre;
	j->t = tar;
	j->at = atar;
	j->nproc = -1;
	j->next = 0;
	return(j);
}

void
freejob(Job *j)
{
	delword(j->p);
	delword(j->np);
	delword(j->t);
	delword(j->at);
	free(j);
}

void
dumpj(char *s, Job *j, int all)
{
	char *t;

	Bprint(&bout, "%s\n", s);
	while(j){
		Bprint(&bout, "job@%p: r=%p n=%p stem='%s' nproc=%d\n",
			j, j->r, j->n, j->stem, j->nproc);
		Bprint(&bout, "\ttarget=%s", t = wtos(j->t)), free(t);
		Bprint(&bout, " alltarget=%s", t = wtos(j->at)), free(t);
		Bprint(&bout, " prereq=%s", t = wtos(j->p)), free(t);
		Bprint(&bout, " nprereq=%s\n", t = wtos(j->np)), free(t);
		j = all? j->next : 0;
	}
}
