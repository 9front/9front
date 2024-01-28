#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <dtracy.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

/* this contains the code to prepare the kernel data structures and to parse records */

Clause *clause;
Clause **clauses;
int nclauses;

/* we could just rely on the types in the expression tree but i'm paranoid */
typedef struct Val Val;
struct Val {
	enum {
		VALINT,
		VALSTR,
	} type;
	union {
		vlong v;
		char *s;
	};
};

Val
mkval(int type, ...)
{
	Val r;
	va_list va;
	
	r.type = type;
	va_start(va, type);
	switch(type){
	case VALINT: r.v = va_arg(va, uvlong); break;
	case VALSTR: r.s = va_arg(va, char*); break;
	}
	va_end(va);
	return r;
}

static char *
insertstars(char *n)
{
	Fmt f;
	int partlen;
	
	fmtstrinit(&f);
	partlen = 0;
	for(; *n != 0; n++){
		if(*n == ':'){
			if(partlen == 0)
				fmtrune(&f, '*');
			partlen = 0;
		}else
			partlen++;
		fmtrune(&f, *n);
	}
	if(partlen == 0)
		fmtrune(&f, '*');
	return fmtstrflush(&f);
}

void
clausebegin(void)
{
	clause = emalloc(sizeof(Clause));
	clause->id = nclauses;
}

void
addprobe(char *s)
{
	clause->probs = erealloc(clause->probs, sizeof(char *) * (clause->nprob + 1));
	clause->probs[clause->nprob++] = insertstars(s);
}

static char *aggtypes[] = {
	[AGGCNT] "count",
	[AGGMIN] "min",
	[AGGMAX] "max",
	[AGGSUM] "sum",
	[AGGAVG] "avg",
	[AGGSTD] "std",
};

int
aggtype(Symbol *s)
{
	int i;

	for(i = 0; i < nelem(aggtypes); i++)
		if(strcmp(s->name, aggtypes[i]) == 0)
			return i;
	error("%s unknown aggregation type", s->name);
	return 0;
}

void
addstat(int type, ...)
{
	Stat *s;
	va_list va;

	clause->stats = erealloc(clause->stats, sizeof(Stat) * (clause->nstats + 1));
	s = &clause->stats[clause->nstats++];
	memset(s, 0, sizeof(Stat));
	s->type = type;
	va_start(va, type);
	switch(type){
	case STATEXPR:
		s->n = va_arg(va, Node *);
		break;
	case STATPRINT:
	case STATPRINTF:
		break;
	case STATAGG:
		s->agg.name = va_arg(va, Symbol *);
		s->agg.key = va_arg(va, Node *);
		s->agg.type = aggtype(va_arg(va, Symbol *));
		s->agg.value = va_arg(va, Node *);
		if(s->agg.type == AGGCNT){
			if(s->agg.value != nil)
				error("too many arguments for count()");
		}else{
			if(s->agg.value == nil)
				error("need argument for %s()", aggtypes[s->agg.type]);
		}
		break;
	default:
		sysfatal("addstat: unknown type %d", type);
	}
	va_end(va);
}

void
addarg(Node *n)
{
	Stat *s;
	
	assert(clause->nstats > 0);
	s = &clause->stats[clause->nstats - 1];
	s->arg = erealloc(s->arg, sizeof(Node *) * (s->narg + 1));
	s->arg[s->narg++] = n;
}

void
clauseend(void)
{
	clauses = erealloc(clauses, sizeof(Clause) * (nclauses + 1));
	clauses[nclauses++] = clause;
}

void
actgradd(DTActGr *a, DTAct b)
{
	a->acts = erealloc(a->acts, sizeof(DTAct) * (a->nact + 1));
	a->acts[a->nact++] = b;
}

void
addpred(DTExpr *e)
{
	clause->pred = e;
}

static void
prepprintf(Node **arg, int narg, DTActGr *g, int *recoff)
{
	char *fmt;
	int n;
	Fmt f;

	if(narg <= 0) sysfatal("printf() needs an argument");
	if((*arg)->type != OSTR) sysfatal("printf() format string must be a literal");
	fmt = (*arg)->str;
	fmtstrinit(&f);
	n = 1;
	for(; *fmt != 0; fmt++){
		fmtrune(&f, *fmt);
		if(*fmt != '%')
			continue;
		fmt++;
	again:
		switch(*fmt){
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
		case 'u': case '+': case '-': case ',': case '#': case ' ': case '.':
			fmtrune(&f, *fmt);
			fmt++;
			goto again;
		case 'x': case 'X': case 'o': case 'b': case 'd':
			if(n >= narg) sysfatal("printf() too few arguments");
			if(arg[n]->typ->type != TYPINT)
				sysfatal("%d: print() %%%c with non-integer", arg[n]->line, *fmt);
			arg[n] = tracegen(arg[n], g, recoff);
			n++;
			fmtrune(&f, 'l');
			fmtrune(&f, 'l');
			fmtrune(&f, *fmt);
			break;
		case 's':
			if(n >= narg) sysfatal("printf() too few arguments");
			if(arg[n]->typ->type != TYPSTRING)
				sysfatal("%d: print() %%s with non-string", arg[n]->line);
			arg[n] = tracegen(arg[n], g, recoff);
			n++;
			fmtrune(&f, *fmt);
			break;
		case 0: sysfatal("printf() missing verb");
		default: sysfatal("printf() unknown verb %%%c", *fmt);
		}
	}
	if(n < narg) sysfatal("printf() too many arguments");
	(*arg)->str = fmtstrflush(&f);
}

int aggid;

int
allagg(Clause *c)
{
	Stat *s;

	for(s = c->stats; s < c->stats + c->nstats; s++)
		if(s->type != STATAGG)
			return 0;
	return 1;
}

DTClause *
mkdtclause(Clause *c)
{
	DTClause *d;
	Stat *s;
	int recoff, i;
	Node *n;
	
	d = emalloc(sizeof(DTClause));
	d->nprob = c->nprob;
	d->probs = c->probs;
	d->gr = emalloc(sizeof(DTActGr));
	d->gr->pred = c->pred;
	d->gr->id = c->id;
	recoff = 12;
	for(s = c->stats; s < c->stats + c->nstats; s++)
		switch(s->type){
		case STATEXPR:
			actgradd(d->gr, (DTAct){ACTTRACE, codegen(s->n), 0, noagg});
			break;
		case STATPRINT:
			for(i = 0; i < s->narg; i++)
				s->arg[i] = tracegen(s->arg[i], d->gr, &recoff);
			break;
		case STATPRINTF:
			prepprintf(s->arg, s->narg, d->gr, &recoff);
			break;
		case STATAGG: {
			DTAgg agg = {.id = s->agg.type << 28 | 1 << 16 | aggid++};
			assert(dtaunpackid(&agg) >= 0);
			aggs = realloc(aggs, sizeof(Agg) * aggid);
			memset(&aggs[aggid-1], 0, sizeof(Agg));
			aggs[aggid-1].DTAgg = agg;
			aggs[aggid-1].name = strdup(s->agg.name == nil ? "" : s->agg.name->name);
			actgradd(d->gr, (DTAct){ACTAGGKEY, codegen(s->agg.key), 8, agg});
			n = s->agg.value;
			if(n == nil) n = node(ONUM, 0ULL);
			actgradd(d->gr, (DTAct){ACTAGGVAL, codegen(n), 8, agg});
			break;
		}
		}
	if(allagg(c))
		actgradd(d->gr, (DTAct){ACTCANCEL, codegen(node(ONUM, 0)), 0, noagg});
	return d;
}

void
packclauses(Fmt *f)
{
	int i;
	DTClause *d;
	
	for(i = 0; i < nclauses; i++){
		d = mkdtclause(clauses[i]);
		dtclpack(f, d);
	}
}

/* epid lookup table, filled with info from the kernel */
Enab *enabtab[1024];

void
addepid(u32int epid, u32int cid, int reclen, char *p)
{
	Enab *e, **ep;
	
	assert(cid < nclauses);
	assert((uint)reclen >= 12);
	e = emalloc(sizeof(Enab));
	e->epid = epid;
	e->cl = clauses[cid];
	e->reclen = reclen;
	e->probe = strdup(p);
	ep = &enabtab[epid % nelem(enabtab)];
	e->next = *ep;
	*ep = e;
}

Enab *
epidlookup(u32int epid)
{
	Enab *e;
	
	for(e = enabtab[epid % nelem(enabtab)]; e != nil; e = e->next)
		if(e->epid == epid)
			return e;
	return nil;
}

uchar *
unpack(uchar *p, uchar *e, char *fmt, ...)
{
	va_list va;
	u64int vl;
	
	va_start(va, fmt);
	for(;;)
		switch(*fmt++){
		case 'c':
			if(p + 1 > e) return nil;
			*va_arg(va, u8int *) = p[0];
			p += 1;
			break;
		case 's':
			if(p + 2 > e) return nil;
			*va_arg(va, u16int *) = p[0] | p[1] << 8;
			p += 2;
			break;
		case 'i':
			if(p + 4 > e) return nil;
			*va_arg(va, u32int *) = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
			p += 4;
			break;
		case 'v':
			if(p + 8 > e) return nil;
			vl = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
			vl |= (uvlong)p[4] << 32 | (uvlong)p[5] << 40 | (uvlong)p[6] << 48 | (uvlong)p[7] << 56;
			*va_arg(va, u64int *) = vl;
			p += 8;
			break;
		case 0:
			return p;
		default:
			abort();
		}
}

static Val
receval(Node *n, uchar *p, uchar *e, Enab *en)
{
	u8int c;
	u16int s;
	u32int i;
	uvlong v;
	char *sp;
	uchar *q;
	Val a, b;

	switch(n->type){
	case OSYM:
		switch(n->sym->type){
		case SYMVAR:
			switch(n->sym->idx){
			case DTV_TIME:
				q = unpack(p + 4, e, "v", &v);
				assert(q != nil);
				return mkval(VALINT, v);
			case DTV_PROBE:
				return mkval(VALSTR, en->probe);
			default: sysfatal("receval: unknown variable %d", n->type);
			}
			break;
		default: sysfatal("receval: unknown symbol type %d", n->type);
		}
	case ONUM: return mkval(VALINT, n->num);
	case OBIN:
		a = receval(n->n1, p, e, en);
		b = receval(n->n2, p, e, en);
		assert(a.type == VALINT);
		assert(b.type == VALINT);
		return mkval(VALINT, evalop(n->op, n->typ->sign, a.v, b.v));
	case OLNOT:
		a = receval(n->n1, p, e, en);
		assert(a.type == VALINT);
		return mkval(VALINT, (uvlong) !a.v);
	case OTERN:
		a = receval(n->n1, p, e, en);
		assert(a.type == VALINT);
		return a.v ? receval(n->n2, p, e, en) : receval(n->n3, p, e, en);
	case ORECORD:
		switch(n->typ->type){
		case TYPINT:
			switch(n->typ->size){
			case 1: q = unpack(p + n->num, e, "c", &c); v = n->typ->sign ? (s8int)c : (u8int)c; break;
			case 2: q = unpack(p + n->num, e, "s", &s); v = n->typ->sign ? (s16int)s : (u16int)s; break;
			case 4: q = unpack(p + n->num, e, "i", &i); v = n->typ->sign ? (s32int)i : (u32int)i; break;
			case 8: q = unpack(p + n->num, e, "v", &v); break;
			default: q = nil;
			}
			assert(q != nil);
			return mkval(VALINT, v);
		case TYPSTRING:
			assert(p + n->num + n->typ->size <= e);
			sp = emalloc(n->typ->size + 1);
			memcpy(sp, p + n->num, n->typ->size);
			return mkval(VALSTR, sp); /* TODO: fix leak */
		default:
			sysfatal("receval: don't know how to parse record for %τ", n->typ);
		}
	default:
		sysfatal("receval: unknown type %α", n->type);
	}
}

static void
execprintf(Node **arg, int narg, uchar *p, uchar *e, Enab *en)
{
	char *x, *xp;
	Val v;
	int i;
	
	x = emalloc(sizeof(uvlong) * (narg - 1));
	xp = x;
	for(i = 0; i < narg - 1; i++){
		v = receval(arg[i + 1], p, e, en);
		switch(v.type){
		case VALINT:
			*(uvlong*)xp = v.v;
			xp += sizeof(uvlong);
			break;
		case VALSTR:
			*(char**)xp = v.s;
			xp += sizeof(char*);
			break;
		default: abort();
		}
	}
	vfprint(1, (*arg)->str, (va_list) x);
	free(x);
}

int
parseclause(Clause *cl, uchar *p, uchar *e, Enab *en, Biobuf *bp)
{
	Stat *s;
	int i;
	Val v;
	
	for(s = cl->stats; s < cl->stats + cl->nstats; s++)
		switch(s->type){
		case STATEXPR: break;
		case STATPRINT:
			for(i = 0; i < s->narg; i++){
				v = receval(s->arg[i], p, e, en);
				switch(v.type){
				case VALINT:
					Bprint(bp, "%lld", v.v);
					break;
				case VALSTR:
					Bprint(bp, "%s", v.s);
					break;
				default: sysfatal("parseclause: unknown val type %d", s->type);
				}
				Bprint(bp, "%c", i == s->narg - 1 ? '\n' : ' ');
			}
			break;
		case STATPRINTF:
			execprintf(s->arg, s->narg, p, e, en);
			break;
		case STATAGG: break;
		default:
			sysfatal("parseclause: unknown type %d", s->type);
		}
	return 0;
}

uchar *
parsefault(uchar *p0, uchar *e)
{
	uchar *p;
	u32int epid;
	u8int type, dummy;
	u16int n;
	Enab *en;

	p = unpack(p0, e, "csci", &type, &n, &dummy, &epid);
	if(p == nil) return nil;
	en = epidlookup(epid);
	switch(type){
	case DTFILL: {
		u32int pid;
		u64int addr;
		
		p = unpack(p, e, "iv", &pid, &addr);
		if(p == nil) return nil;
		fprint(2, "dtracy: illegal access: probe=%s, pid=%d, addr=%#llx\n", en != nil ? en->probe : nil, pid, addr);
		break;
	}
	default:
		fprint(2, "dtracy: unknown fault type %#.2ux\n", type);
	}
	return p0 + n - 12;
}

int
parsebuf(uchar *p, int n, Biobuf *bp)
{
	uchar *e;
	u32int epid;
	u64int ts;
	Enab *en;
	
	e = p + n;
	while(p < e){
		p = unpack(p, e, "iv", &epid, &ts);
		if(p == nil) goto err;
		if(epid == (u32int)-1){
			p = parsefault(p, e);
			if(p == nil) goto err;
			continue;
		}
		en = epidlookup(epid);
		if(en == nil) goto err;
		if(parseclause(en->cl, p - 12, p + en->reclen - 12, en, bp) < 0) return -1;
		p += en->reclen - 12;
	}
	return 0;
err:
	werrstr("buffer invalid");
	return -1;
}

static void
dumpexpr(DTExpr *e, char *prefix)
{
	int i;
	
	for(i = 0; i < e->n; i++)
		print("%s%.8ux %I\n", prefix, e->b[i], e->b[i]);
}

#pragma varargck type "ε" Node*

static void
fmtstring(Fmt *f, char *s)
{
	fmtrune(f, '"');
	for(; *s != 0; s++)
		switch(*s){
		case '\n': fmtprint(f, "\\n"); break;
		case '\r': fmtprint(f, "\\r"); break;
		case '\t': fmtprint(f, "\\t"); break;
		case '\v': fmtprint(f, "\\v"); break;
		case '\b': fmtprint(f, "\\b"); break;
		case '\a': fmtprint(f, "\\a"); break;
		case '"': fmtprint(f, "\""); break;
		case '\\': fmtprint(f, "\\"); break;
		default:
			if(*s < 0x20 || *s >= 0x7f)
				fmtprint(f, "\\%.3o", (uchar)*s);
			else
				fmtrune(f, *s);
		}
	fmtrune(f, '"');
}

typedef struct Op Op;
struct Op {
	char *name;
	int pred;
	enum { PRECRIGHT = 1 } flags;
};
static Op optab[] = {
	[OPLOR] {"||", 3, 0},
	[OPLAND] {"&&", 4, 0},
	[OPOR] {"|", 5, 0},
	[OPXNOR] {"~^", 6, 0},
	[OPXOR] {"^", 6, 0},
	[OPAND] {"&", 7, 0},
	[OPEQ] {"==", 8, },
	[OPNE] {"!=", 8, 0},
	[OPLE] {"<=", 9, 0},
	[OPLT] {"<", 9, 0},
	[OPLSH] {"<<", 10, 0},
	[OPRSH] {">>", 10, 0},
	[OPADD] {"+", 11, 0},
	[OPSUB] {"-", 11, 0},
	[OPDIV] {"/", 12, 0},
	[OPMOD] {"%", 12, 0},
	[OPMUL] {"*", 12, 0},
};
enum { PREDUNARY = 14 };

int
nodefmt(Fmt *f)
{
	Node *n;
	Op *op;
	int p;
	
	p = f->width;
	n = va_arg(f->args, Node *);
	switch(n->type){
	case OSYM: fmtprint(f, "%s", n->sym->name); break;
	case ONUM: fmtprint(f, "%lld", n->num); break;
	case OSTR: fmtstring(f, n->str); break;
	case OBIN:
		if(n->op >= nelem(optab) || optab[n->op].name == nil)
			fmtprint(f, "(%*ε ??op%d %*ε)", PREDUNARY, n->n1, n->op, PREDUNARY, n->n2);
		else{
			op = &optab[n->op];
			if(op->pred < p) fmtrune(f, '(');
			fmtprint(f, "%*ε %s %*ε", op->pred + (op->flags & PRECRIGHT), n->n1, op->name, op->pred + (~op->flags & PRECRIGHT), n->n2);
			if(op->pred < p) fmtrune(f, ')');
		}
		break;
	case OLNOT: fmtprint(f, "!%*ε", PREDUNARY, n->n1); break;
	case OTERN: fmtprint(f, "%2ε ? %1ε : %1ε", n->n1, n->n2, n->n3); break;
	case ORECORD: fmtprint(f, "record(%ε, %τ, %d)", n->n1, n->typ, (int)n->num); break;
	case OCAST: fmtprint(f, "(%τ) %*ε", n->typ, PREDUNARY, n->n1); break;
	default: fmtprint(f, "??? %α", n->type);
	}
	return 0;
}

void
dump(void)
{
	int i, j;
	Stat *s;
	Clause *c;
	DTClause *d;
	DTAct *a;
	
	for(i = 0; i < nclauses; i++){
		c = clauses[i];
		d = mkdtclause(c);
		print("clause %d:\n", c->id);
		for(j = 0; j < c->nprob; j++)
			print("\tprobe '%s'\n", c->probs[j]);
		print("\tkernel code:\n");
		if(c->pred == nil)
			print("\t\tno predicate\n");
		else{
			print("\t\tpredicate\n");
			dumpexpr(c->pred, "\t\t\t");
		}
		for(a = d->gr->acts; a < d->gr->acts + d->gr->nact; a++)
			switch(a->type){
			case ACTTRACE:
				print("\t\ttrace (%d bytes)\n", a->size);
				dumpexpr(a->p, "\t\t\t");
				break;
			case ACTTRACESTR:
				print("\t\ttrace string (%d bytes)\n", a->size);
				dumpexpr(a->p, "\t\t\t");
				break;
			case ACTAGGKEY:
				print("\t\taggregation key (%s,%d,%d)\n", a->agg.type >= nelem(aggtypes) ? "???" : aggtypes[a->agg.type], a->agg.keysize, (u16int)a->agg.id);
				dumpexpr(a->p, "\t\t\t");
				break;
			case ACTAGGVAL:
				print("\t\taggregation value (%s,%d,%d)\n", a->agg.type >= nelem(aggtypes) ? "???" : aggtypes[a->agg.type], a->agg.keysize, (u16int)a->agg.id);
				dumpexpr(a->p, "\t\t\t");
				break;
			case ACTCANCEL:
				print("\t\tcancel record\n");
				break;
			default:
				print("\t\t??? %d\n", a->type);
			}
		print("\trecord formatting:\n");
		for(s = c->stats; s < c->stats + c->nstats; s++)
			switch(s->type){
			case STATEXPR:
				break;
			case STATPRINT:
				print("\t\tprint\n");
				for(j = 0; j < s->narg; j++)
					print("\t\t\targ %ε\n", s->arg[j]);
				break;
			case STATPRINTF:
				print("\t\tprintf\n");
				for(j = 0; j < s->narg; j++)
					print("\t\t\targ %ε\n", s->arg[j]);
				break;
			case STATAGG:
				break;
			default:
				print("\t\t??? %d\n", s->type);
			}
	}
}
