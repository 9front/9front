#include <u.h>
#include <libc.h>
#include "cpp.h"

#define	NSTAK	128
#define	SGN	0
#define	UNS	1
#define	UND	2

#define	UNSMARK	0x1000

struct value {
	vlong	val;
	int	type;
};

/* conversion types */
enum {
	NONE,
	RELAT,
	ARITH,
	LOGIC,
	SPCL,
	SHIFT,
	UNARY
};

/* operator priority, arity, and conversion type, indexed by tokentype */
const struct pri {
	char	pri;
	char	assoc;
	char	arity;
	char	ctype;
} priority[MAXTOK] = {
	[END]		{ 0, 0, 0, 0 },
	[EQ]		{ 11, 0, 2, RELAT },
	[NEQ]		{ 11, 0, 2, RELAT },
	[LEQ]		{ 12, 0, 2, RELAT },
	[GEQ]		{ 12, 0, 2, RELAT },
	[LSH]		{ 13, 0, 2, SHIFT },
	[RSH]		{ 13, 0, 2, SHIFT },
	[LAND]		{ 7, 0, 2, LOGIC },
	[LOR]		{ 6, 0, 2, LOGIC },
	[LP]		{ 3, 1, 0, 0 },
	[RP]		{ 3, 1, 0, 0 },
	[AND]		{ 10, 0, 2, ARITH },
	[STAR]		{ 15, 0, 2, ARITH },
	[PLUS]		{ 14, 0, 2, ARITH },
	[MINUS]		{ 14, 0, 2, ARITH },
	[TILDE]		{ 16, 0, 1, UNARY },
	[NOT]		{ 16, 0, 1, UNARY },
	[SLASH]		{ 15, 0, 2, ARITH },
	[PCT]		{ 15, 0, 2, ARITH },
	[LT]		{ 12, 0, 2, RELAT },
	[GT]		{ 12, 0, 2, RELAT },
	[CIRC]		{ 9, 0, 2, ARITH },
	[OR]		{ 8, 0, 2, ARITH },
	[QUEST]		{ 5, 1, 2, SPCL },
	[COLON]		{ 5, 1, 2, SPCL },
	[COMMA]		{ 4, 0, 2, 0 },
	[XCOMMA]	{ 4, 0, 2, 0 },
	[DEFINED]	{ 16, 0, 1, UNARY },
	[UMINUS]	{ 16, 0, 0, UNARY },
};

int	evalop(struct pri);
struct	value tokval(Token *);
struct value vals[NSTAK], *vp;
enum toktype ops[NSTAK], *op;

/*
 * Evaluate an #if #elif #ifdef #ifndef line.  trp->tp points to the keyword.
 * Using shunting yard algorithm.
 */
vlong
eval(Tokenrow *trp, int kw)
{
	Token *tp;
	Nlist *np;
	int ntok, rand;

	trp->tp++;
	if (kw==KIFDEF || kw==KIFNDEF) {
		if (trp->lp - trp->bp != 4 || trp->tp->type!=NAME) {
			error(ERROR, "Syntax error in #ifdef/#ifndef");
			return 0;
		}
		np = lookup(trp->tp, 0);
		return (kw==KIFDEF) == (np && np->flag&(ISDEFINED|ISMAC));
	}
	ntok = trp->tp - trp->bp;
	kwdefined->val = KDEFINED;	/* activate special meaning of defined */
	expandrow(trp, "<if>");
	kwdefined->val = NAME;
	vp = vals;
	op = ops;
	for (rand=0, tp = trp->bp+ntok; tp < trp->lp; tp++) {
		switch(tp->type) {
		case WS:
		case NL:
			continue;

		/* nilary */
		case NAME:
		case NAME1:
		case NUMBER:
		case CCON:
		case STRING:
			if (rand)
				goto syntax;
			if(vp == vals + NSTAK)
				goto fullstakdeveloper;
			*vp++ = tokval(tp);
			rand = 1;
			continue;

		/* unary */
		case DEFINED:
		case TILDE:
		case NOT:
			if (rand)
				goto syntax;
			if(op == ops + NSTAK)
				goto fullstakdeveloper;
			*op++ = tp->type;
			continue;

		/* unary-binary */
		case PLUS: case MINUS: case STAR: case AND:
			if (rand==0) {
				if(op == ops + NSTAK)
					goto fullstakdeveloper;
				if (tp->type==MINUS)
					*op++ = UMINUS;
				if (tp->type==STAR || tp->type==AND) {
					error(ERROR, "Illegal operator * or & in #if/#elif");
					return 0;
				}
				continue;
			}
			/* flow through */

		/* plain binary */
		case EQ: case NEQ: case LEQ: case GEQ: case LSH: case RSH:
		case LAND: case LOR: case SLASH: case PCT:
		case LT: case GT: case CIRC: case OR: case QUEST:
		case COLON: case COMMA: case XCOMMA:
			if (rand==0)
				goto syntax;
			if (evalop(priority[tp->type])!=0)
				return 0;
			if(op == ops + NSTAK)
				goto fullstakdeveloper;
			*op++ = tp->type;
			rand = 0;
			continue;

		case LP:
			if (rand)
				goto syntax;
			if(op == ops + NSTAK)
				goto fullstakdeveloper;
			*op++ = LP;
			continue;

		case RP:
			if (!rand)
				goto syntax;
			if (evalop(priority[RP])!=0)
				return 0;
			if (op<=ops || op[-1]!=LP) {
				goto syntax;
			}
			op--;
			continue;

		default:
			error(ERROR,"Bad operator (%t) in #if/#elif", tp);
			return 0;
		}
	}
	if (rand==0)
		goto syntax;
	if (evalop(priority[END])!=0)
		return 0;
	if (op!=ops || vp!=&vals[1]) {
		error(ERROR, "Botch in #if/#elif");
		return 0;
	}
	if (vals[0].type==UND)
		error(ERROR, "Undefined expression value");
	return vals[0].val;
syntax:
	error(ERROR, "Syntax xx error in #if/#elif");
	return 0;
fullstakdeveloper:
	error(ERROR, "Out of stack space evaluating #if");
	return 0;
}

int
evalop(struct pri pri)
{
	struct value v1, v2;
	vlong rv1, rv2;
	int rtype, oper;

	rv2=0;
	rtype=0;
	while (op != ops && pri.pri + pri.assoc <= priority[op[-1]].pri) {
		oper = *--op;
		if (priority[oper].arity==2) {
			v2 = *--vp;
			rv2 = v2.val;
		}
		v1 = *--vp;
		rv1 = v1.val;
		switch (priority[oper].ctype) {
		case 0:
		default:
			error(WARNING, "Syntax error in #if/#endif");
			return 1;
		case ARITH:
		case RELAT:
			if (v1.type==UNS || v2.type==UNS)
				rtype = UNS;
			else
				rtype = SGN;
			if (v1.type==UND || v2.type==UND)
				rtype = UND;
			if (priority[oper].ctype==RELAT && rtype==UNS) {
				oper |= UNSMARK;
				rtype = SGN;
			}
			break;
		case SHIFT:
			if (v1.type==UND || v2.type==UND)
				rtype = UND;
			else
				rtype = v1.type;
			if (rtype==UNS)
				oper |= UNSMARK;
			break;
		case UNARY:
			rtype = v1.type;
			break;
		case LOGIC:
		case SPCL:
			break;
		}
		switch (oper) {
		case EQ: case EQ|UNSMARK:
			rv1 = rv1==rv2; break;
		case NEQ: case NEQ|UNSMARK:
			rv1 = rv1!=rv2; break;
		case LEQ:
			rv1 = rv1<=rv2; break;
		case GEQ:
			rv1 = rv1>=rv2; break;
		case LT:
			rv1 = rv1<rv2; break;
		case GT:
			rv1 = rv1>rv2; break;
		case LEQ|UNSMARK:
			rv1 = (uvlong)rv1<=rv2; break;
		case GEQ|UNSMARK:
			rv1 = (uvlong)rv1>=rv2; break;
		case LT|UNSMARK:
			rv1 = (uvlong)rv1<rv2; break;
		case GT|UNSMARK:
			rv1 = (uvlong)rv1>rv2; break;
		case LSH:
			rv1 <<= rv2; break;
		case LSH|UNSMARK:
			rv1 = (uvlong)rv1<<rv2; break;
		case RSH:
			rv1 >>= rv2; break;
		case RSH|UNSMARK:
			rv1 = (uvlong)rv1>>rv2; break;
		case LAND:
			rtype = UND;
			if (v1.type==UND)
				break;
			if (rv1!=0) {
				if (v2.type==UND)
					break;
				rv1 = rv2!=0;
			} else
				rv1 = 0;
			rtype = SGN;
			break;
		case LOR:
			rtype = UND;
			if (v1.type==UND)
				break;
			if (rv1==0) {
				if (v2.type==UND)
					break;
				rv1 = rv2!=0;
			} else
				rv1 = 1;
			rtype = SGN;
			break;
		case AND:
			rv1 &= rv2; break;
		case STAR:
			rv1 *= rv2; break;
		case PLUS:
			rv1 += rv2; break;
		case MINUS:
			rv1 -= rv2; break;
		case UMINUS:
			if (v1.type==UND)
				rtype = UND;
			rv1 = -rv1; break;
		case OR:
			rv1 |= rv2; break;
		case CIRC:
			rv1 ^= rv2; break;
		case TILDE:
			rv1 = ~rv1; break;
		case NOT:
			rv1 = !rv1; if (rtype!=UND) rtype = SGN; break;
		case SLASH:
			if (rv2==0) {
				rtype = UND;
				break;
			}
			if (rtype==UNS)
				rv1 /= (uvlong)rv2;
			else
				rv1 /= rv2;
			break;
		case PCT:
			if (rv2==0) {
				rtype = UND;
				break;
			}
			if (rtype==UNS)
				rv1 %= (uvlong)rv2;
			else
				rv1 %= rv2;
			break;
		case COLON:
			if (op[-1] != QUEST)
				error(ERROR, "Bad ?: in #if/endif");
			else {
				op--;
				if ((--vp)->val==0)
					v1 = v2;
				rtype = v1.type;
				rv1 = v1.val;
			}
			break;
		case DEFINED:
			break;
		default:
			error(ERROR, "Eval botch (unknown operator)");
			return 1;
		}
		v1.val = rv1;
		v1.type = rtype;
		if(op == ops + NSTAK){
			error(ERROR, "Out of stack space evaluating #if");
			return 0;
		}
		*vp++ = v1;
	}
	return 0;
}

struct value
tokval(Token *tp)
{
	struct value v;
	Nlist *np;
	int i, base, c, longcc;
	uvlong n;
	Rune r;
	uchar *p;

	v.type = SGN;
	v.val = 0;
	switch (tp->type) {

	case NAME:
		v.val = 0;
		break;

	case NAME1:
		if ((np = lookup(tp, 0)) && np->flag&(ISDEFINED|ISMAC))
			v.val = 1;
		break;

	case NUMBER:
		n = 0;
		base = 10;
		p = tp->t;
		c = p[tp->len];
		p[tp->len] = '\0';
		if (*p=='0') {
			base = 8;
			if (p[1]=='x' || p[1]=='X') {
				base = 16;
				p++;
			}
			p++;
		}
		for (;; p++) {
			if ((i = digit(*p)) < 0)
				break;
			if (i>=base)
				error(WARNING,
				  "Bad digit in number %t", tp);
			n *= base;
			n += i;
		}
		if (n>=(1ULL<<63) && base!=10)
			v.type = UNS;
		for (; *p; p++) {
			if (*p=='u' || *p=='U')
				v.type = UNS;
			else if (*p=='l' || *p=='L')
				{}
			else {
				error(ERROR,
				  "Bad number %t in #if/#elif", tp);
				break;
			}
		}
		v.val = n;
		tp->t[tp->len] = c;
		break;

	case CCON:
		n = 0;
		p = tp->t;
		longcc = 0;
		if (*p=='L') {
			p += 1;
			longcc = 1;
		}
		p += 1;
		if (*p=='\\') {
			p += 1;
			if ((i = digit(*p))>=0 && i<=7) {
				n = i;
				p += 1;
				if ((i = digit(*p))>=0 && i<=7) {
					p += 1;
					n <<= 3;
					n += i;
					if ((i = digit(*p))>=0 && i<=7) {
						p += 1;
						n <<= 3;
						n += i;
					}
				}
			} else if (*p=='x') {
				p += 1;
				while ((i = digit(*p))>=0 && i<=15) {
					p += 1;
					n <<= 4;
					n += i;
				}
			} else {
				static char cvcon[]
				  = "a\ab\bf\fn\nr\rt\tv\v''\"\"??\\\\";
				for (i=0; i<sizeof(cvcon); i+=2) {
					if (*p == cvcon[i]) {
						n = cvcon[i+1];
						break;
					}
				}
				p += 1;
				if (i>=sizeof(cvcon))
					error(WARNING,
					 "Undefined escape in character constant");
			}
		} else if (*p=='\'')
			error(ERROR, "Empty character constant");
		else {
			i = chartorune(&r, (char*)p);
			n = r;
			p += i;
			if (i>1 && longcc==0)
				error(WARNING, "Undefined character constant");
		}
		if (*p!='\'')
			error(WARNING, "Multibyte character constant undefined");
		else if (n>127 && longcc==0)
			error(WARNING, "Character constant taken as not signed");
		v.val = n;
		break;

	case STRING:
		error(ERROR, "String in #if/#elif");
		break;
	}
	return v;
}

int
digit(int i)
{
	if ('0'<=i && i<='9')
		i -= '0';
	else if ('a'<=i && i<='f')
		i -= 'a'-10;
	else if ('A'<=i && i<='F')
		i -= 'A'-10;
	else
		i = -1;
	return i;
}
