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
	char *arg[4], *s, *p;

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
	if((nbuf = readn(0, buf, sizeof(buf)-1)) < 0)
		sysfatal("read: %r");
	buf[nbuf] = 0;
	p = buf;
	while(nbuf > 0){
		if(memcmp(p, "\xEF\xBB\xBF", 3)==0){
			p += 3;
			cset = "utf";
			break;
		}
		if(memcmp(p, "\xFE\xFF", 2) == 0){
			p += 2;
			cset = "unicode-be";
			break;
		}
		if(memcmp(p, "\xFF\xFE", 2) == 0){
			p += 2;
			cset = "unicode-le";
			break;
		}
		if(s = cistrstr(p, "encoding="))
			if(s = strval(s+9)){
				cset = s;
				break;
			}
		if(s = cistrstr(p, "charset="))
			if(s = strval(s+8)){
				cset = s;
				break;
			}
		break;
	}
	nbuf -= p - buf;

	if(pflag){
		print("%s\n", cset);
		exits(0);
	}

	if(nbuf == 0){
		write(1, p, 0);
		exits(0);
	}

	if(pipe(pfd) < 0)
		sysfatal("pipe: %r");

	switch(rfork(RFFDG|RFREND|RFPROC)){
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
		if(write(1, p, nbuf) != nbuf)
			sysfatal("write: %r");
		p = buf;
		if((nbuf = read(0, p, sizeof(buf))) < 0)
			sysfatal("read: %r");
	}
	close(1);
	waitpid();
	exits(0);
}
