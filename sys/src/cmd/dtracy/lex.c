#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <dtracy.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"
#include "y.tab.h"

char *str, *strp, *stre;
int lineno = 1;
int errors;

typedef struct Keyword Keyword;
struct Keyword {
	char *name;
	int tok;
};
/* both tables must be sorted */
Keyword kwtab[] = {
	"if", TIF,
	"print", TPRINT,
	"printf", TPRINTF,
	"s16", TS16,
	"s32", TS32,
	"s64", TS64,
	"s8", TS8,
	"string", TSTRING,
	"u16", TU16,
	"u32", TU32,
	"u64", TU64,
	"u8", TU8,
};
Keyword optab[] = {
	"!=", TNE,
	"&&", TAND,
	"<<", TLSL,
	"<=", TLE,
	"==", TEQ,
	">=", TGE,
	">>", TLSR,
	"||", TOR,
};
Keyword *kwchar[128], *opchar[128];

void
lexinit(void)
{
	Keyword *kw;
	
	for(kw = kwtab; kw < kwtab + nelem(kwtab); kw++)
		if(kwchar[*kw->name] == nil)
			kwchar[*kw->name] = kw;
	for(kw = optab; kw < optab + nelem(optab); kw++)
		if(opchar[*kw->name] == nil)
			opchar[*kw->name] = kw;
}

void
lexstring(char *s)
{
	str = strp = s;
	stre = str + strlen(str);
}

void
error(char *fmt, ...)
{
	Fmt f;
	char buf[128];
	va_list va;
	
	fmtfdinit(&f, 2, buf, sizeof(buf));
	fmtprint(&f, "%d ", lineno);
	va_start(va, fmt);
	fmtvprint(&f, fmt, va);
	fmtrune(&f, '\n');
	va_end(va);
	fmtfdflush(&f);
	errors++;
}

void
yyerror(char *msg)
{
	error("%s", msg);
}

static int
getch(void)
{
	if(strp >= stre){
		strp++;
		return -1;
	}
	return *strp++;
}

static void
ungetch(void)
{
	assert(strp > str);
	strp--;
}

int
yylex(void)
{
	int ch;
	static char buf[512];
	char *p;
	Keyword *kw;
	u64int v;

again:
	while(ch = getch(), ch >= 0 && isspace(ch)){
		if(ch == '\n')
			lineno++;
	}
	if(ch < 0)
		return -1;
	if(ch == '/'){
		ch = getch();
		if(ch == '/'){
			while(ch = getch(), ch >= 0 && ch != '\n')
				;
			if(ch == '\n')
				lineno++;
			goto again;
		}
		if(ch == '*'){
		s1:
			ch = getch();
			if(ch < 0) return -1;
			if(ch == '\n') lineno++;
			if(ch != '*') goto s1;
		s2:
			ch = getch();
			if(ch < 0) return -1;
			if(ch == '\n') lineno++;
			if(ch == '*') goto s2;
			if(ch != '/') goto s1;
			goto again;
		}
		ungetch();
		return '/';
	}
	if(isalnum(ch) || ch == '_' || ch >= 0x80 || ch == ':'){
		p = buf;
		*p++ = ch;
		while(ch = getch(), isalnum(ch) || ch == '_' || ch >= 0x80 || ch == ':')
			if(p < buf + sizeof(buf) - 1)
				*p++ = ch;
		*p = 0;
		ungetch();
		v = strtoull(buf, &p, 0);
		if(p != buf && *p == 0){
			yylval.num = v;
			return TNUM;
		}
		if(strcmp(buf, ":") == 0)
			return ':';
		if((uchar)buf[0] < 0x80 && kwchar[buf[0]] != nil)
			for(kw = kwchar[buf[0]]; kw < kwtab + nelem(kwtab) && kw->name[0] == buf[0]; kw++)
				if(strcmp(kw->name, buf) == 0)
					return kw->tok;
		yylval.sym = getsym(buf);
		return TSYM;
	}
	if(ch == '"'){
		p = buf;
		while(ch = getch(), ch >= 0 && ch != '"'){
			if(ch == '\n')
				error("unterminated string");
			if(ch == '\\')
				switch(ch = getch()){
				case 'n': ch = '\n'; break;
				case 'r': ch = '\r'; break;
				case 't': ch = '\t'; break;
				case 'v': ch = '\v'; break;
				case 'b': ch = '\b'; break;
				case 'a': ch = '\a'; break;
				case '"': case '\\': break;
				default: error("unknown escape code \\%c", ch);
				}
			if(p < buf + sizeof(buf) - 1)
				*p++ = ch;
		}
		if(ch < 0) error("unterminated string");
		*p = 0;
		yylval.str = strdup(buf);
		return TSTR;
	}
	if(opchar[ch] != nil){
		buf[0] = ch;
		buf[1] = getch();
		for(kw = opchar[buf[0]]; kw < optab + nelem(optab) && kw->name[0] == buf[0]; kw++)
			if(buf[1] == kw->name[1]){
				buf[2] = getch();
				buf[3] = 0;
				if(kw + 1 < optab + nelem(optab) && strcmp(kw[1].name, buf) == 0)
					return kw[1].tok;
				ungetch();
				return kw->tok;
			}
		ungetch();
	}
	return ch;
}

int
nodetfmt(Fmt *f)
{
	int t;
	static char *nodestr[] = {
		[OINVAL] "OINVAL",
		[OBIN] "OBIN",
		[OLNOT] "OLNOT",
		[OSYM] "OSYM",
		[ONUM] "ONUM",
		[OSTR] "OSTR",
		[OTERN] "OTERN",
		[ORECORD] "ORECORD",
		[OCAST] "OCAST",
	};
	
	t = va_arg(f->args, int);
	if(t >= nelem(nodestr) || nodestr[t] == nil)
		return fmtprint(f, "??? (%d)", t);
	else
		return fmtprint(f, "%s", nodestr[t]);
}

Node *
node(int type, ...)
{
	va_list va;
	Node *n;
	
	n = emalloc(sizeof(Node));
	n->type = type;
	n->line = lineno;
	va_start(va, type);
	switch(type){
	case OBIN:
		n->op = va_arg(va, int);
		n->n1 = va_arg(va, Node *);
		n->n2 = va_arg(va, Node *);
		break;
	case OLNOT:
		n->n1 = va_arg(va, Node *);
		break;
	case OSYM:
		n->sym = va_arg(va, Symbol *);
		break;
	case ONUM:
		n->num = va_arg(va, s64int);
		break;
	case OTERN:
		n->n1 = va_arg(va, Node *);
		n->n2 = va_arg(va, Node *);
		n->n3 = va_arg(va, Node *);
		break;
	case ORECORD:
		n->n1 = va_arg(va, Node *);
		break;
	case OCAST:
		n->typ = va_arg(va, Type *);
		n->n1 = va_arg(va, Node *);
		break;
	case OSTR:
		n->str = va_arg(va, char *);
		break;
	default:
		sysfatal("node: unknown type %α", type);
	}
	va_end(va);
	return n;
}

SymTab globals;

static u64int
hash(char *s)
{
	u64int h;
	
	h = 0xcbf29ce484222325ULL;
	for(; *s != 0; s++){
		h ^= *s;
		h *= 0x100000001b3ULL;
	}
	return h;
}

Symbol *
getsym(char *name)
{
	u64int h;
	Symbol **sp, *s;
	
	h = hash(name);
	for(sp = &globals.sym[h % SYMHASH]; s = *sp, s != nil; sp = &s->next)
		if(strcmp(s->name, name) == 0)
			return s;
	*sp = s = emalloc(sizeof(Symbol));
	s->name = strdup(name);
	return s;
}

int
typetfmt(Fmt *f)
{
	int t;
	static char *tstr[] = {
		[TYPINVAL] "TYPINVAL",
		[TYPINT] "TYPINT",
		[TYPPTR] "TYPPTR",
		[TYPSTRING] "TYPSTRING",
	};
	
	t = va_arg(f->args, int);
	if(t >= nelem(tstr) || tstr[t] == nil)
		return fmtprint(f, "??? (%d)", t);
	else
		return fmtprint(f, "%s", tstr[t]);
}

int
typefmt(Fmt *f)
{
	Type *t;
	
	t = va_arg(f->args, Type *);
	switch(t->type){
	case TYPINT: return fmtprint(f, "%c%d", t->sign ? 's' : 'u', t->size * 8);
	case TYPSTRING: return fmtprint(f, "string");
	case TYPPTR: return fmtprint(f, "%τ*", t->ref);
	default: return fmtprint(f, "%t", t->type);
	}
}

static Type typu8 = {.type TYPINT, .size 1, .sign 0};
static Type typs8 = {.type TYPINT, .size 1, .sign 1};
static Type typu16 = {.type TYPINT, .size 2, .sign 0};
static Type typs16 = {.type TYPINT, .size 2, .sign 1};
static Type typu32 = {.type TYPINT, .size 4, .sign 0};
static Type typs32 = {.type TYPINT, .size 4, .sign 1};
static Type typu64 = {.type TYPINT, .size 8, .sign 0};
static Type typs64 = {.type TYPINT, .size 8, .sign 1};
static Type typstr = {.type TYPSTRING, .size DTSTRMAX };
static Type *typereg;

static Type *
mkptr(Type *t)
{
	Type *s;
	
	for(s = typereg; s != nil; s = s->typenext)
		if(s->type == TYPPTR && s->ref == t)
			return s;
	s = emalloc(sizeof(Type));
	s->type = TYPPTR;
	s->ref = t;
	return s;
}

Type *
type(int typ, ...)
{
	int size, sign;
	va_list va;
	
	va_start(va, typ);
	switch(typ){
	case TYPINT:
		size = va_arg(va, int);
		sign = va_arg(va, int);
		switch(size << 4 | sign){
		case 0x10: return &typu8;
		case 0x11: return &typs8;
		case 0x20: return &typu16;
		case 0x21: return &typs16;
		case 0x40: return &typu32;
		case 0x41: return &typs32;
		case 0x80: return &typu64;
		case 0x81: return &typs64;
		default: sysfatal("type: invalid (size,sign) = (%d,%d)\n", size, sign);
		}
	case TYPSTRING: return &typstr;
	case TYPPTR: return mkptr(va_arg(va, Type *));
	default: sysfatal("type: unknown %t", typ);
	}
}
