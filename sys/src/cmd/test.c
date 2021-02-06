/*
 * POSIX standard
 *	test expression
 *	[ expression ]
 *
 * Plan 9 additions:
 *	-A file exists and is append-only
 *	-L file exists and is exclusive-use
 *	-T file exists and is temporary
 */

#include <u.h>
#include <libc.h>

#define EQ(a,b)	((tmp=a)==0?0:(strcmp(tmp,b)==0))

int	ap;
int	ac;
char	**av;
char	*tmp;

void	synbad(char *, char *);
int	fsizep(char *);
int	isdir(char *);
int	isreg(char *);
int	isatty(int);
int	isint(char *, int *);
int	isolder(char *, char *);
int	isolderthan(char *, char *);
int	isnewerthan(char *, char *);
int	hasmode(char *, ulong);
int	tio(char *, int);
int	e(int), e1(int), e2(int), e3(int);
char	*nxtarg(int);

void
main(int argc, char *argv[])
{
	int r;
	char *c;

	ac = argc; av = argv; ap = 1;
	if(EQ(argv[0],"[")) {
		if(!EQ(argv[--ac],"]"))
			synbad("] missing","");
	}
	argv[ac] = 0;
	if (ac<=1)
		exits("usage");
	r = e(1);
	if((c = nxtarg(1)) != nil)
		synbad("unexpected operator/operand: ", c);
	exits(r?0:"false");
}

char *
nxtarg(int mt)
{
	if(ap>=ac){
		if(mt){
			ap++;
			return(0);
		}
		synbad("argument expected","");
	}
	return(av[ap++]);
}

int
nxtintarg(int *pans)
{
	if(ap<ac && isint(av[ap], pans)){
		ap++;
		return 1;
	}
	return 0;
}

int
e(int eval)
{
	char *op;
	int p1;

	p1 = e1(eval);
	for(;;){
		op = nxtarg(1);
		if(op == nil)
			break;
		if(!EQ(op, "-o")){
			ap--;
			return p1;
		}
		if(!p1 && eval)
			p1 |= e1(1);
		else
			e1(0);
	}
	return p1;
}

int
e1(int eval)
{
	char *op;
	int p1;

	p1 = e2(eval);
	for(;;){
		op = nxtarg(1);
		if(op == nil)
			break;
		if(!EQ(op, "-a")){
			ap--;
			return p1;
		}
		if(p1 && eval)
			p1 &= e2(1);
		else
			e2(0);
	}
	return p1;
}

int
e2(int eval)
{
	char *op;
	int p1;

	p1 = 0;
	for(;;){
		op = nxtarg(1);
		if(op == nil)
			return p1 ^ 1;
		if(!EQ(op, "!"))
			break;
		p1 ^= 1;
	}
	ap--;
	return(p1^e3(eval));
}

int
e3(int eval)
{
	int p1, int1, int2;
	char *a, *b, *p2;

	a = nxtarg(0);
	if(EQ(a, "(")) {
		p1 = e(eval);
		if(!EQ(nxtarg(0), ")"))
			synbad(") expected","");
		return(p1);
	}

	if(EQ(a, "-A")){
		b = nxtarg(0);
		return(eval && hasmode(b, DMAPPEND));
	}

	if(EQ(a, "-L")){
		b = nxtarg(0);
		return(eval && hasmode(b, DMEXCL));
	}

	if(EQ(a, "-T")){
		b = nxtarg(0);
		return(eval && hasmode(b, DMTMP));
	}

	if(EQ(a, "-f")){
		b = nxtarg(0);
		return(eval && isreg(b));
	}

	if(EQ(a, "-d")){
		b = nxtarg(0);
		return(eval && isdir(b));
	}

	if(EQ(a, "-r")){
		b = nxtarg(0);
		return(eval && tio(b, AREAD));
	}

	if(EQ(a, "-w")){
		b = nxtarg(0);
		return(eval && tio(b, AWRITE));
	}

	if(EQ(a, "-x")){
		b = nxtarg(0);
		return(eval && tio(b, AEXEC));
	}

	if(EQ(a, "-e")){
		b = nxtarg(0);
		return(eval && tio(b, AEXIST));
	}

	if(EQ(a, "-c"))
		return(0);

	if(EQ(a, "-b"))
		return(0);

	if(EQ(a, "-u"))
		return(0);

	if(EQ(a, "-g"))
		return(0);

	if(EQ(a, "-s")){
		b = nxtarg(0);
		return(eval && fsizep(b));
	}

	if(EQ(a, "-t"))
		if(ap>=ac)
			return(eval && isatty(1));
		else if(nxtintarg(&int1))
			return(eval && isatty(int1));
		else
			synbad("not a valid file descriptor number ", "");

	if(EQ(a, "-n"))
		return(!EQ(nxtarg(0), ""));
	if(EQ(a, "-z"))
		return(EQ(nxtarg(0), ""));

	p2 = nxtarg(1);
	if (p2==0)
		return(!EQ(a,""));
	if(EQ(p2, "="))
		return(EQ(nxtarg(0), a));

	if(EQ(p2, "!="))
		return(!EQ(nxtarg(0), a));

	if(EQ(p2, "-older")){
		b = nxtarg(0);
		return(eval && isolder(b, a));
	}

	if(EQ(p2, "-ot")){
		b = nxtarg(0);
		return(eval && isolderthan(b, a));
	}

	if(EQ(p2, "-nt")){
		b = nxtarg(0);
		return(eval && isnewerthan(b, a));
	}

	if(isint(a, &int1)){
		if(nxtintarg(&int2)){
			if(EQ(p2, "-eq"))
				return(int1==int2);
			if(EQ(p2, "-ne"))
				return(int1!=int2);
			if(EQ(p2, "-gt"))
				return(int1>int2);
			if(EQ(p2, "-lt"))
				return(int1<int2);
			if(EQ(p2, "-ge"))
				return(int1>=int2);
			if(EQ(p2, "-le"))
				return(int1<=int2);
			ap--;
		}
	}
	ap--;
	return !EQ(a, "");
}

int
tio(char *a, int f)
{
	return access (a, f) >= 0;
}

/*
 * note that the name strings pointed to by Dir members are
 * allocated with the Dir itself (by the same call to malloc),
 * but are not included in sizeof(Dir), so copying a Dir won't
 * copy the strings it points to.
 */

int
hasmode(char *f, ulong m)
{
	int r;
	Dir *dir;

	dir = dirstat(f);
	if (dir == nil)
		return 0;
	r = (dir->mode & m) != 0;
	free(dir);
	return r;
}

int
isdir(char *f)
{
	return hasmode(f, DMDIR);
}

int
isreg(char *f)
{
	int r;
	Dir *dir;

	dir = dirstat(f);
	if (dir == nil)
		return 0;
	r = (dir->mode & DMDIR) == 0;
	free(dir);
	return r;
}

int
isatty(int fd)
{
	int r;
	Dir *d1, *d2;

	d1 = dirfstat(fd);
	d2 = dirstat("/dev/cons");
	if (d1 == nil || d2 == nil)
		r = 0;
	else
		r = d1->type == d2->type && d1->dev == d2->dev &&
			d1->qid.path == d2->qid.path;
	free(d1);
	free(d2);
	return r;
}

int
fsizep(char *f)
{
	int r;
	Dir *dir;

	dir = dirstat(f);
	if (dir == nil)
		return 0;
	r = dir->length > 0;
	free(dir);
	return r;
}

void
synbad(char *s1, char *s2)
{
	int len;

	write(2, "test: ", 6);
	if ((len = strlen(s1)) != 0)
		write(2, s1, len);
	if ((len = strlen(s2)) != 0)
		write(2, s2, len);
	write(2, "\n", 1);
	exits("bad syntax");
}

int
isint(char *s, int *pans)
{
	char *ep;

	*pans = strtol(s, &ep, 0);
	return (*ep == 0);
}

int
isolder(char *pin, char *f)
{
	int r;
	ulong n, m;
	char *p = pin;
	Dir *dir;

	dir = dirstat(f);
	if (dir == nil)
		return 0;

	/* parse time */
	n = 0;
	r = 1;
	while(*p){
		m = strtoul(p, &p, 0);
		switch(*p){
		case 0:
			n = m;
			r = 0;
			break;
		case 'y':
			m *= 12;
			/* fall through */
		case 'M':
			m *= 30;
			/* fall through */
		case 'd':
			m *= 24;
			/* fall through */
		case 'h':
			m *= 60;
			/* fall through */
		case 'm':
			m *= 60;
			/* fall through */
		case 's':
			n += m;
			p++;
			break;
		default:
			synbad("bad time syntax, ", pin);
		}
	}

	if (r != 0)
		n = time(0) - n;
	r = dir->mtime < n;
	free(dir);
	return r;
}

int
isolderthan(char *a, char *b)
{
	int r;
	Dir *ad, *bd;

	ad = dirstat(a);
	bd = dirstat(b);
	if (ad == nil || bd == nil)
		r = 0;
	else
		r = ad->mtime > bd->mtime;
	free(ad);
	free(bd);
	return r;
}

int
isnewerthan(char *a, char *b)
{
	int r;
	Dir *ad, *bd;

	ad = dirstat(a);
	bd = dirstat(b);
	if (ad == nil || bd == nil)
		r = 0;
	else
		r = ad->mtime < bd->mtime;
	free(ad);
	free(bd);
	return r;
}
