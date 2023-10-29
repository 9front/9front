#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <ip.h>
#include "dns.h"

Area *owned, *delegated;

static Area*
nameinarea(char *name, Area *s)
{
	int len;
	
	for(len = strlen(name); s != nil; s = s->next){
		if(s->len > len)
			continue;
		if(cistrcmp(s->soarr->owner->name, name + len - s->len) == 0)
			if(len == s->len || name[len - s->len - 1] == '.')
				return s;
	}
	return nil;
}

/*
 *  true if a name is in our area
 */
Area*
inmyarea(char *name)
{
	Area *s, *d;

	s = nameinarea(name, owned);
	if(s == nil)
		return nil;
	d = nameinarea(name, delegated);
	if(d && d->len > s->len)
		return nil;
	return s;	/* name is in owned area `s' and not in a delegated subarea */
}

/*
 *  our area is the part of the domain tree that
 *  we serve
 */
void
addarea(RR *rp, Ndbtuple *t)
{
	DN *dp;
	Area *s;
	Area **l;
	int len;

	dp = rp->owner;
	len = strlen(dp->name);

	if(t->val[0])
		l = &delegated;
	else
		l = &owned;

	for (s = *l; s != nil; l = &s->next, s = s->next){
		if(s->len < len)
			break;
		if(s->soarr->owner == dp)
			return;		/* we've already got one */
	}

	/*
	 *  The area contains a copy of the soa rr that created it.
	 *  The owner of the the soa rr should stick around as long
	 *  as the area does.
	 */
	s = emalloc(sizeof(*s));
	s->len = len;
	rrcopy(rp, &s->soarr);
	s->neednotify = 1;
	s->needrefresh = 0;

	if (debug)
		dnslog("new area %s %s", dp->name,
			l == &delegated? "delegated": "owned");

	s->next = *l;
	*l = s;
}

void
freeareas(Area **l)
{
	Area *s;

	while(s = *l){
		*l = s->next;
		rrfree(s->soarr);
		memset(s, 0, sizeof *s);	/* cause trouble */
		free(s);
	}
}
