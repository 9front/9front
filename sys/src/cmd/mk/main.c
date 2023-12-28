#include	"mk.h"

#define		MKFILE		"mkfile"

int debug;
int nflag = 0;
int sflag = 0;
int tflag = 0;
int iflag = 0;
int kflag = 0;
int aflag = 0;
int uflag = 0;
int nreps = 1;
char *explain = 0;
Word *target1;
Job *jobs;
Rule *rules, *patrule, *metarules;
Biobuf bout;

void badusage(void);

void
main(int argc, char **argv)
{
	Word **link, *flags, *files, *args;
	Bufblock *whatif;
	char *s;
	int i;

	/*
	 *  start with a copy of the current environment variables
	 *  instead of sharing them
	 */

	Binit(&bout, 1, OWRITE);
	whatif = 0;
	USED(argc);

	flags = 0;
	link = &flags;
	for(argv++; *argv && (**argv == '-'); argv++, link = &(*link)->next)
	{
		*link = newword(*argv);
		switch(argv[0][1])
		{
		case 'a':
			aflag = 1;
			break;
		case 'd':
			if(*(s = &argv[0][2]))
				while(*s) switch(*s++)
				{
				case 'p':	debug |= D_PARSE; break;
				case 'g':	debug |= D_GRAPH; break;
				case 'e':	debug |= D_EXEC; break;
				}
			else
				debug = 0xFFFF;
			break;
		case 'e':
			explain = &argv[0][2];
			break;
		case 'f':
			argv++;
			if(*argv == 0 || **argv == 0)
				badusage();
			link = &(*link)->next;
			*link = newword(*argv);
			wadd(&files, *argv);
			break;
		case 'i':
			iflag = 1;
			break;
		case 'k':
			kflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 't':
			tflag = 1;
			break;
		case 'u':
			uflag = 1;
			break;
		case 'w':
			if(whatif == 0)
				whatif = newbuf();
			else
				insert(whatif, ' ');
			if(argv[0][2])
				bufcpy(whatif, &argv[0][2]);
			else {
				if(*++argv == 0)
					badusage();
				bufcpy(whatif, &argv[0][0]);
				link = &(*link)->next;
				*link = newword(*argv);
			}
			break;
		default:
			badusage();
		}
	}

	if(aflag)
		iflag = 1;
	usage();
	initenv();
	usage();

	/*
		assignment args become null strings
	*/
	mkinfile = "command line args";
	for(i = 0; argv[i]; i++) if(utfrune(argv[i], '=')){
		if(!wadd(&flags, argv[i]))
			varoverride(argv[i]);
		*argv[i] = 0;
	}
	setvar("MKFLAGS", flags);

	args = 0;
	link = &args;
	for(i = 0; argv[i]; i++){
		if(*argv[i] == 0) continue;
		*link = newword(argv[i]);
		link = &(*link)->next;
	}
	setvar("MKARGS", args);

	if(files == 0){
		if(access(MKFILE, AEXIST) == 0)
			parse(MKFILE, open(MKFILE, OREAD|OCEXEC));
	} else {
		for(; files; files = popword(files))
			parse(files->s, open(files->s, OREAD|OCEXEC));
	}
	if(DEBUG(D_PARSE)){
		dumpw("default targets", target1);
		dumpr("rules", rules);
		dumpr("metarules", metarules);
		dumpv("variables");
	}
	if(whatif){
		insert(whatif, 0);
		timeinit(whatif->start);
		freebuf(whatif);
	}
	execinit();

	catchnotes();
	if(args == 0){
		if(target1 == 0){
			fprint(2, "mk: nothing to mk\n");
			Exit();
		}
		for(; target1; target1 = popword(target1))
			mk(target1->s);
	} else {
		args = wdup(args);
		if(sflag){
			for(; args; args = popword(args))
				mk(args->s);
		} else {
			if(args->next == 0)
				mk(args->s);
			else {
				Word *head = newword(mkinfile);
				addrules(head, args, Strdup(""), VIR, 0, 0);
				mk(head->s);
			}
		}
	}
	if(uflag)
		prusage();
	exits(0);
}

void
badusage(void)
{
	fprint(2, "usage: mk [-f file] [-n] [-a] [-e] [-t] [-k] [-i] [-d[egp]] [targets ...]\n");
	Exit();
}

void *
Malloc(int n)
{
	void *s;

	s = malloc(n);
	if(!s) {
		fprint(2, "mk: cannot alloc %d bytes\n", n);
		Exit();
	}
	setmalloctag(s, getcallerpc(&n));
	return(s);
}

void *
Realloc(void *s, int n)
{
	if(s)
		s = realloc(s, n);
	else
		s = malloc(n);
	if(!s) {
		fprint(2, "mk: cannot alloc %d bytes\n", n);
		Exit();
	}
	setrealloctag(s, getcallerpc(&s));
	return(s);
}

char *
Strdup(char *s)
{
	int n = strlen(s)+1;
	char *d = Malloc(n);
	memcpy(d, s, n);
	setmalloctag(d, getcallerpc(&s));
	return d;
}

void
regerror(char *s)
{
	if(patrule)
		fprint(2, "mk: %s:%d: regular expression error; %s\n",
			patrule->file, patrule->line, s);
	else
		fprint(2, "mk: %s:%d: regular expression error; %s\n",
			mkinfile, mkinline, s);
	Exit();
}
