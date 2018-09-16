#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>

/*
 *  remove duplicates
 */
Ndbtuple*
ndbdedup(Ndbtuple *t)
{
	Ndbtuple *nt, *last, *tt;

	for(nt = t; nt != nil; nt = nt->entry){
		last = nt;
		for(tt = nt->entry; tt != nil; tt = last->entry){
			if(strcmp(nt->attr, tt->attr) != 0
			|| strcmp(nt->val, tt->val) != 0){
				last = tt;
				continue;
			}
			if(last->line == tt)
				last->line = tt->line;
			last->entry = tt->entry;
			tt->entry = nil;
			ndbfree(tt);
		}
	}
	return t;
}
