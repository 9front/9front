#include <u.h>
#include <libc.h>
#include <bio.h>
#include "ndb.h"

/*
 *  search for a tuple that has the given 'attr=val' and also 'rattr=x'.
 *  copy 'x' into 'buf' and return the whole tuple.
 *
 *  return nil if not found.
 */
char*
ndbgetvalue(Ndb *db, Ndbs *s, char *attr, char *val, char *rattr, Ndbtuple **pp)
{
	Ndbtuple *t, *nt;
	char *rv;
	Ndbs temps;

	if(s == nil)
		s = &temps;
	if(pp)
		*pp = nil;
	t = ndbsearch(db, s, attr, val);
	while(t != nil){
		nt = ndbfindattr(t, s->t, rattr);
		if(nt != nil){
			rv = strdup(nt->val);
			if(pp != nil)
				*pp = t;
			else
				ndbfree(t);
			return rv;
		}
		ndbfree(t);
		t = ndbsnext(s, attr, val);
	}
	return nil;
}
