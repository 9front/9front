#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ip.h>
#include <ndb.h>

/*
 *  Look for a pair with the given attribute.  look first on the same line,
 *  then in the whole entry.
 */
Ndbtuple*
ndbfindattr(Ndbtuple *entry, Ndbtuple *line, char *attr)
{
	Ndbtuple *nt;

	/* first look on same line (closer binding) */
	for(nt = line; nt;){
		if(strcmp(attr, nt->attr) == 0)
			return nt;
		nt = nt->line;
		if(nt == line)
			break;
	}

	/* search whole tuple */
	for(nt = entry; nt; nt = nt->entry)
		if(strcmp(attr, nt->attr) == 0)
			return nt;

	return nil;
}
