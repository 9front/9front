#include <u.h>
#include <libc.h>
#include <ctype.h>

int nbuf;
char buf[4096+1];
char *cset = "utf";

void
usage(void)
{
	fprint(2, "%s [ -h ] [ -c charset ] [ file ]\n", argv0);
	exits("usage");
}

char*
strval(char *s)
{
	char *e, q;

	while(strchr("\t ", *s))
		s++;
	q = 0;
	if(*s == '"' || *s == '\'')
		q = *s++;
	for(e = s; *e; e++){
		if(*e == q)
			break;
		if(isalnum(*e))
			continue;
		if(*e == '-' || *e == '_')
			continue;
		break;
	}
	if(e - s > 1)
		return smprint("%.*s", (int)(e-s), s);
	return nil;
}

void
main(int argc, char *argv[])
{
	int pfd[2], pflag = 0;
	char *arg[4], *s;

	ARGBEGIN {
	case 'h':
		usage();
	case 'c':
		cset = EARGF(usage());
		break;
	case 'p':
		pflag = 1;
		break;
	} ARGEND;

	if(*argv){
		close(0);
		if(open(*argv, OREAD) != 1)
			sysfatal("open: %r");
	}
	if((nbuf = read(0, buf, sizeof(buf)-1)) < 0)
		sysfatal("read: %r");
	buf[nbuf] = 0;

	/* useless BOM marker */
	if(memcmp(buf, "\xEF\xBB\xBF", 3)==0)
		memmove(buf, buf+3, nbuf-3);

	for(;;){
		if(s = cistrstr(buf, "encoding="))
			if(s = strval(s+9)){
				cset = s;
				break;
			}
		if(s = cistrstr(buf, "charset="))
			if(s = strval(s+8)){
				cset = s;
				break;
			}
		break;
	}

	if(pflag){
		print("%s\n", cset);
		exits(0);
	}

	if(pipe(pfd) < 0)
		sysfatal("pipe: %r");

	if(nbuf == 0){
		write(1, buf, 0);
		exits(0);
	}

	switch(rfork(RFFDG|RFREND|RFPROC|RFNOWAIT)){
	case -1:
		sysfatal("fork: %r");
	case 0:
		dup(pfd[0], 0);
		close(pfd[0]);
		close(pfd[1]);

		arg[0] = "rc";
		arg[1] = "-c";
		arg[2] = smprint("{tcs -f %s | tcs -f html} || cat", cset);
		arg[3] = nil;
		exec("/bin/rc", arg);
	}

	dup(pfd[1], 1);
	close(pfd[0]);
	close(pfd[1]);

	while(nbuf > 0){
		if(write(1, buf, nbuf) != nbuf)
			sysfatal("write: %r");
		if((nbuf = read(0, buf, sizeof(buf))) < 0)
			sysfatal("read: %r");
	}
	exits(0);
}
