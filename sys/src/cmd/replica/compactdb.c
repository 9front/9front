/*
 * compact a database file
 */
#include "all.h"

Db *db;

void
usage(void)
{
	fprint(2, "usage: replica/compactdb db\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	Biobuf bout;
	Entry *e;

	quotefmtinstall();
	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	Binit(&bout, 1, OWRITE);
	db = opendb(argv[0]);
	for(e = (Entry*)avlmin(db->avl); e != nil; e = (Entry*)avlnext(e))
		Bprint(&bout, "%q %q %luo %q %q %lud %lld\n",
			e->name, strcmp(e->name, e->d.name)==0 ? "-" : e->d.name, e->d.mode,
			e->d.uid, e->d.gid, e->d.mtime, e->d.length);
	if(Bterm(&bout) < 0)
		sysfatal("writing output: %r");

	exits(nil);
}
