%{
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <mp.h>
#include <thread.h>
#include <libsec.h>

int inbase = 10, outbase, divmode, sep, heads, fail, prompt, eof;
enum { MAXARGS = 16 };

typedef struct Num Num;
struct Num {
	mpint;
	int b;
	Ref;
};
enum { STRONG = 0x100 };

void *
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

void *
error(char *fmt, ...)
{
	va_list va;
	Fmt f;
	char buf[256];
	
	fmtfdinit(&f, 2, buf, sizeof(buf));
	va_start(va, fmt);
	fmtvprint(&f, fmt, va);
	fmtrune(&f, '\n');
	fmtfdflush(&f);
	va_end(va);
	fail++;
	return nil;
}

Num *
numalloc(void)
{
	Num *r;
	
	r = emalloc(sizeof(Num));
	r->ref = 1;
	r->p = emalloc(sizeof(mpdigit));
	mpassign(mpzero, r);
	return r;
}

Num *
numincref(Num *n)
{
	incref(n);
	return n;
}

Num *
numdecref(Num *n)
{
	if(n == nil) return nil;
	if(decref(n) == 0){
		free(n->p);
		free(n);
		return nil;
	}
	return n;
}

Num *
nummod(Num *n)
{
	Num *m;

	if(n == nil) return nil;
	if(n->ref == 1) return n;
	m = numalloc();
	mpassign(n, m);
	m->b = n->b;
	numdecref(n);
	return m;
}

int
basemax(int a, int b)
{
	if(a == STRONG+10 && b >= STRONG) return b;
	if(b == STRONG+10 && a >= STRONG) return a;
	if(a == 10) return b;
	if(b == 10) return a;
	if(a < b) return b;
	return a;
}

%}
%token LOEXP LOLSH LORSH LOEQ LONE LOLE LOGE LOLAND LOLOR
%{

Num *
numbin(int op, Num *a, Num *b)
{
	mpint *r;
	
	if(fail || a == nil || b == nil) return nil;
	a = nummod(a);
	a->b = basemax(a->b, b->b);
	switch(op){
	case '+': mpadd(a, b, a); break;
	case '-': mpsub(a, b, a); break;
	case '*': mpmul(a, b, a); break;
	case '/':
		if(mpcmp(b, mpzero) == 0){
			numdecref(a);
			numdecref(b);
			return error("division by zero");
		}
		r = mpnew(0);
		mpdiv(a, b, a, r);
		if(!divmode && r->sign < 0)
			if(b->sign > 0)
				mpsub(a, mpone, a);
			else
				mpadd(a, mpone, a);
		mpfree(r);
		break;
	case '%':
		if(mpcmp(b, mpzero) == 0){
			numdecref(a);
			numdecref(b);
			return error("division by zero");
		}	
		mpdiv(a, b, nil, a);
		if(!divmode && a->sign < 0)
			if(b->sign > 0)
				mpadd(a, b, a);
			else
				mpsub(a, b, a);
		break;
	case '&': mpand(a, b, a); break;
	case '|': mpor(a, b, a); break;
	case '^': mpxor(a, b, a); break;
	case LOEXP:
		if(mpcmp(b, mpzero) < 0){
			numdecref(a);
			numdecref(b);
			return error("negative exponent");
		}
		mpexp(a, b, nil, a);
		break;
	case LOLSH:
		if(mpsignif(b) >= 31){
			if(b->sign > 0)
				error("left shift overflow");
			itomp(-(mpcmp(a, mpzero) < 0), a);
		}else
			mpasr(a, -mptoi(b), a);
		break;	
	case LORSH:
		if(mpsignif(b) >= 31){
			if(b->sign < 0)
				error("right shift overflow");
			itomp(-(mpcmp(a, mpzero) < 0), a);
		}else
			mpasr(a, mptoi(b), a);
		break;
	case '<': itomp(mpcmp(a, b) < 0, a); a->b = 0; break;
	case '>': itomp(mpcmp(a, b) > 0, a); a->b = 0; break;
	case LOLE: itomp(mpcmp(a, b) <= 0, a); a->b = 0; break;
	case LOGE: itomp(mpcmp(a, b) >= 0, a); a->b = 0; break;
	case LOEQ: itomp(mpcmp(a, b) == 0, a); a->b = 0; break;
	case LONE: itomp(mpcmp(a, b) != 0, a); a->b = 0; break;
	case LOLAND:
		a->b = b->b;
		if(mpcmp(a, mpzero) == 0)
			mpassign(mpzero, a);
		else
			mpassign(b, a);
		break;
	case LOLOR:
		a->b = b->b;
		if(mpcmp(a, mpzero) != 0)
			mpassign(mpone, a);
		else
			mpassign(b, a);
		break;
	case '$':
		a->b = b->b;
		mpxtend(b, mptoi(a), a);
		break;
	}
	numdecref(b);
	return a;
}

typedef struct Symbol Symbol;
struct Symbol {
	enum {
		SYMNONE,
		SYMVAR,
		SYMFUNC,
	} t;
	Num *val;
	int nargs;
	Num *(*func)(int, Num **); 
	char *name;
	Symbol *next;
};
Symbol *symtab[64];

Symbol *
getsym(char *n, int mk)
{
	Symbol **p;
	for(p = &symtab[*n&63]; *p != nil; p = &(*p)->next)
		if(strcmp((*p)->name, n) == 0)
			return *p;
	if(!mk) return nil;
	*p = emalloc(sizeof(Symbol));
	(*p)->name = strdup(n);
	return *p;
}

static void
printhead(int n, int s, int sp, char *t)
{
	char *q;
	int i, j, k;
	
	for(i = 1; i < n; i *= 10)
		;
	while(i /= 10, i != 0){
		q = t;
		*--q = 0;
		for(j = 0, k = 0; j < n; j += s, k++){
			if(k == sep && sep != 0){
				*--q = ' ';
				k = 0;
			}
			if(j >= i || j == 0 && i == 1)
				*--q = '0' + j / i % 10;
			else
				*--q = ' ';
		}
		for(j = 0; j < sp; j++)
			*--q = ' ';
		print("%s\n", q);
	}
}

void
numprint(Num *n)
{
	int b;
	int l, i, st, sp;
	char *s, *t, *p, *q;

	if(n == nil) return;
	if(n->b >= STRONG || n->b != 0 && outbase == 0)
		b = n->b & ~STRONG;
	else if(outbase == 0)
		b = 10;
	else
		b = outbase;
	s = mptoa(n, b, nil, 0);
	l = strlen(s);
	t = emalloc(l * 2 + 4);
	q = t + l * 2 + 4;
	if(heads){
		switch(b){
		case 16: st = 4; sp = 2; break;
		case 8: st = 3; sp = 1; break;
		case 2: st = 1; sp = 2; break;
		default: st = 0; sp = 0;
		}
		if(n->sign < 0)
			sp++;
		if(st != 0)
			printhead(mpsignif(n), st, sp, q);
	}
	*--q = 0;
	for(p = s + l - 1, i = 0; p >= s && *p != '-'; p--, i++){
		if(sep != 0 && i == sep){
			*--q = '_';
			i = 0;
		}
		if(*p >= 'A')
			*--q = *p + ('a' - 'A');
		else
			*--q = *p;
	}
	if(mpcmp(n, mpzero) != 0)
		switch(b){
		case 16: *--q = 'x'; *--q = '0'; break;
		case 10: if(outbase != 0 && outbase != 10 || inbase != 10) {*--q = 'd'; *--q = '0';} break;
		case 8: *--q = '0'; break;
		case 2: *--q = 'b'; *--q = '0'; break;
		}
	if(p >= s)
		*--q = '-';
	print("%s\n", q);
	free(s);
	free(t);
}

void
numdecrefs(int n, Num **x)
{
	int i;
	
	for(i = 0; i < n; i++)
		numdecref(x[i]);
}

Num *
fncall(Symbol *s, int n, Num **x)
{
	int i;

	if(s->t != SYMFUNC){
		numdecrefs(n, x);
		return error("%s: not a function", s->name);
	}
	else if(s->nargs >= 0 && s->nargs != n){
		numdecrefs(n, x);
		return error("%s: wrong number of arguments", s->name);
	}
	for(i = 0; i < n; i++)
		if(x[i] == nil)
			return nil;
	return s->func(n, x);
}

Num *
hexfix(Symbol *s)
{
	char *b, *p, *q;

	if(inbase != 16) return nil;
	if(s->val != nil) return numincref(s->val);
	if(strspn(s->name, "0123456789ABCDEFabcdef_") != strlen(s->name)) return nil;
	b = strdup(s->name);
	for(p = b, q = b; *p != 0; p++)
		if(*p != '_')
			*q++ = *p;
	*q = 0;
	s->val = numalloc();
	strtomp(b, nil, 16, s->val);
	s->val->b = 16;
	free(b);
	return numincref(s->val);
}

%}

%union {
	Num *n;
	Symbol *sym;
	struct {
		Num *x[MAXARGS];
		int n;
	} args;
}

%token <n> LNUM
%token <sym> LSYMB

%type <n> expr
%type <args> elist elist1

%right '='
%right '?'
%left LOLOR
%left LOLAND
%left '|'
%left '^'
%left '&'
%left LOEQ LONE
%left '<' '>' LOLE LOGE
%left LOLSH LORSH
%left '+' '-'
%left unary
%left '*' '/' '%'
%right LOEXP
%right '$'

%{
	int save;
	Num *last;
	Num *lastp;
%}

%%

input: | input line '\n' {
		if(!fail && last != nil) {
			numprint(last);
			numdecref(lastp);
			lastp = last;
		}
		fail = 0;
		last = nil;
	}

line: stat
	| line ';' stat

stat: { last = nil; }
	| expr { last = $1; }
	| '_' { save = inbase; inbase = 10; } expr {
		inbase = save;
		if(mpcmp($3, mpzero) < 0)
			error("no.");
		if(!fail) 
			sep = mptoi($3);
		numdecref($3);
		numdecref(last);
		last = nil;
	}
	| '<' { save = inbase; inbase = 10; } expr {
		inbase = save;
		if(!fail) 
			inbase = mptoi($3);
		if(inbase != 2 && inbase != 8 && inbase != 10 && inbase != 16){
			error("no.");
			inbase = save;
		}
		numdecref($3);
		numdecref(last);
		last = nil;
	}
	| '>' { save = inbase; inbase = 10; } expr {
		inbase = save;
		save = outbase;
		if(!fail) 
			outbase = mptoi($3);
		if(outbase != 0 && outbase != 2 && outbase != 8 && outbase != 10 && outbase != 16){
			error("no.");
			outbase = save;
		}
		numdecref($3);
		numdecref(last);
		last = nil;
	}
	| '/' { save = inbase; inbase = 10; } expr {
		inbase = save;
		save = divmode;
		if(!fail) 
			divmode = mptoi($3);
		if(divmode != 0 && divmode != 1){
			error("no.");
			divmode = save;
		}
		numdecref($3);
		numdecref(last);
		last = nil;
	}
	| '\'' { save = inbase; inbase = 10; } expr {
		inbase = save;
		save = heads;
		if(!fail) 
			heads = mptoi($3);
		if(heads != 0 && heads != 1){
			error("no.");
			heads = save;
		}
		numdecref($3);
		numdecref(last);
		last = nil;
	}
	| error

expr: LNUM
	| '(' expr ')' { $$ = $2; }
	| expr '+' expr { $$ = numbin('+', $1, $3); }
	| expr '-' expr { $$ = numbin('-', $1, $3); }
	| expr '*' expr { $$ = numbin('*', $1, $3); }
	| expr '/' expr { $$ = numbin('/', $1, $3); }
	| expr '%' expr { $$ = numbin('%', $1, $3); }
	| expr '&' expr { $$ = numbin('&', $1, $3); }
	| expr '|' expr { $$ = numbin('|', $1, $3); }
	| expr '^' expr { $$ = numbin('^', $1, $3); }	
	| expr LOEXP expr { $$ = numbin(LOEXP, $1, $3); }
	| expr LOLSH expr { $$ = numbin(LOLSH, $1, $3); }
	| expr LORSH expr { $$ = numbin(LORSH, $1, $3); }
	| expr LOEQ expr { $$ = numbin(LOEQ, $1, $3); }
	| expr LONE expr { $$ = numbin(LONE, $1, $3); }
	| expr '<' expr { $$ = numbin('<', $1, $3); }
	| expr '>' expr { $$ = numbin('>', $1, $3); }
	| expr LOLE expr { $$ = numbin(LOLE, $1, $3); }
	| expr LOGE expr { $$ = numbin(LOGE, $1, $3); }
	| expr LOLAND expr { $$ = numbin(LOLAND, $1, $3); }
	| expr LOLOR expr { $$ = numbin(LOLOR, $1, $3); }
	| '+' expr %prec unary { $$ = $2; }
	| '-' expr %prec unary { $$ = nummod($2); if($$ != nil) mpsub(mpzero, $$, $$); }
	| '~' expr %prec unary { $$ = nummod($2); if($$ != nil) mpnot($$, $$); }
	| '!' expr %prec unary { $$ = nummod($2); if($$ != nil) {itomp(mpcmp($$, mpzero) == 0, $$); $$->b = 0; } }
	| '$' expr { $$ = nummod($2); if($$ != nil) if($2->sign > 0) mpxtend($2, mpsignif($2), $$); else mpassign($2, $$); }
	| expr '?' expr ':' expr %prec '?' {
		if($1 == nil || mpcmp($1, mpzero) != 0){
			$$ = $3;
			numdecref($5);
		}else{
			$$ = $5;
			numdecref($3);
		}
		numdecref($1);
	}
	| LSYMB '(' elist ')' { $$ = fncall($1, $3.n, $3.x); }
	| LSYMB {
		Num *n;
		$$ = nil;
		switch($1->t){
		case SYMVAR: $$ = numincref($1->val); break;
		case SYMNONE:
			n = hexfix($1);
			if(n != nil) $$ = n;
			else error("%s undefined", $1->name);
			break;
		case SYMFUNC: error("%s is a function", $1->name); break;
		default: error("%s invalid here", $1->name);
		}
	}
	| LSYMB '=' expr {
		if($1->t != SYMNONE && $1->t != SYMVAR)
			error("%s redefined", $1->name);
		else if(!fail){
			$1->t = SYMVAR;
			numdecref($1->val);
			$1->val = numincref($3);
		}
		$$ = $3;
	}
	| '@' {
		$$ = lastp;
		if($$ == nil) error("no last result");
		else numincref($$);
	}
	| expr '$' expr { $$ = numbin('$', $1, $3); }

elist: { $$.n = 0; } | elist1
elist1: expr { $$.x[0] = $1; $$.n = 1; }
	| elist1 ',' expr {
		$$ = $1;
		if($$.n >= MAXARGS)
			error("too many arguments");
		else
			$$.x[$$.n++] = $3;
	}

%%

typedef struct Keyword Keyword;
struct Keyword {
	char *name;
	int tok;
};

Keyword ops[] = {
	"**", LOEXP,
	"<<", LOLSH,
	"<=", LOLE,
	">>", LORSH,
	">=", LOGE,
	"==", LOEQ,
	"&&", LOLAND,
	"||", LOLOR,
	"", 0,
};

Keyword *optab[128];


Biobuf *in;
int prompted;

int
yylex(void)
{
	int c, b;
	char buf[512], *p;
	Keyword *kw;

	if(prompt && !prompted && !eof) {print("; "); prompted = 1;}
	do
		c = Bgetc(in);
	while(c != '\n' && isspace(c));
	if(c < 0 && !eof){
		eof = 1;
		c = '\n';
	}
	if(c == '\n')
		prompted = 0;
	if(isdigit(c)){
		for(p = buf, *p++ = c; c = Bgetc(in), isalnum(c) || c == '_'; )
			if(p < buf + sizeof(buf) - 1 && c != '_')
				*p++ = c;
		*p = 0;
		if(c >= 0) Bungetc(in);
		b = inbase;
		p = buf;
		if(*p == '0'){
			p++;
			switch(*p++){
			case 0: p -= 2; break;
			case 'b': case 'B': b = 2; break;
			case 'd': case 'D': b = 10; break;
			case 'x': case 'X': b = 16; break;
			default: p--; b = 8; break;
			}
		}
		yylval.n = numalloc();
		strtomp(p, &p, b, yylval.n);
		if(*p != 0) error("not a number: %s", buf);
		yylval.n->b = b;
		return LNUM;
	}
	if(isalpha(c) || c >= 0x80 || c == '_'){
		for(p = buf, *p++ = c; c = Bgetc(in), isalnum(c) || c >= 0x80 || c == '_'; )
			if(p < buf + sizeof(buf) - 1)
				*p++ = c;
		*p = 0;
		Bungetc(in);
		if(buf[0] == '_' && buf[1] == 0) return '_';
		yylval.sym = getsym(buf, 1);
		return LSYMB;
	}
	if(c >= 0 && c < 128 && (kw = optab[c], kw != nil)){
		b = Bgetc(in);
		for(; kw->name[0] == c; kw++)
			if(kw->name[0] == b)
				return kw->tok;
		if(c >= 0) Bungetc(in);
	}
	return c;
}

void
yyerror(char *msg)
{
	error("%s", msg);
}

void
regfunc(char *n, Num *(*f)(int, Num **), int nargs)
{
	Symbol *s;
	
	s = getsym(n, 1);
	s->t = SYMFUNC;
	s->func = f;
	s->nargs = nargs;
}

int
toint(Num *n, int *p, int mustpos)
{
	if(mpsignif(n) > 31 || mustpos && mpcmp(n, mpzero) < 0){
		error("invalid argument");
		return -1;
	}
	if(p != nil)
		*p = mptoi(n);
	return 0;
}

Num *
fnhex(int, Num **a)
{
	Num *r;
	
	r = nummod(a[0]);
	r->b = STRONG | 16;
	return r;
}

Num *
fndec(int, Num **a)
{
	Num *r;
	
	r = nummod(a[0]);
	r->b = STRONG | 10;
	return r;
}

Num *
fnoct(int, Num **a)
{
	Num *r;
	
	r = nummod(a[0]);
	r->b = STRONG | 8;
	return r;
}

Num *
fnbin(int, Num **a)
{
	Num *r;
	
	r = nummod(a[0]);
	r->b = STRONG | 2;
	return r;
}

Num *
fnpb(int, Num **a)
{
	Num *r;
	int b;
	
	if(toint(a[1], &b, 1)){
	out:
		numdecref(a[0]);
		numdecref(a[1]);
		return nil;
	}
	if(b != 0 && b != 2 && b != 8 && b != 10 && b != 16){
		error("unsupported base");
		goto out;
	}
	r = nummod(a[0]);
	if(b == 0)
		r->b = 0;
	else
		r->b = STRONG | b;
	return r;
}

Num *
fnabs(int, Num **a)
{
	Num *r;
	
	r = nummod(a[0]);
	r->sign = 1;
	return r;
}

Num *
fnround(int, Num **a)
{
	mpint *q, *r;
	int i;

	if(mpcmp(a[1], mpzero) <= 0){
		numdecref(a[0]);
		numdecref(a[1]);
		return error("invalid argument");
	}
	q = mpnew(0);
	r = mpnew(0);
	a[0] = nummod(a[0]);
	mpdiv(a[0], a[1], q, r);
	if(r->sign < 0) mpadd(r, a[1], r);
	mpleft(r, 1, r);
	i = mpcmp(r, a[1]);
	mpright(r, 1, r);
	if(i > 0 || i == 0 && (a[0]->sign < 0) ^ (q->top != 0 && (q->p[0] & 1) != 0))
		mpsub(r, a[1], r);
	mpsub(a[0], r, a[0]);
	mpfree(q);
	mpfree(r);
	numdecref(a[1]);
	return a[0];
}

Num *
fnfloor(int, Num **a)
{
	mpint *r;

	if(mpcmp(a[1], mpzero) <= 0){
		numdecref(a[0]);
		numdecref(a[1]);
		return error("invalid argument");
	}
	r = mpnew(0);
	a[0] = nummod(a[0]);
	mpdiv(a[0], a[1], nil, r);
	if(r->sign < 0) mpadd(r, a[1], r);
	mpsub(a[0], r, a[0]);
	mpfree(r);
	numdecref(a[1]);
	return a[0];
}

Num *
fnceil(int, Num **a)
{
	mpint *r;

	if(mpcmp(a[1], mpzero) <= 0){
		numdecref(a[0]);
		numdecref(a[1]);
		return error("invalid argument");
	}
	r = mpnew(0);
	a[0] = nummod(a[0]);
	mpdiv(a[0], a[1], nil, r);
	if(r->sign < 0) mpadd(r, a[1], r);
	if(mpcmp(r, mpzero) != 0){
		mpsub(a[0], r, a[0]);
		mpadd(a[0], a[1], a[0]);
	}
	mpfree(r);
	numdecref(a[1]);
	return a[0];
}

Num *
fntrunc(int, Num **a)
{
	int i;
	
	if(toint(a[1], &i, 1)){
		numdecref(a[0]);
		numdecref(a[1]);
		return nil;
	}
	a[0] = nummod(a[0]);
	mptrunc(a[0], i, a[0]);
	return a[0];
}

Num *
fnxtend(int, Num **a)
{
	int i;
	
	if(toint(a[1], &i, 1)) return nil;
	a[0] = nummod(a[0]);
	mpxtend(a[0], i, a[0]);
	return a[0];
}

Num *
fnclog(int n, Num **a)
{
	int r;

	if(n != 1 && n != 2){
		numdecrefs(n, a);
		return error("clog: wrong number of arguments");
	}
	if(mpcmp(a[0], mpzero) <= 0 || n == 2 && mpcmp(a[1], mpone) <= 0){
		numdecref(a[0]);
		return error("invalid argument");
	}
	if(n == 1 || mpcmp(a[1], mptwo) == 0){
		a[0] = nummod(a[0]);
		mpsub(a[0], mpone, a[0]);
		itomp(mpsignif(a[0]), a[0]);
		a[0]->b = 0;
		if(n == 2) numdecref(a[1]);
		return a[0];
	}
	a[0] = nummod(a[0]);
	for(r = 0; mpcmp(a[0], mpone) > 0; r++){
		mpadd(a[0], a[1], a[0]);
		mpsub(a[0], mpone, a[0]);
		mpdiv(a[0], a[1], a[0], nil);
	}
	itomp(r, a[0]);
	a[0]->b = 0;
	numdecref(a[1]);
	return a[0];
}

Num *
fnubits(int, Num **a)
{
	if(a[0]->sign < 0){
		numdecref(a[0]);
		return error("invalid argument");
	}
	a[0] = nummod(a[0]);
	itomp(mpsignif(a[0]), a[0]);
	a[0]->b = 0;
	return a[0];
}

Num *
fnsbits(int, Num **a)
{
	a[0] = nummod(a[0]);
	if(a[0]->sign < 0) mpadd(a[0], mpone, a[0]);
	itomp(mpsignif(a[0]) + 1, a[0]);
	a[0]->b = 0;
	return a[0];
}

Num *
fnnsa(int, Num **a)
{
	int n, i;
	mpdigit d;

	a[0] = nummod(a[0]);
	if(a[0]->sign < 0){
		numdecref(a[0]);
		return error("invalid argument");
	}
	n = 0;
	for(i = 0; i < a[0]->top; i++){
		d = a[0]->p[i];
		for(; d != 0; d &= d-1)
			n++;
	}
	itomp(n, a[0]);
	a[0]->b = 0;
	return a[0];
}

Num *
fngcd(int, Num **a)
{
	a[0] = nummod(a[0]);
	a[0]->b = basemax(a[0]->b, a[1]->b);
	mpextendedgcd(a[0], a[1], a[0], nil, nil);
	return a[0];
}

Num *
fnrand(int, Num **a)
{
	Num *n;

	n = numalloc();
	n->b = a[0]->b;
	mpnrand(a[0], genrandom, n);
	numdecref(a[0]);
	return n;
}

Num *
fnminv(int, Num **a)
{
	mpint *x;

	a[0] = nummod(a[0]);
	x = mpnew(0);
	mpextendedgcd(a[0], a[1], x, a[0], nil);
	if(mpcmp(x, mpone) != 0)
		error("no modular inverse");
	else
		mpmod(a[0], a[1], a[0]);
	mpfree(x);
	numdecref(a[1]);
	return a[0];
}

Num *
fnrev(int, Num **a)
{
	mpdigit v, m;
	int i, j, n;
	
	if(toint(a[1], &n, 1)){
		numdecref(a[0]);
		numdecref(a[1]);
		return nil;
	}
	a[0] = nummod(a[0]);
	mptrunc(a[0], n, a[0]);
	for(i = 0; i < a[0]->top; i++){
		v = a[0]->p[i];
		m = -1;
		for(j = sizeof(mpdigit) * 8; j >>= 1; ){
			m ^= m << j;
			v = v >> j & m | v << j & ~m;
		}
		a[0]->p[i] = v;
	}
	for(i = 0; i < a[0]->top / 2; i++){
		v = a[0]->p[i];
		a[0]->p[i] = a[0]->p[a[0]->top - 1 - i];
		a[0]->p[a[0]->top - 1 - i] = v;
	}
	mpleft(a[0], n - a[0]->top * sizeof(mpdigit) * 8, a[0]);
	numdecref(a[1]);
	return a[0];
}

Num *
fncat(int n, Num **a)
{
	int i, w;
	Num *r;

	if(n % 2 != 0){
		error("cat: odd number of arguments");
		i = 0;
	fail:
		for(; i < n; i++)
			numdecref(a[i]);
		return nil;
	}
	r = numalloc();
	for(i = 0; i < n; i += 2){
		if(toint(a[i+1], &w, 1)) goto fail;
		mpleft(r, w, r);
		if(a[i]->sign < 0 || mpsignif(a[i]) > w){
			a[i] = nummod(a[i]);
			mptrunc(a[i], w, a[i]);
		}
		r->b = basemax(r->b, a[i]->b);
		mpor(r, a[i], r);
		numdecref(a[i]);
		numdecref(a[i+1]);
	}
	return r;
}

void
main(int, char **)
{
	char buf[32];
	Keyword *kw;
	
	fmtinstall('B', mpfmt);
	
	for(kw = ops; kw->name[0] != 0; kw++)
		if(optab[kw->name[0]] == nil)
			optab[kw->name[0]] = kw;
	
	regfunc("hex", fnhex, 1);
	regfunc("dec", fndec, 1);
	regfunc("oct", fnoct, 1);
	regfunc("bin", fnbin, 1);
	regfunc("pb", fnpb, 2);
	regfunc("abs", fnabs, 1);
	regfunc("round", fnround, 2);
	regfunc("floor", fnfloor, 2);
	regfunc("ceil", fnceil, 2);
	regfunc("trunc", fntrunc, 2);
	regfunc("xtend", fnxtend, 2);
	regfunc("clog", fnclog, -1);
	regfunc("ubits", fnubits, 1);
	regfunc("sbits", fnsbits, 1);
	regfunc("nsa", fnnsa, 1);
	regfunc("gcd", fngcd, 2);
	regfunc("minv", fnminv, 2);
	regfunc("rand", fnrand, 1);
	regfunc("rev", fnrev, 2);
	regfunc("cat", fncat, -1);

	prompt = fd2path(0, buf, sizeof buf) >= 0 && strstr(buf, "/dev/cons") != nil;
	in = Bfdopen(0, OREAD);
	if(in == nil)
		sysfatal("Bfdopen: %r");
	extern void yyparse(void);
	yyparse();
	extern int yynerrs;
	exits(yynerrs ? "error" : nil);
}
