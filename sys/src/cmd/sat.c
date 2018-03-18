#include <u.h>
#include <libc.h>
#include <sat.h>
#include <bio.h>
#include <ctype.h>

typedef struct Trie Trie;
typedef struct Var Var;
Biobuf *bin, *bout;

struct Trie {
	u64int hash;
	Trie *c[2];
	uchar l;
};
struct Var {
	Trie trie;
	char *name;
	int n;
	Var *next;
};
Trie *root;
Var *vlist, **vlistp = &vlist;
int varctr;

static void*
emalloc(ulong n)
{
	void *v;
	
	v = malloc(n);
	if(v == nil) sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&n));
	memset(v, 0, n);
	return v;
}

u64int
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

int
ctz(u64int d)
{
	int r;
	
	d &= -d;
	r = 0;
	if((int)d == 0) r += 32, d >>= 32;
	r += ((d & 0xffff0000) != 0) << 4;
	r += ((d & 0xff00ff00) != 0) << 3;
	r += ((d & 0xf0f0f0f0) != 0) << 2;
	r += ((d & 0xcccccccc) != 0) << 1;
	r += ((d & 0xaaaaaaaa) != 0);
	return r;
}

Trie *
trieget(u64int h)
{
	Trie *t, *s, **tp;
	u64int d;
	
	tp = &root;
	for(;;){
		t = *tp;
		if(t == nil){
			t = emalloc(sizeof(Var));
			t->hash = h;
			t->l = 64;
			*tp = t;
			return t;
		}
		d = (h ^ t->hash) << 64 - t->l >> 64 - t->l;
		if(d == 0 || t->l == 0){
			if(t->l == 64)
				return t;
			tp = &t->c[h >> t->l & 1];
			continue;
		}
		s = emalloc(sizeof(Trie));
		s->hash = h;
		s->l = ctz(d);
		s->c[t->hash >> s->l & 1] = t;
		*tp = s;
		tp = &s->c[h >> s->l & 1];
	}
}

Var *
varget(char *n)
{
	Var *v, **vp;
	
	v = (Var*) trieget(hash(n));
	if(v->name == nil){
	gotv:
		v->name = strdup(n);
		v->n = ++varctr;
		*vlistp = v;
		vlistp = &v->next;
		return v;
	}
	if(strcmp(v->name, n) == 0)
		return v;
	for(vp = (Var**)&v->trie.c[0]; (v = *vp) != nil; vp = (Var**)&v->trie.c[0])
		if(strcmp(v->name, n) == 0)
			return v;
	v = emalloc(sizeof(Var));
	*vp = v;
	goto gotv;
}

static int
isvarchar(int c)
{
	return isalnum(c) || c == '_' || c == '-' || c >= 0x80;
}

char lexbuf[512];
enum { TEOF = -1, TVAR = -2 };
int peektok;

int
lex(void)
{
	int c;
	char *p;
	
	if(peektok != 0){
		c = peektok;
		peektok = 0;
		return c;
	}
	do
		c = Bgetc(bin);
	while(c >= 0 && isspace(c) && c != '\n');
	if(c == '#'){
		do
			c = Bgetc(bin);
		while(c >= 0 && c != '\n');
		if(c < 0) return TEOF;
		c = Bgetc(bin);
	}
	if(c < 0) return TEOF;
	if(isvarchar(c)){
		p = lexbuf;
		*p++ = c;
		while(c = Bgetc(bin), c >= 0 && isvarchar(c))
			if(p < lexbuf + sizeof(lexbuf) - 1)
				*p++ = c;
		*p = 0;
		Bungetc(bin);
		return TVAR;
	}
	return c;
}

void
superman(int t)
{
	peektok = t;
}

int
clause(SATSolve *s)
{
	int t;
	int n;
	int not, min, max;
	char *p;
	static int *clbuf, nclbuf;
	
	n = 0;
	not = 1;
	min = -1;
	max = -1;
	for(;;)
		switch(t = lex()){
		case '[':
			t = lex();
			if(t == TVAR){
				min = strtol(lexbuf, &p, 10);
				if(p == lexbuf || *p != 0 || min < 0) goto syntax;
				t = lex();
			}else
				min = 0;
			if(t == ']'){
				max = min;
				break;
			}
			if(t != ',') goto syntax;
			t = lex();
			if(t == TVAR){
				max = strtol(lexbuf, &p, 10);
				if(p == lexbuf || *p != 0 || max < 0) goto syntax;
				t = lex();
			}else
				max = -1;
			if(t != ']') goto syntax;
			break;
		case TVAR:
			if(n == nclbuf){
				clbuf = realloc(clbuf, (nclbuf + 32) * sizeof(int));
				nclbuf += 32;
			}
			clbuf[n++] = not * varget(lexbuf)->n;
			not = 1;
			break;
		case '!':
			not *= -1;
			break;
		case TEOF:
		case '\n':
		case ';':
			goto out;
		default:
			sysfatal("unexpected token %d", t);
		}
out:
	if(n != 0)
		if(min >= 0)
			satrange1(s, clbuf, n, min, max< 0 ? n : max);
		else
			satadd1(s, clbuf, n);
	return t != TEOF;
syntax:
	sysfatal("syntax error");
	return 0;
}

int oneflag, multiflag;

void
usage(void)
{
	fprint(2, "usage: %s [-1m] [file]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	SATSolve *s;
	Var *v;
	
	ARGBEGIN {
	case '1': oneflag++; break;
	case 'm': multiflag++; break;
	default: usage();
	} ARGEND;
	
	switch(argc){
	case 0:
		bin = Bfdopen(0, OREAD);
		break;
	case 1:
		bin = Bopen(argv[0], OREAD);
		break;
	default: usage();
	}
	if(bin == nil) sysfatal("Bopen: %r");
	s = satnew();
	if(s == nil) sysfatal("satnew: %r");
	while(clause(s))
		;
	if(multiflag){	
		bout = Bfdopen(1, OWRITE);
		while(satmore(s) > 0){
			for(v = vlist; v != nil; v = v->next)
				if(satval(s, v->n) > 0)
					Bprint(bout, "%s ", v->name);
			Bprint(bout, "\n");
			Bflush(bout);
		}
	}else if(oneflag){
		if(satsolve(s) == 0)
			exits("unsat");
		bout = Bfdopen(1, OWRITE);
		for(v = vlist; v != nil; v = v->next)
			if(satval(s, v->n) > 0)
				Bprint(bout, "%s ", v->name);
		Bprint(bout, "\n");
		Bflush(bout);
	}else{
		if(satsolve(s) == 0)
			exits("unsat");
		bout = Bfdopen(1, OWRITE);
		for(v = vlist; v != nil; v = v->next)
			Bprint(bout, "%s %d\n", v->name, satval(s, v->n));
		Bflush(bout);
	}
	exits(nil);
}
