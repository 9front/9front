/*
 *  search the network database for matches
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>

/* for ndbvalfmt */
#pragma varargck type "$" char*

static int all, multiple, ipinfo, csinfo;
static Ndb *db = nil;
static char *net = nil;
static Biobuf bout;

static char*
skipat(char *s)
{
	if(*s == '@')
		s++;
	return s;
}

static int
match(char *attr, char **rattr, int nrattr)
{
	int i;

	if(nrattr == 0)
		return 1;
	for(i = 0; i < nrattr; i++){
		if(strcmp(attr, skipat(rattr[i])) == 0)
			return 1;
	}
	return 0;
}

/* print values of nt's attributes matching rattr */
static void
prmatch(Ndbtuple *nt, char **rattr, int nrattr)
{
	if(nt == nil)
		return;

	if(nrattr == 1) {
		for(; nt != nil; nt = nt->entry){
			if(match(nt->attr, rattr, nrattr)){
				Bprint(&bout, "%s\n", nt->val);
				if(!multiple && !all)
					break;
			}
		}
	} else {
		char *sep = "";

		for(; nt != nil; nt = nt->entry){
			if(match(nt->attr, rattr, nrattr)){
				Bprint(&bout, "%s%s=%$", sep, nt->attr, nt->val);
				sep = " ";
			}
			if(nt->entry != nt->line)
				sep = "\n\t";
		}
		Bprint(&bout, "\n");
	}
}

static void
search(char *attr, char *val, char **rattr, int nrattr)
{
	char *p;
	Ndbs s;
	Ndbtuple *t;

	if(ipinfo){
		if(csinfo)
			t = csipinfo(net, attr, val, rattr, nrattr);
		else
			t = ndbipinfo(db, attr, val, rattr, nrattr);
		prmatch(t, rattr, nrattr);
		ndbfree(t);
		return;
	}

	if(nrattr == 1 && !all){
		if(csinfo)
			p = csgetvalue(net, attr, val, rattr[0], &t);
		else
			p = ndbgetvalue(db, &s, attr, val, rattr[0], &t);
		if(p != nil && !multiple)
			Bprint(&bout, "%s\n", p);
		else
			prmatch(t, rattr, nrattr);
		ndbfree(t);
		free(p);
		return;
	}

	if(csinfo)
		return;

	for(t = ndbsearch(db, &s, attr, val); t != nil; t = ndbsnext(&s, attr, val)){
		prmatch(t, rattr, nrattr);
		ndbfree(t);
		if(!all)
			break;
	}
}

static void
usage(void)
{
	fprint(2, "usage: %s [-acim] [-x netmtpt] [-f ndbfile] attr value [rattr]...\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *dbfile = nil;

	ARGBEGIN{
	case 'a':
		all = 1;
		break;
	case 'c':
		csinfo = 1;
		break;
	case 'f':
		dbfile = EARGF(usage());
		break;
	case 'i':
		ipinfo = 1;
		break;
	case 'm':
		multiple = 1;
		break;
	case 'x':
		net = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	switch(argc){
	case 0:
	case 1:
		usage();
		break;
	case 2:
		if(ipinfo)
			usage();
		csinfo = 0;
		break;
	default:
		break;
	}

	fmtinstall('$', ndbvalfmt);

	if(Binit(&bout, 1, OWRITE) == -1)
		sysfatal("Binit: %r");

	if(csinfo)
		search(argv[0], argv[1], argv+2, argc-2);
	else {
		db = ndbopen(dbfile);
		if(db == nil){
			fprint(2, "%s: no db files\n", argv0);
			exits("no db");
		}
		search(argv[0], argv[1], argv+2, argc-2);
		ndbclose(db);
	}
	exits(nil);
}
