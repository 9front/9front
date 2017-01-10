%{
#include <u.h>
#include <libc.h>
#include <avl.h>
#include <bio.h>
#include "mix.h"
%}

%union {
	Sym *sym;
	long lval;
	u32int mval;
	Rune *rbuf;
}

%type	<lval>	wval apart exp aexp fpart ipart
%type	<mval>	wval1
%type	<sym>	loc reflit

%token	<sym>	LSYMDEF LSYMREF LOP LEQU LORIG LCON LALF LEND
%token	<sym>	LBACK LHERE LFORW
%token	<lval>	LNUM
%token	<rbuf>	LSTR

%left '+' '-' '*' '/' LSS ':' ','

%%

prog:
|	prog inst

inst:
	eol
|	loc LOP apart ipart fpart eol
	{
		defloc($loc, star);
		asm($LOP, $apart, $ipart, $fpart);
	}
|	loc LOP reflit ipart fpart eol
	{
		defloc($loc, star);
		addref($reflit, star);
		refasm($LOP, $ipart, $fpart);
	}
|	loc LEQU wval eol
	{
		defloc($loc, $wval);
	}
|	loc LORIG wval eol
	{
		defloc($loc, star);
		star = $wval;
	}
|	loc LCON wval1 eol
	{
		defloc($loc, star);
		cells[star++] = $wval1;
	}
|	loc LALF LSTR eol
	{
		defloc($loc, star);
		alf(star++, $LSTR);
	}
|	loc LEND wval eol
	{
		endprog($wval);
		defloc($loc, star);
	}

loc:
	{
		$$ = nil;
	}
|	LSYMREF
	{
		$$ = $LSYMREF;
	}
|	LHERE
	{
		Sym *f;
		int l;

		l = ($LHERE)->opc;
		back[l] = star;
		f = forw + l;
		defloc(f, star);
		f->lex = LSYMREF;
		f->refs = nil;
		f->i = f->max = 0;
		$$ = nil;
	}

apart:
	{
		$$ = 0;
	}
|	exp
|	LBACK
	{
		$$ = back[($LBACK)->opc];
	}

reflit:
	LSYMREF
|	'=' wval1 '='
	{
		$$ = con($wval1);
	}
|	LFORW
	{
		$$ = forw + ($LFORW)->opc;
	}

ipart:
	{
		$$ = 0;
	}
|	',' exp
	{
		$$ = $exp;
	}

fpart:
	{
		$$ = -1;
	}
|	'(' exp ')'
	{
		if($exp < 0)
			yyerror("invalid fpart %d\n", $exp);
		$$ = $exp;
	}

exp:
	aexp
|	'+' aexp
	{
		$$ = $aexp;
	}
|	'-' aexp
	{
		$$ = -$aexp;
	}
|	exp '+' aexp
	{
		$$ = $exp + $aexp;
	}
|	exp '-' aexp
	{
		$$ = $exp - $aexp;
	}
|	exp '*' aexp
	{
		$$ = $exp * $aexp;
	}
|	exp '/' aexp
	{
		$$ = ($exp) / $aexp;
	}
|	exp LSS aexp
	{
		$$ = (((vlong)$exp) << 30) / $aexp;
	}
|	exp ':' aexp
	{
		$$ = F($exp, $aexp);
	}

aexp:
	LNUM
|	LSYMDEF
	{
		u32int mval;

		mval = ($LSYMDEF)->mval;
		if(mval & SIGNB) {
			mval &= ~SIGNB;
			$$ = -((long)mval);
		} else
			$$ = mval;
	}
|	'*'
	{
		$$ = star;
	}

wval:
	wval1
	{
		if($wval1 & SIGNB)
			$$ = -(long)($wval1 & MASK5);
		else
			$$ = $wval1;
	}

wval1:
	exp fpart
	{
		$$ = wval(0, $exp, $fpart);
	}
|	wval ',' exp fpart
	{
		$$ = wval($wval, $exp, $fpart);
	}

eol:
	'\n'

%%

int back[10];
Sym forw[10];

void
defrefs(Sym *sym, long apart)
{
	u32int inst, mval;
	int *ref, *ep;

	ep = sym->refs + sym->i;
	for(ref = sym->refs; ref < ep; ref++) {
		inst = cells[*ref];
		inst &= ~(MASK2 << BITS*3);
		if(apart < 0) {
			mval = -apart;
			inst |= SIGNB;
		} else
			mval = apart;
		inst |= (mval&MASK2) << BITS*3;
		cells[*ref] = inst;
	}
}

void
defloc(Sym *sym, long val)
{
	if(sym == nil)
		return;
	defrefs(sym, val);
	free(sym->refs);
	sym->lex = LSYMDEF;
	sym->mval = val < 0 ? -val|SIGNB : val;
}

void
addref(Sym *ref, long star)
{
	if(ref->refs == nil || ref->i == ref->max) {
		ref->max = ref->max == 0 ? 3 : ref->max*2;
		ref->refs = erealloc(ref->refs, ref->max * sizeof(int));
	}
	ref->refs[ref->i++] = star;
}

static void
asm(Sym *op, long apart, long ipart, long fpart)
{
	u32int inst, mval;

	inst = op->opc & MASK1;

	if(fpart == -1)
		inst |= (op->f&MASK1) << BITS;
	else
		inst |= (fpart&MASK1) << BITS;

	inst |= (ipart&MASK1) << BITS*2;

	if(apart < 0) {
		mval = -apart;
		inst |= SIGNB;
	} else
		mval = apart;
	inst |= (mval&MASK2) << BITS*3;

	cells[star++] = inst;
}

void
refasm(Sym *op, long ipart, long fpart)
{
	u32int inst;

	inst = op->opc & MASK1;

	if(fpart == -1)
		inst |= (op->f&MASK1) << BITS;
	else
		inst |= (fpart&MASK1) << BITS;

	inst |= (ipart&MASK1) << BITS*2;

	cells[star++] = inst;
}

Sym*
con(u32int exp)
{
	Con *c;
	static int i;
	static char buf[20];

	seprint(buf, buf+20, "con%d\n", i++);
	c = emalloc(sizeof(*c));
	c->sym = sym(buf);
	c->exp = exp;
	c->link = cons;
	cons = c;
	return c->sym;
}

void
alf(int loc, Rune *b)
{
	u32int w;
	int m;
	Rune *r, *e;

	w = 0;
	e = b + 5;
	for(r = b; r < e; r++) {
		if((m = runetomix(*r)) == -1)
			yyerror("Bad mixchar %C\n", *r);
		w |= m;
		if(r+1 < e)
			w <<= BITS;
	}
	cells[loc] = w;
}

void
endprog(int start)
{
	Con *c, *link;
	for(c = cons; c != nil; c = link) {
		defloc(c->sym, star);
		cells[star++] = c->exp;
		link = c->link;
		free(c);
	}
	cons = nil;
	vmstart = start;
	yydone = 1;
}

u32int
wval(u32int old, int exp, int f)
{
	if(f == -1) {
		if(exp < 0)
			return -exp | SIGNB;
		else
			return exp;
	}

	if(exp < 0)
		return mixst(old, -exp&MASK5 | SIGNB, f);
	return mixst(old, exp & MASK5, f);
}
