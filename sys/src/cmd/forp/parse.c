#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <mp.h>
#include "dat.h"
#include "fns.h"

Biobuf *bin;
Line line;
char lexbuf[512];
int peektok;

enum {
	TEOF = -1,
	TSYM = -2,
	TNUM = -3,
	TBIT = -4,
	TOBVIOUSLY = -5,
	TEQ = -6,
	TNEQ = -7,
	TLSH = -8,
	TRSH = -9,
	TLE = -10,
	TGE = -11,
	TLAND = -12,
	TLOR = -13,
	TASSUME = -14,
	TIMP = -15,
	TEQV = -16,
	TSIGNED = -17,
};

typedef struct Keyword Keyword;
typedef struct Oper Oper;
struct Keyword {
	char *name;
	int tok;
};
/* both tables must be sorted */
static Keyword kwtab[] = {
	"assume", TASSUME,
	"bit", TBIT,
	"obviously", TOBVIOUSLY,
	"signed", TSIGNED,
};
/* <=> is implemented through a hack below */
static Keyword koptab[] = {
	"!=", TNEQ,
	"&&", TLAND,
	"<<", TLSH,
	"<=", TLE,
	"==", TEQ,
	"=>", TIMP,
	">=", TGE,
	">>", TRSH,
	"||", TLOR,
};
static Keyword *kwjmp[128];
static Keyword *kopjmp[128];
struct Oper {
	int tok;
	int type;
	int pred;
	char *str;
};
#define MAXPREC 15
static Oper optab[] = {
	'*', OPMUL, 14, "*",
	'/', OPDIV, 14, "/",
	'%', OPMOD, 14, "%",
	'+', OPADD, 13, "+",
	'-', OPSUB, 13, "-",
	TLSH, OPLSH, 12, "<<",
	TRSH, OPRSH, 12, ">>",
	'<', OPLT, 11, "<",
	TLE, OPLE, 11, "<=",
	'>', OPGT, 11, ">",
	TGE, OPGE, 11, ">=",
	TEQ, OPEQ, 10, "==",
	TNEQ, OPNEQ, 10, "!=",
	'&', OPAND, 9, "&",
	'^', OPXOR, 8, "^",
	'|', OPOR, 7, "|",
	TLAND, OPLAND, 6, "&&",
	TLOR, OPLOR, 5, "||",
	TEQV, OPEQV, 4, "<=>",
	TIMP, OPIMP, 4, "=>",
	/* ?: */
	'=', OPASS, 2, "=",
	',', OPCOMMA, 1, ",",
	-1, OPNOT, MAXPREC, "!",
	-1, OPCOM, MAXPREC, "~",
	-1, OPNEG, MAXPREC, "-",
};

void
error(Line *l, char *msg, ...)
{
	char buf[256];
	Fmt f;
	va_list va;

	if(l == nil) l = &line;
	fmtfdinit(&f, 2, buf, sizeof(buf));
	fmtprint(&f, "%s:%d: ", l->filen, l->lineno);
	va_start(va, msg);
	fmtvprint(&f, msg, va);
	va_end(va);
	fmtrune(&f, '\n');
	fmtfdflush(&f);
	exits("error");
}

static int
tokfmt(Fmt *f)
{
	int t;
	Keyword *k;
	
	t = va_arg(f->args, int);
	if(t >= ' ' && t < 0x7f) return fmtprint(f, "%c", t);
	for(k = kwtab; k < kwtab + nelem(kwtab); k++)
		if(k->tok == t)
			return fmtprint(f, "%s", k->name);
	for(k = koptab; k < koptab + nelem(koptab); k++)
		if(k->tok == t)
			return fmtprint(f, "%s", k->name);
	switch(t){
	case TSYM: return fmtprint(f, "TSYM"); break;
	case TNUM: return fmtprint(f, "TNUM"); break;
	case TEOF: return fmtprint(f, "eof"); break;
	default: return fmtprint(f, "%d", t); break;
	}
}

static int
exprfmt(Fmt *f)
{
	Node *n;
	Oper *o;
	int w;
	
	n = va_arg(f->args, Node *);
	if(n == nil) return fmtprint(f, "nil");
	switch(n->type){
	case ASTSYM: return fmtprint(f, "%s", n->sym->name);
	case ASTBIN:
		for(o = optab; o < optab + nelem(optab); o++)
			if(o->type == n->op)
				break;
		if(o == optab + nelem(optab)) return fmtprint(f, "[unknown operation %O]", n->op);
		w = f->width;
		if(w > o->pred) fmtrune(f, '(');
		fmtprint(f, "%*ε %s %*ε", o->pred, n->n1, o->str, o->pred + 1, n->n2);
		if(w > o->pred) fmtrune(f, ')');
		return 0;
	case ASTNUM: return fmtprint(f, "0x%B", n->num);
	default: return fmtprint(f, "???(%α)", n->type);
	}
}

static int
issymchar(int c)
{
	return c >= 0 && (isalnum(c) || c == '_' || c >= 0x80);
}

static int
lex(void)
{
	int c, d;
	char *p;
	Keyword *kw;

	if(peektok != 0){
		c = peektok;
		peektok = 0;
		return c;
	}
loop:
	do{
		c = Bgetc(bin);
		if(c == '\n') line.lineno++;
	}while(c >= 0 && isspace(c));
	if(c < 0) return TEOF;
	if(c == '/'){
		c = Bgetc(bin);
		if(c == '/'){
			do
				c = Bgetc(bin);
			while(c >= 0 && c != '\n');
			if(c < 0) return TEOF;
			line.lineno++;
			goto loop;
		}else if(c == '*'){
		s0:
			c = Bgetc(bin);
			if(c != '*') goto s0;
		s1:
			c = Bgetc(bin);
			if(c == '*') goto s1;
			if(c != '/') goto s0;
			goto loop;
		}else{
			Bungetc(bin);
			return '/';
		}
	}
	if(isdigit(c)){
		p = lexbuf;
		*p++ = c;
		while(c = Bgetc(bin), issymchar(c))
			if(p < lexbuf + sizeof(lexbuf) - 1)
				*p++ = c;
		Bungetc(bin);
		*p = 0;
		strtol(lexbuf, &p, 0);
		if(p == lexbuf || *p != 0)
			error(nil, "invalid number %q", lexbuf);
		return TNUM;
	}
	if(issymchar(c)){
		p = lexbuf;
		*p++ = c;
		while(c = Bgetc(bin), issymchar(c))
			if(p < lexbuf + sizeof(lexbuf) - 1)
				*p++ = c;
		Bungetc(bin);
		*p = 0;
		c = lexbuf[0];
		if((signed char)c>= 0 && (kw = kwjmp[c], kw != nil))
			for(; kw < kwtab + nelem(kwtab) && kw->name[0] == c; kw++)
				if(strcmp(lexbuf, kw->name) == 0)
					return kw->tok;
		return TSYM;
	}
	if(kw = kopjmp[c], kw != nil){
		d = Bgetc(bin);
		for(; kw < koptab + nelem(koptab) && kw->name[0] == c; kw++)
			if(kw->name[1] == d){
				if(kw->tok == TLE){
					c = Bgetc(bin);
					if(c == '>')
						return TEQV;
					Bungetc(bin);
				}
				return kw->tok;
			}
		Bungetc(bin);
	}
	return c;
}

static void
superman(int t)
{
	assert(peektok == 0);
	peektok = t;
}

static int
peek(void)
{
	if(peektok != 0) return peektok;
	return peektok = lex();
}

static void
expect(int t)
{
	int s;
	
	s = lex();
	if(t != s)
		error(nil, "expected %t, got %t", t, s);
}

static int
got(int t)
{
	return peek() == t && (lex(), 1);
}

static Node *
expr(int level)
{
	Node *a, *b, *c;
	Oper *op;
	Symbol *s;
	mpint *num;
	int t;
	
	if(level == MAXPREC+2){
		switch(t = lex()){
		case '(':
			a = expr(0);
			expect(')');
			return a;
		case TSYM:
			s = symget(lexbuf);
			switch(s->type){
			case SYMNONE:
				error(nil, "%#q undefined", s->name);
			default:
				error(nil, "%#q symbol type error", s->name);
			case SYMBITS:
				break;
			}
			return node(ASTSYM, s);
		case TNUM:
			num = strtomp(lexbuf, nil, 0, nil);
			return node(ASTNUM, num);
		default:
			error(nil, "unexpected %t", t);
		}
	}else if(level == MAXPREC+1){
		a = expr(level + 1);
		if(got('[')){
			b = expr(0);
			if(got(':'))
				c = expr(0);
			else
				c = nil;
			expect(']');
			a = node(ASTIDX, a, b, c);
		}
		return a;
	}else if(level == MAXPREC){
		switch(t = lex()){
		case '~': return node(ASTUN, OPCOM, expr(level));
		case '!': return node(ASTUN, OPNOT, expr(level));
		case '+': return expr(level);
		case '-': return node(ASTUN, OPNEG, expr(level));
		default: superman(t); return expr(level+1); break;
		}
	}else if(level == 3){
		a = expr(level + 1);
		if(got('?')){
			b = expr(level);
			expect(':');
			c = expr(level);
			a = node(ASTTERN, a, b, c);
		}
		return a;
	}
	a = expr(level+1);
	for(;;){
		t = peek();
		for(op = optab; op < optab + nelem(optab); op++)
			if(op->tok == t && op->pred >= level)
				break;
		if(op == optab+nelem(optab)) return a;
		lex();
		a = node(ASTBIN, op->type, a, expr(level+1));
	}
}

static void
vardecl(void)
{
	Symbol *s;
	int l, flags;

	flags = 0;
	for(;;)
		switch(l = lex()){
		case TBIT: if((flags & 1) != 0) goto err; flags |= 1; break;
		case TSIGNED: if((flags & 2) != 0) goto err; flags |= 2; break;
		default: superman(l); goto out;
		}
out:
	do{
		expect(TSYM);
		s = symget(lexbuf);
		if(s->type != SYMNONE) error(nil, "%#q redefined", s->name);
		s->type = SYMBITS;
		if((flags & 2) != 0)
			s->flags |= SYMFSIGNED;
		s->size = 1;
		if(got('[')){
			expect(TNUM);
			s->type = SYMBITS;
			s->size = strtol(lexbuf, nil, 0);
			expect(']');
		}
		s->vars = emalloc(sizeof(int) * s->size);
	}while(got(','));
	expect(';');
	return;
err:	error(nil, "syntax error");
}

static int
statement(void)
{
	Node *n;
	int t;

	switch(t=peek()){
	case TEOF:
		return 0;
	case TBIT:
	case TSIGNED:
		vardecl();
		break;
	case TASSUME:
	case TOBVIOUSLY:
		lex();
		n = expr(0);
		expect(';');
		convert(n, -1);
		if(t == TASSUME)
			assume(n);
		else
			obviously(n);
		break;
	case ';':
		lex();
		break;
	default:
		n = expr(0);
		convert(n, -1);
		expect(';');
	}
	return 1;
}

void
parsinit(void)
{
	Keyword *k;

	fmtinstall('t', tokfmt);
	fmtinstall(L'ε', exprfmt);
	for(k = kwtab; k < kwtab + nelem(kwtab); k++)
		if(kwjmp[k->name[0]] == nil)
			kwjmp[k->name[0]] = k;
	for(k = koptab; k < koptab + nelem(koptab); k++)
		if(kopjmp[k->name[0]] == nil)
			kopjmp[k->name[0]] = k;
}

void
parse(char *fn)
{
	if(fn == nil){
		bin = Bfdopen(0, OREAD);
		line.filen = "<stdin>";
	}else{
		bin = Bopen(fn, OREAD);
		line.filen = strdup(fn);
	}
	if(bin == nil) sysfatal("open: %r");
	line.lineno = 1;
	while(statement())
		;
}
