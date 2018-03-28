#include <u.h>
#include <libc.h>
#include <mp.h>
#include "dat.h"
#include "fns.h"

char *astnames[] = {
	[ASTINVAL] "ASTINVAL",
	[ASTSYM] "ASTSYM",
	[ASTNUM] "ASTNUM",
	[ASTBIN] "ASTBIN",
	[ASTUN] "ASTUN",
	[ASTIDX] "ASTIDX",
	[ASTTERN] "ASTTERN",
	[ASTTEMP] "ASTTEMP",
};

char *opnames[] = {
	[OPABS] "OPABS",
	[OPADD] "OPADD",
	[OPAND] "OPAND",
	[OPASS] "OPASS",
	[OPCOM] "OPCOM",
	[OPCOMMA] "OPCOMMA",
	[OPDIV] "OPDIV",
	[OPEQ] "OPEQ",
	[OPEQV] "OPEQV",
	[OPGE] "OPGE",
	[OPGT] "OPGT",
	[OPIMP] "OPIMP",
	[OPINVAL] "OPINVAL",
	[OPLAND] "OPLAND",
	[OPLE] "OPLE",
	[OPLOR] "OPLOR",
	[OPLSH] "OPLSH",
	[OPLT] "OPLT",
	[OPMOD] "OPMOD",
	[OPMUL] "OPMUL",
	[OPNEG] "OPNEG",
	[OPNEQ] "OPNEQ",
	[OPNOT] "OPNOT",
	[OPOR] "OPOR",
	[OPRSH] "OPRSH",
	[OPSUB] "OPSUB",
	[OPXOR] "OPXOR",
};

Trie *root;
Symbol *syms, **lastsymp = &syms;

void *
emalloc(ulong sz)
{
	void *v;
	
	v = malloc(sz);
	if(v == nil) sysfatal("malloc: %r");
	setmalloctag(v, getmalloctag(&sz));
	memset(v, 0, sz);
	return v;
}

void *
erealloc(void *v, ulong sz)
{
	v = realloc(v, sz);
	if(v == nil) sysfatal("realloc: %r");
	setrealloctag(v, getmalloctag(&v));
	return v;
}

static int
astfmt(Fmt *f)
{
	int t;
	
	t = va_arg(f->args, int);
	if(t >= nelem(astnames) || astnames[t] == nil)
		return fmtprint(f, "%d", t);
	return fmtprint(f, "%s", astnames[t]);
}

static int
opfmt(Fmt *f)
{
	int t;
	
	t = va_arg(f->args, int);
	if(t >= nelem(opnames) || opnames[t] == nil)
		return fmtprint(f, "%d", t);
	return fmtprint(f, "%s", opnames[t]);
}

static int
clz(uvlong v)
{
	int n;
	
	n = 0;
	if(v >> 32 == 0) {n += 32; v <<= 32;}
	if(v >> 48 == 0) {n += 16; v <<= 16;}
	if(v >> 56 == 0) {n += 8; v <<= 8;}
	if(v >> 60 == 0) {n += 4; v <<= 4;}
	if(v >> 62 == 0) {n += 2; v <<= 2;}
	if(v >> 63 == 0) {n += 1; v <<= 1;}
	return n;
}

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

static Symbol *
trieget(uvlong hash)
{
	Trie **tp, *t, *s;
	uvlong d;
	
	tp = &root;
	for(;;){
		t = *tp;
		if(t == nil){
			t = emalloc(sizeof(Symbol));
			t->hash = hash;
			t->l = 64;
			*tp = t;
			return (Symbol *) t;
		}
		d = (hash ^ t->hash) & -(1ULL<<64 - t->l);
		if(d == 0 || t->l == 0){
			if(t->l == 64)
				return (Symbol *) t;
			tp = &t->n[hash << t->l >> 64 - TRIEB];
		}else{
			s = emalloc(sizeof(Trie));
			s->hash = hash;
			s->l = clz(d) & -TRIEB;
			s->n[t->hash << s->l >> 64 - TRIEB] = t;
			*tp = s;
			tp = &s->n[hash << s->l >> 64 - TRIEB];
		}
	}
}

Symbol *
symget(char *name)
{
	uvlong h;
	Symbol *s;
	
	h = hash(name);
	while(s = trieget(h), s->name != nil && strcmp(s->name, name) != 0)
		h++;
	if(s->name == nil){
		s->name = strdup(name);
		*lastsymp = s;
		lastsymp = &s->next;
	}
	return s;
}

void
trieprint(Trie *t, char *pref, int bits)
{
	int i;

	if(t == nil) {print("%snil\n", pref); return;}
	if(t->l == 64) {print("%s%#.8p %.*llux '%s'\n", pref, t, (t->l - bits + 3) / 4, t->hash << bits >> bits >> 64 - t->l, ((Symbol*)t)->name); return;}
	print("%s%#.8p %.*llux %d\n", pref, t, (t->l - bits + 3) / 4, t->hash << bits >> bits >> 64 - t->l, t->l);
	for(i = 0; i < (1<<TRIEB); i++){
		if(t->n[i] == nil) continue;
		print("%s%x:\n", pref, i);
		trieprint(t->n[i], smprint("\t%s", pref), t->l);
	}
}

Node *
node(int t, ...)
{
	Node *n;
	va_list va;
	extern Line line;
	
	n = emalloc(sizeof(Node));
	n->type = t;
	n->Line = line;
	va_start(va, t);
	switch(t){
	case ASTSYM:
		n->sym = va_arg(va, Symbol *);
		break;
	case ASTNUM:
		n->num = va_arg(va, mpint *);
		break;
	case ASTBIN:
		n->op = va_arg(va, int);
		n->n1 = va_arg(va, Node *);
		n->n2 = va_arg(va, Node *);
		break;
	case ASTUN:
		n->op = va_arg(va, int);
		n->n1 = va_arg(va, Node *);
		break;
	case ASTIDX:
	case ASTTERN:
		n->n1 = va_arg(va, Node *);
		n->n2 = va_arg(va, Node *);
		n->n3 = va_arg(va, Node *);
		break;
	case ASTTEMP:
		break;
	default:
		sysfatal("node: unknown type %α", t);
	}
	va_end(va);
	return n;
}

static int
triedesc(Trie *t, int(*f)(Symbol *, va_list), va_list va)
{
	int i, rc;
	
	if(t == nil) return 0;
	if(t->l == 64) return f((Symbol *) t, va);
	rc = 0;
	for(i = 0; i < 1<<TRIEB; i++)
		rc += triedesc(t->n[i], f, va);
	return rc;
}

void
miscinit(void)
{
	fmtinstall(L'α', astfmt);
	fmtinstall(L'O', opfmt);
}
