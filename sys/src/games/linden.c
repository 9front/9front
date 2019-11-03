#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>

Biobuf *Bin;

typedef struct Symbol Symbol;
typedef struct SString SString;

enum { TSTRING = -2 };

struct Symbol {
	Rune name;
	SString *rule;
	char *output;
	Symbol *next;
};

struct SString {
	int n;
	Symbol **d;
};
#pragma varargck type "σ" SString*

Symbol *syms;
SString *sstring;
char strbuf[1024];

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

void
sstringaddsym(SString *a, Symbol *b)
{
	a->d = realloc(a->d, (a->n + 1) * sizeof(Symbol *));
	a->d[a->n++] = b;
}

void
sstringappend(SString *a, SString *b)
{
	a->d = realloc(a->d, (a->n + b->n) * sizeof(Symbol *));
	memcpy(a->d + a->n, b->d, b->n * sizeof(Symbol *));
	a->n += b->n;
}

Symbol *
getsym(Rune name)
{
	Symbol **sp;
	
	for(sp = &syms; *sp != nil; sp = &(*sp)->next)
		if(name == (*sp)->name)
			return *sp;
	*sp = emalloc(sizeof(Symbol));
	(*sp)->name = name;
	return *sp;
}

int peektok = -1;

int
lex(void)
{
	int c;
	char *p;
	
	if(peektok >= 0){
		c = peektok;
		peektok = -1;
		return c;
	}
	do
		c = Bgetrune(Bin);
	while(c >= 0 && c < 0x80 && isspace(c) && c != '\n');
	if(c == '\''){
		p = strbuf;
		for(;;){
			c = Bgetc(Bin);
			if(c == '\'') break;
			if(p < strbuf + sizeof(strbuf) - 1)
				*p++ = c;
		}
		*p = 0;
		return TSTRING;
	}
	return c;
}

int
peek(void)
{
	if(peektok >= 0) return peektok;
	return peektok = lex();
}

SString *
symstring(void)
{
	int c;
	SString *r;
	
	r = emalloc(sizeof(SString));
	for(;;){
		c = peek();
		if(c == '\n' || c == ':')
			break;
		lex();
		r->d = realloc(r->d, (r->n + 1) * sizeof(Symbol *));
		r->d[r->n++] = getsym(c);
	}
	return r;
}

int
fmtsstring(Fmt *f)
{
	SString *s;
	int i;
	
	s = va_arg(f->args, SString *);
	for(i = 0; i < s->n; i++)
		fmtprint(f, "%C", s->d[i]->name);
	return 0;
}

void
syntax(void)
{
	sysfatal("syntax error");
}

void
parse(void)
{
	Symbol *s;
	int c;

	sstring = symstring();
	while(peek() > 0){
		if(peek() == '\n') {lex(); continue;}
		if(peek() == ':') syntax();
		s = getsym(lex());
		c = lex();
		if(c == ':')
			s->rule = symstring();
		else if(c == '='){
			if(lex() != TSTRING) syntax();
			s->output = strdup(strbuf);
		}else
			syntax();
		c = lex();
		if(c != -1 && c != '\n') syntax();
	}
}

SString *
iterate(SString *in)
{
	SString *r;
	int i;
	
	r = emalloc(sizeof(SString));
	for(i = 0; i < in->n; i++)
		if(in->d[i]->rule == nil)
			sstringaddsym(r, in->d[i]);
		else
			sstringappend(r, in->d[i]->rule);
	return r;
}

void
main()
{
	int i, j;

	fmtinstall(L'σ', fmtsstring);
	Bin = Bfdopen(0, OREAD);
	if(Bin == nil) sysfatal("Bfdopen: %r");
	parse();
	for(j = 0; j < 9; j++){
		for(i = 0; i < sstring->n; i++)
			if(sstring->d[i]->output != nil)
				print("%s\n", sstring->d[i]->output);
		print("end\n");
		sstring = iterate(sstring);
	}
}
