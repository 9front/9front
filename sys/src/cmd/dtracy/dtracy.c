#include <u.h>
#include <libc.h>
#include <dtracy.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

DTAgg noagg;

char *dtracyroot = "#Δ";
int dtracyno;
int ctlfd, buffd;

int
min(int a, int b)
{
	return a < b ? a : b;
}

int
max(int a, int b)
{
	return a < b ? b : a;
}

void *
emalloc(ulong n)
{
	void *v;
	
	v = malloc(n);
	if(v == nil) sysfatal("malloc: %r");
	memset(v, 0, n);
	setmalloctag(v, getcallerpc(&n));
	return v;
}

void *
erealloc(void *v, ulong n)
{
	v = realloc(v, n);
	if(n != 0){
		if(v == nil) sysfatal("realloc: %r");
		setrealloctag(v, getcallerpc(&v));
	}
	return v;
}

void *
dtmalloc(ulong n)
{
	return emalloc(n);
}

void
dtfree(void *v)
{
	free(v);
}

void
defvar(char *name, int idx, Type *ty)
{
	Symbol *s;
	
	s = getsym(name);
	s->type = SYMVAR;
	s->idx = idx;
	s->typ = ty;
}

void
globvars(void)
{
	defvar("arg0", DTV_ARG0, type(TYPINT, 8, 1));
	defvar("arg1", DTV_ARG1, type(TYPINT, 8, 1));
	defvar("arg2", DTV_ARG2, type(TYPINT, 8, 1));
	defvar("arg3", DTV_ARG3, type(TYPINT, 8, 1));
	defvar("arg4", DTV_ARG4, type(TYPINT, 8, 1));
	defvar("arg5", DTV_ARG5, type(TYPINT, 8, 1));
	defvar("arg6", DTV_ARG6, type(TYPINT, 8, 1));
	defvar("arg7", DTV_ARG7, type(TYPINT, 8, 1));
	defvar("arg8", DTV_ARG8, type(TYPINT, 8, 1));
	defvar("arg9", DTV_ARG9, type(TYPINT, 8, 1));
	defvar("pid", DTV_PID, type(TYPINT, 4, 1));
	defvar("machno", DTV_MACHNO, type(TYPINT, 4, 1));
	defvar("time", DTV_TIME, type(TYPINT, 8, 0));
	defvar("probe", DTV_PROBE, type(TYPSTRING));
}

int
setup(void)
{
	char buf[512];
	char *p;
	int n;
	
	snprint(buf, sizeof(buf), "%s/clone", dtracyroot);
	ctlfd = open(buf, ORDWR);
	if(ctlfd < 0) return -1;
	n = read(ctlfd, buf, sizeof(buf) - 1);
	if(n < 0) return -1;
	buf[n] = 0;
	dtracyno = strtol(buf, &p, 10);
	if(p == buf || *p != 0){
		werrstr("expected number reading from ctl");
		return -1;
	}
	snprint(buf, sizeof(buf), "%s/%d/buf", dtracyroot, dtracyno);
	buffd = open(buf, OREAD);
	if(buffd < 0) return -1;
	return 0;
}

int
progcopy(void)
{
	char buf[512];
	int fd;
	char *prog;
	Fmt f;

	fmtstrinit(&f);
	packclauses(&f);
	prog = fmtstrflush(&f);
	snprint(buf, sizeof(buf), "%s/%d/prog", dtracyroot, dtracyno);
	fd = open(buf, OWRITE);
	if(fd < 0) return -1;
	if(write(fd, prog, strlen(prog)) < 0){
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int
epidread(void)
{
	char buf[512];
	Biobuf *bp;
	char *s;
	char *f[5];
	int a, b, c;

	snprint(buf, sizeof(buf), "%s/%d/epid", dtracyroot, dtracyno);
	bp = Bopen(buf, OREAD);
	if(bp == nil) return -1;
	for(; s = Brdstr(bp, '\n', 1), s != nil; free(s)){
		if(tokenize(s, f, nelem(f)) != 4)
			goto err;
		a = atoi(f[0]);
		b = atoi(f[1]);
		c = atoi(f[2]);
		addepid(a, b, c, f[3]);
	}
	return 0;
err:
	werrstr("epidread: invalid format");
	free(s);
	return -1;
	
}

int
bufread(Biobuf *bp)
{
	static uchar buf[65536];
	int n;
	
	n = read(buffd, buf, sizeof(buf));
	if(n < 0)
		sysfatal("bufread: %r");
	if(parsebuf(buf, n, bp) < 0)
		sysfatal("parsebuf: %r");
	Bflush(bp);
	return 0;
}

void
aggproc(void)
{
	char buf[65536];
	int buffd, n;
	extern int interrupted;

	switch(rfork(RFPROC|RFMEM)){
	case -1: sysfatal("rfork: %r");
	case 0: return;
	default: break;
	}
	snprint(buf, sizeof(buf), "%s/%d/aggbuf", dtracyroot, dtracyno);
	buffd = open(buf, OREAD);
	if(buffd < 0) sysfatal("open: %r");
	agginit();
	atnotify(aggnote, 1);
	while(!interrupted){
		n = read(buffd, buf, sizeof(buf));
		if(n < 0){
			if(interrupted)
				break;
			sysfatal("aggbufread: %r");
		}
		if(aggparsebuf((uchar *) buf, n) < 0)
			exits("error");
	}
	aggdump();
	exits(nil);
}

static void
usage(void)
{
	fprint(2, "usage: %s [ -d ] script\n", argv0);
	exits("usage");
}

int dflag;

void
main(int argc, char **argv)
{
	Biobuf *out;

	dflag = 0;
	ARGBEGIN {
	case 'd': dflag = 1; break;
	default: usage();
	} ARGEND;
	if(argc != 1) usage();

	fmtinstall(L'α', nodetfmt);
	fmtinstall('t', typetfmt);
	fmtinstall(L'I', dtefmt);
	fmtinstall(L'τ', typefmt);
	fmtinstall(L'ε', nodefmt);
	lexinit();
	lexstring(argv[0]);
	globvars();
	yyparse();
	if(errors != 0)
		exits("errors");
	if(dflag)
		dump();
	else{
		if(setup() < 0)
			sysfatal("setup: %r");
		if(progcopy() < 0)
			sysfatal("progcopy: %r");
		if(epidread() < 0)
			sysfatal("epidread: %r");
		fprint(ctlfd, "go");
		out = Bfdopen(1, OWRITE);
		if(out == nil) sysfatal("Bfdopen: %r");
		if(aggid > 0)
			aggproc();
		for(;;)
			bufread(out);
	}
	exits(nil);
}
