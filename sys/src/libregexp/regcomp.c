#include <u.h>
#include <libc.h>
#include <regexp.h>
#include "regimpl.h"

int instrcnt[] = {
	[TANY]    2,
	[TBOL]    1,
	[TCAT]    0,
	[TCLASS]  1,
	[TEOL]    1,
	[TNOTNL]  1,
	[TOR]     2,
	[TPLUS]   1,
	[TQUES]   1,
	[TRUNE]   1,
	[TSTAR]   2,
	[TSUB]    2
};

static Renode*
node(Parselex *plex, int op, Renode *l, Renode *r)
{
	Renode *n;

	plex->instrs += instrcnt[op];
	n = plex->next++;
	n->op = op;
	n->left = l;
	n->right = r;
	return n;
}

static Renode*
e3(Parselex *plex)
{
	char error[128];
	Renode *n;

	switch(lex(plex)) {
	case LANY:
		return node(plex, TANY, nil, nil);
	case LBOL:
		return node(plex, TBOL, nil, nil);
	case LEOL:
		return node(plex, TEOL, nil, nil);
	case LRUNE:
		n = node(plex, TRUNE, nil, nil);
		n->r = plex->rune;
		return n;
	case LCLASS:
		if(plex->nc)
			return buildclassn(plex);
		return buildclass(plex);
	case LLPAR:
		n = e0(plex);
		n = node(plex, TSUB, n, nil);
		if(lex(plex) != LRPAR) {
			snprint(error, sizeof(error), "regexp %s: no matching parenthesis", plex->orig);
			regerror(error);
			longjmp(plex->exitenv, 1);
		}
		return n;
	default:
		if(plex->rune)
			snprint(error, sizeof(error), "regexp %s: syntax error: %C", plex->orig, plex->rune);
		else
			snprint(error, sizeof(error), "regexp %s: parsing error", plex->orig);
		regerror(error);
		longjmp(plex->exitenv, 1);
	}
}

static Renode*
e2(Parselex *plex)
{
	Renode *n;

	n = e3(plex);
	while(lex(plex) == LREP) {
		switch(plex->rune) {
		case L'*':
			n = node(plex, TSTAR, n, nil);
			break;
		case L'+':
			n = node(plex, TPLUS, n, nil);
			break;
		case L'?':
			n = node(plex, TQUES, n, nil);
			break;
		}
	}
	plex->peek = 1;
	return n;
}

static Renode*
invert(Renode *n)
{
	Renode *n1;

	if(n->op != TCAT)
		return n;
	while(n->left->op == TCAT) {
		n1 = n->left;
		n->left = n1->right;
		n1->right = n;
		n = n1;
	}
	return n;
}

static Renode*
e1(Parselex *plex)
{
	Renode *n;
	int sym;

	n = e2(plex);
	for(;;) {
		sym = lex(plex);
		if(sym == LEND || sym == LOR || sym == LRPAR)
			break;
		plex->peek = 1;
		n = node(plex, TCAT, n, e2(plex));
	}
	plex->peek = 1;
	return invert(n);
}

static Renode*
e0(Parselex *plex)
{
	Renode *n;

	n = e1(plex);
	for(;;) {
		if(lex(plex) != LOR)
			break;
		n = node(plex, TOR, n, e1(plex));
	}
	plex->peek = 1;
	return n;
}

static Parselex*
initplex(Parselex *plex, char *regstr, int lit)
{
	plex->getnextr = lit ? getnextrlit : getnextr;
	plex->rawexp = plex->orig = regstr;
	plex->sub = 0;
	plex->instrs = 0;
	plex->peek = 0;
	plex->done = 0;
	return plex;
}

static Reprog*
regcomp1(char *regstr, int nl, int lit)
{
	Reprog *reprog;
	Parselex plex;
	Renode *parsetr;
	int regstrlen, maxthr;

	regstrlen = utflen(regstr);
	initplex(&plex, regstr, lit);
	plex.nodes = calloc(sizeof(*plex.nodes), regstrlen*2);
	if(plex.nodes == nil)
		return nil;
	plex.next = plex.nodes;

	if(setjmp(plex.exitenv) != 0) {
		free(plex.nodes);
		return nil;
	}

	maxthr = regstrlen + 1;
	parsetr = node(&plex, TSUB, e0(&plex), nil);

//	prtree(parsetr, 0, 1);
	reprog = malloc(sizeof(Reprog) +
	                sizeof(Reinst) * plex.instrs +
	                sizeof(Rethread) * maxthr);
	reprog->len = plex.instrs;
	reprog->nthr = maxthr;
	reprog->startinst = compile(parsetr, reprog, nl);
	reprog->threads = (Rethread*)(reprog->startinst + reprog->len);
	reprog->regstr = regstr;

	free(plex.nodes);
	return reprog;
}

Reprog*
regcomp(char *str)
{
	return regcomp1(str, 0, 0);
}

Reprog*
regcomplit(char *str)
{
	return regcomp1(str, 0, 1);
}

Reprog*
regcompnl(char *str)
{
	return regcomp1(str, 1, 0);
}

static Reinst*
compile1(Renode *renode, Reinst *reinst, int *sub, int nl)
{
	Reinst *i;
	int s;

Tailcall:
	if(renode == nil)
		return reinst;
	switch(renode->op) {
	case TCLASS:
		reinst->op = OCLASS;
		reinst->r = renode->r;
		reinst->r1 = renode->r1;
		reinst->a = reinst + 1 + renode->nclass;
		renode = renode->left;
		reinst++;
		goto Tailcall;
	case TCAT:
		reinst = compile1(renode->left, reinst, sub, nl);
		renode = renode->right;
		goto Tailcall;
	case TOR:
		reinst->op = OSPLIT;
		reinst->a = reinst + 1;
		i = compile1(renode->left, reinst->a, sub, nl);
		reinst->b = i + 1;
		i->op = OJMP;
		i->a = compile1(renode->right, reinst->b, sub, nl);
		return i->a;
	case TSTAR:
		reinst->op = OSPLIT;
		reinst->a = reinst + 1;
		i = compile1(renode->left, reinst->a, sub, nl);
		reinst->b = i + 1;
		i->op = OJMP;
		i->a = reinst;
		return reinst->b;
	case TPLUS:
		i = reinst;
		reinst = compile1(renode->left, reinst, sub, nl);
		reinst->op = OSPLIT;
		reinst->a = i;
		reinst->b = reinst + 1;
		return reinst->b;
	case TQUES:
		reinst->op = OSPLIT;
		reinst->a = reinst + 1;
		reinst->b = compile1(renode->left, reinst->a, sub, nl);
		return reinst->b;
	case TSUB:
		reinst->op = OSAVE;
		reinst->sub = s = (*sub)++;
		reinst = compile1(renode->left, reinst+1, sub, nl);
		reinst->op = OUNSAVE;
		reinst->sub = s;
		return reinst + 1;
	case TANY:
		if(nl == 0)
			reinst++->op = ONOTNL;
		reinst->op = OANY;
		return reinst + 1;
	case TRUNE:
		reinst->op = ORUNE;
		reinst->r = renode->r;
		return reinst + 1;
	case TNOTNL:
		reinst->op = ONOTNL;
		return reinst + 1;
	case TEOL:
		reinst->op = OEOL;
		return reinst + 1;
	case TBOL:
		reinst->op = OBOL;
		return reinst + 1;
	}
	return nil;
}

static Reinst*
compile(Renode *parsetr, Reprog *reprog, int nl)
{
	Reinst *reinst, *end;
	int sub;

	sub = 0;
	reinst = (Reinst*)(reprog+1);
	end = compile1(parsetr, reinst, &sub, nl);
	assert(end <= reinst + reprog->len);
	return reinst;
}

static void
getnextr(Parselex *l)
{
	l->literal = 0;
	if(l->done) {
		l->rune = L'\0';
		return;
	}
	l->rawexp += chartorune(&l->rune, l->rawexp);
	if(*l->rawexp == 0)
		l->done = 1;
	if(l->rune == L'\\')
		getnextrlit(l);
}

static void
getnextrlit(Parselex *l)
{
	l->literal = 1;
	if(l->done) {
		l->literal = 0;
		l->rune = L'\0';
		return;
	}
	l->rawexp += chartorune(&l->rune, l->rawexp);
	if(*l->rawexp == 0)
		l->done = 1;
}

static int
lex(Parselex *l)
{
	if(l->peek) {
		l->peek = 0;
		return l->peeklex;
	}
	l->getnextr(l);
	if(l->literal)
		return l->peeklex = LRUNE;
	switch(l->rune){
	case L'\0':
		return l->peeklex = LEND;
	case L'*':
	case L'?':
	case L'+':
		return l->peeklex = LREP;
	case L'|':
		return l->peeklex = LOR;
	case L'.':
		return l->peeklex = LANY;
	case L'(':
		return l->peeklex = LLPAR;
	case L')':
		return l->peeklex = LRPAR;
	case L'^':
		return l->peeklex = LBOL;
	case L'$':
		return l->peeklex = LEOL;
	case L'[':
		getclass(l);
		return l->peeklex = LCLASS;
	}
	return l->peeklex = LRUNE;
}

static int
pcmp(void *va, void *vb)
{
	Rune *a, *b;

	a = va;
	b = vb;

	if(a[0] < b[0])
		return 1;
	if(a[0] > b[0])
		return -1;
	if(a[1] < b[1])
		return 1;
	if(a[1] > b[1])
		return -1;
	return 0;
}

static void
getclass(Parselex *l)
{
	Rune *p, *q, t;

	l->nc = 0;
	getnextrlit(l);
	if(l->rune == L'^') {
		l->nc = 1;
		getnextrlit(l);
	}
	p = l->cpairs;
	for(;;) {
		*p = l->rune;
		if(l->rune == L']')
			break;
		if(l->rune == L'-') {
			regerror("Malformed class");
			longjmp(l->exitenv, 1);
		}
		if(l->rune == '\\') {
			getnextrlit(l);
			*p = l->rune;
		}
		if(l->rune == 0) {
			regerror("No closing ] for class");
			longjmp(l->exitenv, 1);
		}
		getnextrlit(l);
		if(l->rune == L'-') {
			getnextrlit(l);
			if(l->rune == L']') {
				regerror("Malformed class");
				longjmp(l->exitenv, 1);
			}
			if(l->rune == L'-') {
				regerror("Malformed class");
				longjmp(l->exitenv, 1);
			}
			if(l->rune == L'\\')
				getnextrlit(l);
			p[1] = l->rune;
			if(p[0] > p[1]) {
				t = p[0];
				p[0] = p[1];
				p[1] = t;
			}
			getnextrlit(l);
		} else
			p[1] = p[0];
		if(p >= l->cpairs + nelem(l->cpairs) - 2) {
			regerror("Class too big\n");
			longjmp(l->exitenv, 1);
		}
		p += 2;
	}
	*p = L'\0';
	qsort(l->cpairs, (p - l->cpairs)/2, 2*sizeof(*l->cpairs), pcmp);
	q = l->cpairs;
	for(p = l->cpairs+2; *p != 0; p += 2) {
		if(p[1] < q[0] - 1) {
			q[2] = p[0];
			q[3] = p[1];
			q += 2;
			continue;
		}
		q[0] = p[0];
		if(p[1] > q[1])
			q[1] = p[1];
	}
	q[2] = 0;
}

/* classes are in descending order see pcmp */
static Renode*
buildclassn(Parselex *l)
{
	Renode *n;
	Rune *p;
	int i;

	i = 0;
	p = l->cpairs;
	n = node(l, TCLASS, nil, nil);
	n->r = p[1] + 1;
	n->r1 = Runemax;
	n->nclass = i++;

	for(; *p != 0; p += 2) {
		n = node(l, TCLASS, n, nil);
		n->r = p[3] + 1;
		n->r1 = p[0] - 1;
		n->nclass = i++;
	}
	n->r = 0;
	return node(l, TCAT, node(l, TNOTNL, nil, nil), n);
}

static Renode*
buildclass(Parselex *l)
{
	Renode *n;
	Rune *p;
	int i;

	i = 0;
	n = node(l, TCLASS, nil, nil);
	n->r = Runemax + 1;
	n->nclass = i++;

	for(p = l->cpairs; *p != 0; p += 2) {
		n = node(l, TCLASS, n, nil);
		n->r = p[0];
		n->r1 = p[1];
		n->nclass = i++;
	}
	return n;
}

static void
prtree(Renode *tree, int d, int f)
{
	int i;

	if(tree == nil)
		return;
	if(f)
	for(i = 0; i < d; i++)
		print("\t");
	switch(tree->op) {
	case TCAT:
		prtree(tree->left, d, 0);
		prtree(tree->right, d, 1);
		break;
	case TOR:
		print("TOR\n");
		prtree(tree->left, d+1, 1);
		for(i = 0; i < d; i++)
			print("\t");
		print("|\n");
		prtree(tree->right, d+1, 1);
		break;
	case TSTAR:
		print("*\n");
		prtree(tree->left, d+1, 1);
		break;
	case TPLUS:
		print("+\n");
		prtree(tree->left, d+1, 1);
		break;
	case TQUES:
		print("?\n");
		prtree(tree->left, d+1, 1);
		break;
	case TANY:
		print(".\n");
		prtree(tree->left, d+1, 1);
		break;
	case TBOL:
		print("^\n");
		break;
	case TEOL:
		print("$\n");
		break;
	case TSUB:
		print("TSUB\n");
		prtree(tree->left, d+1, 1);
		break;
	case TRUNE:
		print("TRUNE: %C\n", tree->r);
		break;
	case TNOTNL:
		print("TNOTNL: !\\n\n");
		break;
	case TCLASS:
		print("CLASS: %C-%C\n", tree->r, tree->r1);
		prtree(tree->left, d, 1);
		break;
	}
}
