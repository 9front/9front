#include <u.h>
#include <libc.h>
#include <ctype.h>

void
usage(void)
{
	print("status=usage\n");
	exits(0);
}

char*
skipspace(char *p)
{
	while(isspace(*p))
		p++;
	return p;
}

char*
nextarg(char *p)
{
	char *s;

	s = strchr(p, ',');
	if(s == nil)
		return p+strlen(p); /* to \0 */
	while(*s == ',' || isspace(*s))
		s++;
	return s;
}

char*
findarg(char *flags, Rune r)
{
	char *p;
	Rune rr;

	for(p=skipspace(flags); *p; p=nextarg(p)){
		chartorune(&rr, p);
		if(rr == r)
			return p;
	}
	return nil;
}

char*
argname(char *p)
{
	Rune r;
	int n;

	while(1){
		n = chartorune(&r, p);
		if(!isalpharune(r) && !isdigitrune(r))
			break;
		p += n;
	}
	return p;
}

int
countargs(char *p)
{
	int n;

	n = 1;
	for(p=skipspace(p); *p && *p != ','; p++)
		if(isspace(*p) && !isspace(*(p-1)))
			n++;
	return n;
}

void
main(int argc, char *argv[])
{
	char *flags, *p, *s, *e, buf[512];
	int i, n;
	Rune r;
	Fmt fmt;
	
	doquote = needsrcquote;
	quotefmtinstall();
	argv0 = argv[0];	/* for sysfatal */
	
	flags = getenv("flagfmt");
	if(flags == nil){
		fprint(2, "$flagfmt not set\n");
		print("exit 'missing flagfmt'");
		exits(0);
	}

	fmtfdinit(&fmt, 1, buf, sizeof buf);
	for(p=skipspace(flags); *p; p=nextarg(p)){
		s = e = nil;
		n = chartorune(&r, p);
		if(p[n] == ':'){
			s = p + n + 1;
			e = argname(s);
		}
		if(s != e)
			fmtprint(&fmt, "%.*s=()\n", (int)(e - s), s);
		else
			fmtprint(&fmt, "flag%C=()\n", r);
	}
	ARGBEGIN{
	default:
		if((p = findarg(flags, ARGC())) == nil)
			usage();
		p += runelen(ARGC());
		s = p + 1;
		e = p + 1;
		if(*p == ':' && (e = argname(s)) != s)
			p = e;
		if(*p == ',' || *p == 0){
			if(s != e)
				fmtprint(&fmt, "%.*s=1\n", (int)(e - s), s);
			else
				fmtprint(&fmt, "flag%C=1\n", ARGC());
			break;
		}
		n = countargs(p);
		if(s != e)
			fmtprint(&fmt, "%.*s=(", (int)(e - s), s);
		else
			fmtprint(&fmt, "flag%C=(", ARGC());
		for(i=0; i<n; i++)
			fmtprint(&fmt, "%s%q", i ? " " : "", EARGF(usage()));
		fmtprint(&fmt, ")\n");
	}ARGEND
	
	fmtprint(&fmt, "*=(");
	for(i=0; i<argc; i++)
		fmtprint(&fmt, "%s%q", i ? " " : "", argv[i]);
	fmtprint(&fmt, ")\n");
	fmtprint(&fmt, "status=''\n");
	fmtfdflush(&fmt);
	exits(0);
}
