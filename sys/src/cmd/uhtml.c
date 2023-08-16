#include <u.h>
#include <libc.h>
#include <ctype.h>

int nbuf;
char buf[IOUNIT+1];
char *cset = nil;
char *whitespace = " \t\r\n";

void
usage(void)
{
	fprint(2, "%s [ -p ] [ -c charset ] [ file ]\n", argv0);
	exits("usage");
}

char*
attr(char *s, char *a)
{
	char *e, q;

	if((s = cistrstr(s, a)) == nil)
		return nil;
	s += strlen(a);
	while(*s && strchr(whitespace, *s))
		s++;
	if(*s++ != '=')
		return nil;
	while(*s && strchr(whitespace, *s))
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
	if((e - s) > 1)
		return smprint("%.*s", (int)(e - s), s);
	return nil;
}

void
main(int argc, char *argv[])
{
	int n, q, pfd[2], pflag = 0;
	char *arg[4], *s, *g, *e, *p, *a, t;
	Rune r;

	ARGBEGIN {
	case 'c':
		cset = EARGF(usage());
		break;
	case 'p':
		pflag = 1;
		break;
	default:
		usage();
	} ARGEND;

	if(*argv){
		close(0);
		if(open(*argv, OREAD) != 0)
			sysfatal("open: %r");
	}
	nbuf = 0;
	while(nbuf < sizeof(buf)-1){
		if((n = read(0, buf + nbuf, sizeof(buf)-1-nbuf)) <= 0)
			break;
		nbuf += n;
		buf[nbuf] = 0;
	}

	p = buf;
	if(nbuf >= 3 && memcmp(p, "\xEF\xBB\xBF", 3)==0){
		p += 3;
		nbuf -= 3;
		cset = "utf";
		goto Found;
	}
	if(nbuf >= 2 && memcmp(p, "\xFE\xFF", 2) == 0){
		p += 2;
		nbuf -= 2;
		cset = "unicode-be";
		goto Found;
	}
	if(nbuf >= 2 && memcmp(p, "\xFF\xFE", 2) == 0){
		p += 2;
		nbuf -= 2;
		cset = "unicode-le";
		goto Found;
	}

	s = p;
	do {
		if((s = strchr(s, '<')) == nil)
			break;
		q = 0;
		g = ++s;
		e = buf+nbuf;
		while(s < e){
			if(*s == '=' && q == 0)
				q = '=';
			else if(*s == '\'' || *s == '"'){
				if(q == '=')
					q = *s;
				else if(q == *s)
					q = 0;
			}
			else if(*s == '>' && q != '\'' && q != '"'){
				e = s;
				break;
			}
			else if(q == '=' && strchr(whitespace, *s) == nil)
				q = 0;
			s++;
		}
		t = *e;
		*e = 0;
		if((a = attr(g, "encoding")) != nil || (a = attr(g, "charset")) != nil)
		if(cistrcmp(a, "utf") != 0 && cistrcmp(a, "utf-8") != 0){
			cset = a;
			*e = t;
			break;
		}
		*e = t;
		s = ++e;
	} while(t);

	s = p;
	while(s+UTFmax < p+nbuf){
		s += chartorune(&r, s);
		if(r == Runeerror){
			if(cset == nil)
				cset = "latin1";
			goto Found;
		}
	}
	cset = "utf";

Found:
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
		arg[2] = smprint("{tcs -f %s || cat} | tcs -f html", cset);
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
		if((nbuf = read(0, p, sizeof(buf)-1)) < 0)
			sysfatal("read: %r");
	}
	close(1);
	waitpid();
	exits(0);
}
