#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>

typedef struct Symbol Symbol;
typedef struct Event Event;
typedef struct Signal Signal;

struct Symbol {
	char *name;
	int t;
	double e;
	Symbol *next;
};
struct Event {
	double t;
	int val;
	Event *next;
	char *data;
	int line;
};
struct Signal {
	char *name;
	Event *ev;
	Signal *next;
};

int lineno, lastc, peeked;
char sname[512];
double sval;
double width, rheight;
double mint, maxt;
double left, right;
int nsig;
Biobuf *bp;
Symbol *stab[256];
Signal *sigfirst, **siglast = &sigfirst;

enum {
	SYMFREE,
	SYMNUM,
};

enum {
	VZ,
	VX,
	VL,
	VH,
	VMULT,
};

enum {
	CMD = -1,
	SYM = -2,
	NUM = -3,
	EOF = -4,
	STR = -5,
};

static int
Tfmt(Fmt *f)
{
	int n;
	
	n = va_arg(f->args, int);
	if(n >= 0 && n < 0x80 && isprint(n))
		return fmtprint(f, "'%c'", n);
	else
		switch(n){
		case CMD: return fmtprint(f, ".%s", sname);
		case SYM: return fmtprint(f, "'%s'", sname);
		case NUM: return fmtprint(f, "%g", sval);
		case EOF: return fmtprint(f, "EOF");
		case STR: return fmtprint(f, "%#q", sname);
		default: return fmtprint(f, "%d", n);
		}
}

static void *
emalloc(int n)
{
	void *v;
	
	v = malloc(n);
	if(v == nil)
		sysfatal("malloc: %r");
	memset(v, 0, n);
	setmalloctag(v, getcallerpc(&n));
	return v;
}

static void
error(int l, char *fmt, ...)
{
	va_list va;
	char *s;
	
	va_start(va, fmt);
	s = vsmprint(fmt, va);
	fprint(2, "%d %s\n", l, s);
	free(s);
	va_end(va);
}

static int
hash(char *s)
{
	int c;

	for(c = 0; *s != 0; s++)
		c += *s;
	return c;
}

static Symbol *
getsym(char *st)
{
	Symbol *s, **p;
	
	for(p = &stab[hash(st)%nelem(stab)]; s = *p, s != nil; p = &s->next){
		if(strcmp(s->name, st) == 0)
			return s;
	}
	s = emalloc(sizeof(Symbol));
	s->name = strdup(st);
	*p = s;
	return s;
}

static int
issym(int c)
{
	return isalnum(c) || c == '_' || c >= 0x80;
}

static int
numfsm(int c, int *st)
{
	enum {
		PREDOT,
		POSTDOT,
		ESIGN,
		EDIG
	};
	
	switch(*st){
	case PREDOT:
		if(c == '.')
			*st = POSTDOT;
		if(c == 'e')
			*st = ESIGN;
		return isdigit(c) || c == '.' || c == 'e';
	case POSTDOT:
		if(c == 'e')
			*st = ESIGN;
		return isdigit(c) || c == 'e';
	case ESIGN:
		*st = EDIG;
		return isdigit(c) || c == '+' || c == '-';
	case EDIG:
		return isdigit(c);
	}
	return 0;
}

static int
lex(void)
{
	int c;
	char *p;
	int st;

	do{
		c = Bgetc(bp);
		if(c < 0)
			return EOF;
		if(!isspace(c))
			break;
		if(c == '\n')
			lineno++;
		lastc = c;
	}while(1);
	
	if(lastc == 10 && c == '.'){
		for(p = sname; c = Bgetc(bp), issym(c); )
			if(p < sname + sizeof(sname) - 1)
				*p++ = c;
		Bungetc(bp);
		*p = 0;
		return CMD;
	}
	if(isdigit(c) || c == '.'){
		st = 0;
		for(p = sname; numfsm(c, &st); c = Bgetc(bp))
			if(p < sname + sizeof(sname) - 1)
				*p++ = c;
		Bungetc(bp);
		*p = 0;
		sval = strtol(sname, &p, 0);
		if(*p != 0)
			sval = strtod(sname, &p);
		if(*p != 0)
			error(lineno, "invalid number %s", sname);
		return NUM;
	}
	if(issym(c)){
		for(p = sname; issym(c); c = Bgetc(bp))
			if(p < sname + sizeof(sname) - 1)
				*p++ = c;
		Bungetc(bp);
		*p = 0;
		return SYM;
	}
	if(c == '\''){
		for(p = sname; c = Bgetc(bp), c != '\'' || Bgetc(bp) == '\''; )
			if(p < sname + sizeof(sname) - 1)
				*p++ = c;
		Bungetc(bp);
		*p = 0;
		return STR;
	}
	return c;
}

static int
next(void)
{
	int rc;

	if(peeked != 0){
		rc = peeked;
		peeked = 0;
		return rc;
	}
	return lex();
}

static int
peek(void)
{
	if(peeked == 0)
		peeked = lex();
	return peeked;
}

static void
expect(int n)
{
	int s;

	if((s = peek()) != n)
		error(lineno, "expected %T, got %T", n, s);
	else
		next();
}

static double expr(void);

static double
factor(void)
{
	int t;
	double g;
	Symbol *s;

	switch(t = next()){
	case NUM:
		return sval;
	case SYM:
		s = getsym(sname);
		if(s->t != SYMNUM)
			error(lineno, "not a number: %s", s->name);
		return s->e;
	case '(':
		g = expr();
		expect(')');
		return g;
	default:
		error(lineno, "factor: unexpected %T", t);
		return 0;
	}
}

static double
term(void)
{
	double g;

	g = factor();
	while(peek() == '*' || peek() == '/'){
		switch(next()){
		case '*': g *= factor(); break;
		case '/': g /= factor(); break;
		}
	}
	return g;
}

static double
expr(void)
{
	int s;
	double g;
	
	s = 1;
	if(peek() == '+' || peek() == '-')
		s = next() == '-' ? -1 : 1;
	g = s * term();
	while(peek() == '+' || peek() == '-'){
		s = next() == '-' ? -1 : 1;
		g += s * term();
	}
	return g;
}

static void
assign(Symbol *s)
{
	next();
	if(s->t != SYMFREE){
		error(lineno, "symbol already exists: %s", s->name);
		return;
	}
	s->t = SYMNUM;
	s->e = expr();
	expect(';');
}

static int
parseval(Event *e)
{
	int t;
	
	switch(t = next()){
	case SYM:
		if(strcmp(sname, "x") == 0)
			return VX;
		if(strcmp(sname, "z") == 0)
			return VZ;
		e->data = strdup(sname);
		return VMULT;
	case NUM:
		if(sval == 0)
			return VL;
		if(sval == 1)
			return VH;
		e->data = smprint("%g", sval);
		return VMULT;
	case STR:
		e->data = strdup(sname);
		return VMULT;
	default:
		error(lineno, "unexpected %T", t);
		return VZ;
	}
}

static void
append(double off, Event ***ep, Event *e)
{
	Event *f;
	
	for(; e != nil; e = e->next){
		f = emalloc(sizeof(Event));
		f->t = e->t + off;
		f->val = e->val;
		f->line = e->line;
		if(e->data != nil)
			f->data = strdup(e->data);
		**ep = f;
		*ep = &f->next;
	}
}

static void
freeev(Event *e)
{
	Event *en;
	
	for(; e != nil; e = en){
		en = e->next;
		free(e->data);
		free(e);
	}
}

static Event *
events(double *off, int ronly)
{
	int rela;
	Event *e, *ev, **ep;
	int i, n;
	double f;
	Symbol *sy;
	double len;
	int line;
	
	rela = 0;
	line = 0;
	ev = nil;
	ep = &ev;
	for(;;)
		switch(peek()){
		case '+':
			rela = 1;
			next();
			break;
		case '}':
		case ';':
			return ev;
		case ':':
			next();
			e = emalloc(sizeof(Event));
			e->t = *off;
			e->val = parseval(e);
			e->line = line;
			line = 0;
			*ep = e;
			ep = &e->next;
			break;
		case '|':
			line = 1;
			next();
			break;
		default:
			f = expr();
			if(peek() == '{'){
				next();
				if(f < 0 || isNaN(f) || f >= 0x7fffffff){
					error(lineno, "invalid repeat count");
					f = 0;
				}
				len = 0;
				e = events(&len, 1);
				expect('}');
				n = f + 0.5;
				for(i = 0; i < n; i++){
					append(*off, &ep, e);
					*off += len;
				}
				if(*off > maxt) maxt = *off;
				freeev(e);
				break;
			}
			if(ronly && !rela){
				error(lineno, "only relative addressing allowed");
				rela = 1;
			}
			if(peek() == SYM){
				next();
				sy = getsym(sname);
				if(sy->t == SYMFREE)
					error(lineno, "undefined %s", sy->name);
				else
					f *= sy->e;
			}
			if(rela)
				*off += f;
			else
				*off = f;
			if(peek() == '['){
				next();
				sy = getsym(sname);
				if(sy->t != SYMFREE && sy->t != SYMNUM)
					error(lineno, "already defined %s", sy->name);
				else{
					sy->t = SYMNUM;
					sy->e = *off;
				}
				expect(']');
			}
			if(*off < mint) mint = *off;
			if(*off > maxt) maxt = *off;
			rela = 0;
			break;
		}
	
}

static void
signal(char *st)
{
	Signal *s;
	double off;

	s = emalloc(sizeof(Signal));
	s->name = strdup(st);
	s->ev = events(&off, 0);
	expect(';');
	*siglast = s;
	siglast = &s->next;
	nsig++;
}

static void
slantfill(double x1, double x2, double t, double b, double tw)
{
	double x;
	double sw = 0.05;
	double soff = 0.05;

	for(x = x1; x < x2 - sw; x += soff)
		print("line from %g,%g to %g,%g\n", x, t, x+sw, b);
	if(x < x2)
		print("line from %g,%g to %g,%g\n", x, t, (2*sw*tw+2*tw*x+sw*x2)/(sw+2*tw), (sw*t+t*(x-x2)+b*(2*tw-x+x2))/(sw+2*tw));
}

static void
sigout(Signal *s, double top)
{
	Event *e, *n;
	double l, w, t, b, x1, x2, tw, m;
	
	for(e = s->ev; e != nil && e->next != nil && e->next->t < left; e = e->next)
		;
	if(e == nil)
		return;
	b = top - rheight * 0.75;
	t = top - rheight * 0.25;
	tw = width * 0.003;
	m = (t+b)/2;
	l = width * 0.2;
	w = width * 0.8 / (right - left);
	x1 = l;
	print("\"%s\" ljust at %g,%g\n", s->name, width * 0.1, m);
	while(n = e->next, n != nil && n->t < right){
		x2 = (n->t - left) * w + l;
		if(n->line)
			print("line from %g,%g to %g,%g dashed\n", x2, 0.0, x2, nsig*rheight);
		switch(e->val){
		case VZ:
			print("line from %g,%g to %g,%g\n", x1, m, x2, m);
			break;
		case VL:
			print("line from %g,%g to %g,%g\n", x1, b, x2-tw, b);
			print("line from %g,%g to %g,%g\n", x2-tw, b, x2, m);
			break;
		case VH:
			print("line from %g,%g to %g,%g\n", x1, t, x2-tw, t);
			print("line from %g,%g to %g,%g\n", x2-tw, t, x2, m);
			break;
		case VMULT:
			print("\"%s\" at %g,%g\n", e->data, (x1+x2)/2, m);
			if(0){
		case VX:
				slantfill(x1, x2-tw, t, b, tw);
			}
			print("line from %g,%g to %g,%g\n", x1, b, x2-tw, b);
			print("line from %g,%g to %g,%g\n", x2-tw, b, x2, m);
			print("line from %g,%g to %g,%g\n", x1, t, x2-tw, t);
			print("line from %g,%g to %g,%g\n", x2-tw, t, x2, m);
			break;
		default:
			fprint(2, "unknown event type %d\n", e->val);
		}
		switch(n->val){
		case VL:
			print("line from %g,%g to %g,%g\n", x2, m, x2+tw, b);
			break;
		case VH:
			print("line from %g,%g to %g,%g\n", x2, m, x2+tw, t);
			break;
		case VMULT:
		case VX:
			print("line from %g,%g to %g,%g\n", x2, m, x2+tw, b);
			print("line from %g,%g to %g,%g\n", x2, m, x2+tw, t);
			break;
		}
		e = e->next;
		if(e->val == VZ)
			x1 = x2;
		else
			x1 = x2 + tw;
	}
	x2 = (right - left) * w + l;
	switch(e->val){
	case VZ:
		print("line from %g,%g to %g,%g\n", x1, m, x2, m);
		break;
	case VL:
		print("line from %g,%g to %g,%g\n", x1, b, x2, b);
		break;
	case VH:
		print("line from %g,%g to %g,%g\n", x1, t, x2, t);
		break;
	case VMULT:
		print("\"%s\" at %g,%g\n", e->data, (x1+x2)/2, m);
		if(0){
	case VX:
			slantfill(x1, x2, t, b, tw);
		}
		print("line from %g,%g to %g,%g\n", x1, b, x2, b);
		print("line from %g,%g to %g,%g\n", x1, t, x2, t);
		break;
	default:
		fprint(2, "unknown event type %d\n", e->val);
	}
}

static void
parseopts(char *l)
{
	char *f[3];
	int rc;

	rc = tokenize(l, f, nelem(f));
	if(rc != 3){
		error(lineno, ".TPS wrong syntax");
		return;
	}
	width = strtod(f[1], 0);
	rheight = strtod(f[2], 0);
}

static void
cleansym(void)
{
	Symbol **p, *s, *sn;
	Signal *si, *sin;
	Event *e, *en;
	
	for(p = stab; p < stab + nelem(stab); p++)
		for(s = *p, *p = nil; s != nil; s = sn){

			free(s->name);
			sn = s->next;
			free(s);
		}
	memset(stab, 0, sizeof(stab));
	
	for(si = sigfirst; si != nil; si = sin){
		for(e = si->ev; e != nil; e = en){
			en = e->next;
			free(e);
		}
		free(si->name);
		sin = si->next;
		free(si);
	}
	siglast = &sigfirst;
}

static void
diagram(char *l)
{
	Symbol *s;
	Signal *si;
	int t;
	double top;
	
	lastc = 10;
	mint = 0;
	maxt = 0;
	nsig = 0;
	parseopts(l);

	for(;;){
		switch(t = next()){
		case SYM:
			s = getsym(sname);
			if(peek() == '=')
				assign(s);
			else
				signal(s->name);
			break;
		case STR:
			signal(sname);
			break;
		case CMD:
			if(strcmp(sname, "TPE") == 0)
				goto end;
			error(lineno, "unknown command %s", sname);
			break;
		default:
			error(lineno, "unexpected %T", t);
		}
	}
end:

	print(".PS %g %g\n", width, rheight * nsig);
	left = mint;
	right = maxt;
	top = rheight * nsig;
	for(si = sigfirst; si != nil; si = si->next, top -= rheight)
		sigout(si, top);
	print(".PE\n");
	
	cleansym();
}

static void
run(char *f)
{
	char *l;

	if(f == nil)
		bp = Bfdopen(0, OREAD);
	else
		bp = Bopen(f, OREAD);
	if(bp == nil)
		sysfatal("open: %r");
	Blethal(bp, nil);
	lineno = 1;
	
	for(;;){
		l = Brdstr(bp, '\n', 1);
		if(l == nil)
			break;
		lineno++;
		if(strncmp(l, ".TPS", 4) == 0 && (!l[4] || isspace(l[4])))
			diagram(l);
		else
			print("%s\n", l);
		free(l);
	}
	Bterm(bp);
}

static void
usage(void)
{
	fprint(2, "usage: %s [ files ]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i;

	fmtinstall('T', Tfmt);
	quotefmtinstall();

	ARGBEGIN {
	default: usage();
	} ARGEND;
	
	if(argc == 0)
		run(nil);
	else
		for(i = 0; i < argc; i++)
			run(argv[i]);
	
	exits(nil);
}
