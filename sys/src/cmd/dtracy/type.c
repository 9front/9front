#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <dtracy.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

Node *
icast(int sign, int size, Node *n)
{
	Type *t;
	
	t = type(TYPINT, sign, size);
	return node(OCAST, t, n);
}

/*
	the type checker checks types.
	the result is an expression that is correct if evaluated with 64-bit operands all the way.
	to maintain c-like semantics, this means adding casts all over the place, which will get optimised later.
	
	note we use kencc, NOT ansi c, semantics for unsigned.
*/

Node *
typecheck(Node *n)
{
	int s1, s2, sign;

	switch(/*nodetype*/n->type){
	case OSYM:
		switch(n->sym->type){
		case SYMNONE: error("undeclared '%s'", n->sym->name); break;
		case SYMVAR: n->typ = n->sym->typ; break;
		default: sysfatal("typecheck: unknown symbol type %d", n->sym->type);
		}
		break;
	case ONUM:
		if((vlong)n->num >= -0x80000000LL && (vlong)n->num <= 0x7fffffffLL)
			n->typ = type(TYPINT, 4, 1);
		else
			n->typ = type(TYPINT, 8, 1);
		break;
	case OSTR:
		n->typ = type(TYPSTRING);
		break;
	case OBIN:
		n->n1 = typecheck(n->n1);
		n->n2 = typecheck(n->n2);
		if(n->n1->typ == nil || n->n2->typ == nil)
			break;
		if(n->n1->typ->type != TYPINT){
			error("%τ not allowed in operation", n->n1->typ);
			break;
		}
		if(n->n2->typ->type != TYPINT){
			error("%τ not allowed in operation", n->n2->typ);
			break;
		}
		s1 = n->n1->typ->size;
		s2 = n->n2->typ->size;
		sign = n->n1->typ->sign && n->n2->typ->sign;
		switch(n->op){
		case OPADD:
		case OPSUB:
		case OPMUL:
		case OPDIV:
		case OPMOD:
		case OPAND:
		case OPOR:
		case OPXOR:
		case OPXNOR:
			n->typ = type(TYPINT, 8, sign);
			if(s1 > 4 || s2 > 4){
				n->n1 = icast(8, sign, n->n1);
				n->n2 = icast(8, sign, n->n2);
				return n;
			}else{
				n->n1 = icast(4, sign, n->n1);
				n->n2 = icast(4, sign, n->n2);
				return icast(4, sign, n);
			}
		case OPEQ:
		case OPNE:
		case OPLT:
		case OPLE:
			n->typ = type(TYPINT, 4, sign);
			if(s1 > 4 || s2 > 4){
				n->n1 = icast(8, sign, n->n1);
				n->n2 = icast(8, sign, n->n2);
				return n;
			}else{
				n->n1 = icast(4, sign, n->n1);
				n->n2 = icast(4, sign, n->n2);
				return n;
			}
		case OPLAND:
		case OPLOR:
			n->typ = type(TYPINT, 4, sign);
			return n;
		case OPLSH:
		case OPRSH:
			if(n->n1->typ->size <= 4)
				n->n1 = icast(4, n->n1->typ->sign, n->n1);
			n->typ = n->n1->typ;
			return icast(n->typ->size, n->typ->sign, n);
		default:
			sysfatal("typecheck: unknown op %d", n->op);
		}
		break;
	case OCAST:
		n->n1 = typecheck(n->n1);
		if(n->n1->typ == nil)
			break;
		if(n->typ->type == TYPINT && n->n1->typ->type == TYPINT){
		}else if(n->typ == n->n1->typ){
		}else if(n->typ->type == TYPSTRING && n->n1->typ->type == TYPINT){
		}else
			error("can't cast from %τ to %τ", n->n1->typ, n->typ);
		break;
	case OLNOT:
		n->n1 = typecheck(n->n1);
		if(n->n1->typ == nil)
			break;
		if(n->n1->typ->type != TYPINT){
			error("%τ not allowed in operation", n->n1->typ);
			break;
		}
		n->typ = type(TYPINT, 4, 1);
		break;
	case OTERN:
		n->n1 = typecheck(n->n1);
		n->n2 = typecheck(n->n2);
		n->n3 = typecheck(n->n3);
		if(n->n1->typ == nil || n->n2->typ == nil || n->n3->typ == nil)
			break;
		if(n->n1->typ->type != TYPINT){
			error("%τ not allowed in operation", n->n1->typ);
			break;
		}
		if(n->n2->typ->type == TYPINT || n->n3->typ->type == TYPINT){
			sign = n->n2->typ->sign && n->n3->typ->sign;
			s1 = n->n2->typ->size;
			s2 = n->n3->typ->size;
			if(s1 > 4 || s2 > 4){
				n->n2 = icast(8, sign, n->n2);
				n->n3 = icast(8, sign, n->n3);
				n->typ = type(TYPINT, 8, sign);
				return n;
			}else{
				n->n2 = icast(4, sign, n->n2);
				n->n3 = icast(4, sign, n->n3);
				n->typ = type(TYPINT, 4, sign);
				return n;
			}
		}else if(n->n2->typ == n->n3->typ){
			n->typ = n->n2->typ;
		}else
			error("don't know how to do ternary with %τ and %τ", n->n2->typ, n->n3->typ);
		break;
	case ORECORD:
	default: sysfatal("typecheck: unknown node type %α", n->type);
	}
	return n;
}

vlong
evalop(int op, int sign, vlong v1, vlong v2)
{
	switch(/*oper*/op){
	case OPADD: return v1 + v2; break;
	case OPSUB: return v1 - v2; break;
	case OPMUL: return v1 * v2; break;
	case OPDIV: if(v2 == 0) sysfatal("division by zero"); return sign ? v1 / v2 : (uvlong)v1 / (uvlong)v2; break;
	case OPMOD: if(v2 == 0) sysfatal("division by zero"); return sign ? v1 % v2 : (uvlong)v1 % (uvlong)v2; break;
	case OPAND: return v1 & v2; break;
	case OPOR: return v1 | v2; break;
	case OPXOR: return v1 ^ v2; break;
	case OPXNOR: return ~(v1 ^ v2); break;
	case OPLSH:
		if((u64int)v2 >= 64)
			return 0;
		else
			return v1 << v2;
		break;
	case OPRSH:
		if(sign){
			if((u64int)v2 >= 64)
				return v1 >> 63;
			else
				return v1 >> v2;
		}else{
			if((u64int)v2 >= 64)
				return 0;
			else
				return (u64int)v1 >> v2;
		}
		break;
	case OPEQ: return v1 == v2; break;
	case OPNE: return v1 != v2; break;
	case OPLT: return v1 < v2; break;
	case OPLE: return v1 <= v2; break;
	case OPLAND: return v1 && v2; break;
	case OPLOR: return v1 || v2; break;
	default:
		sysfatal("cfold: unknown op %.2x", op);
		return 0;
	}

}

Node *
addtype(Type *t, Node *n)
{
	n->typ = t;
	return n;
}

/* fold constants */

static Node *
cfold(Node *n)
{
	switch(/*nodetype*/n->type){
	case ONUM:
	case OSYM:
	case OSTR:
		return n;
	case OBIN:
		n->n1 = cfold(n->n1);
		n->n2 = cfold(n->n2);
		if(n->n1->type != ONUM || n->n2->type != ONUM)
			return n;
		return addtype(n->typ, node(ONUM, evalop(n->op, n->typ->sign, n->n1->num, n->n2->num)));
	case OLNOT:
		n->n1 = cfold(n->n1);
		if(n->n1->type == ONUM)
			return addtype(n->typ, node(ONUM, !n->n1->num));
		return n;
	case OTERN:
		n->n1 = cfold(n->n1);
		n->n2 = cfold(n->n2);
		n->n3 = cfold(n->n3);
		if(n->n1->type == ONUM)
			return n->n1->num ? n->n2 : n->n3;
		return n;
	case OCAST:
		n->n1 = cfold(n->n1);
		if(n->n1->type != ONUM || n->typ->type != TYPINT)
			return n;
		switch(n->typ->size << 4 | n->typ->sign){
		case 0x10: return addtype(n->typ, node(ONUM, (vlong)(u8int)n->n1->num));
		case 0x11: return addtype(n->typ, node(ONUM, (vlong)(s8int)n->n1->num));
		case 0x20: return addtype(n->typ, node(ONUM, (vlong)(u16int)n->n1->num));
		case 0x21: return addtype(n->typ, node(ONUM, (vlong)(s16int)n->n1->num));
		case 0x40: return addtype(n->typ, node(ONUM, (vlong)(u32int)n->n1->num));
		case 0x41: return addtype(n->typ, node(ONUM, (vlong)(s32int)n->n1->num));
		case 0x80: return addtype(n->typ, node(ONUM, n->n1->num));
		case 0x81: return addtype(n->typ, node(ONUM, n->n1->num));
		}
		return n;
	case ORECORD:
	default:
		fprint(2, "cfold: unknown type %α\n", n->type);
		return n;
	}
}

/* calculate the minimum record size for each node of the expression */
static Node *
calcrecsize(Node *n)
{
	switch(/*nodetype*/n->type){
	case ONUM:
	case OSTR:
		n->recsize = 0;
		break;
	case OSYM:
		switch(n->sym->type){
		case SYMVAR:
			switch(n->sym->idx){
			case DTV_TIME:
			case DTV_PROBE:
				n->recsize = 0;
				break;
			default:
				n->recsize = n->typ->size;
				break;
			}
			break;
		default: sysfatal("calcrecsize: unknown symbol type %d", n->sym->type); return nil;
		}
		break;
	case OBIN:
		n->n1 = calcrecsize(n->n1);
		n->n2 = calcrecsize(n->n2);
		n->recsize = min(n->typ->size, n->n1->recsize + n->n2->recsize);
		break;
	case OLNOT:
		n->n1 = calcrecsize(n->n1);
		n->recsize = min(n->typ->size, n->n1->recsize);
		break;
	case OCAST:
		n->n1 = calcrecsize(n->n1);
		if(n->typ->type == TYPSTRING)
			n->recsize = n->typ->size;
		else
			n->recsize = min(n->typ->size, n->n1->recsize);
		break;
	case OTERN:
		n->n1 = calcrecsize(n->n1);
		n->n2 = calcrecsize(n->n2);
		n->n3 = calcrecsize(n->n3);
		n->recsize = min(n->typ->size, n->n1->recsize + n->n2->recsize + n->n3->recsize);
		break;
	case ORECORD:
	default: sysfatal("calcrecsize: unknown type %α", n->type); return nil;
	}
	return n;
}

/* insert ORECORD nodes to mark the subexpression that we will pass to the kernel */
static Node *
insrecord(Node *n)
{
	if(n->recsize == 0)
		return n;
	if(n->typ->size == n->recsize)
		return addtype(n->typ, node(ORECORD, n));
	switch(/*nodetype*/n->type){
	case ONUM:
	case OSTR:
	case OSYM:
		break;
	case OBIN:
		n->n1 = insrecord(n->n1);
		n->n2 = insrecord(n->n2);
		break;
	case OLNOT:
	case OCAST:
		n->n1 = insrecord(n->n1);
		break;
	case OTERN:
		n->n1 = insrecord(n->n1);
		n->n2 = insrecord(n->n2);
		n->n3 = insrecord(n->n3);
		break;
	case ORECORD:
	default: sysfatal("insrecord: unknown type %α", n->type); return nil;
	}
	return n;
}

/*
	delete useless casts.
	going down we determine the number of bits (m) needed to be correct at each stage.
	going back up we determine the number of bits (n->databits) which can be either 0 or 1.
	all other bits are either zero (n->upper == UPZX) or sign-extended (n->upper == UPSX).
	note that by number of bits we always mean a consecutive block starting from the LSB.
	
	we can delete a cast if it either affects only bits not needed (according to m) or
	if it's a no-op (according to databits, upper).
*/
static Node *
elidecasts(Node *n, int m)
{
	switch(/*nodetype*/n->type){
	case OSTR:
		return n;
	case ONUM:
		n->databits = n->typ->size * 8;
		n->upper = n->typ->sign ? UPSX : UPZX;
		break;
	case OSYM:
		/* TODO: make less pessimistic */
		n->databits = 64;
		break;
	case OBIN:
		switch(/*oper*/n->op){
		case OPADD:
		case OPSUB:
			n->n1 = elidecasts(n->n1, m);
			n->n2 = elidecasts(n->n2, m);
			n->databits = min(64, max(n->n1->databits, n->n2->databits) + 1);
			n->upper = n->n1->upper | n->n2->upper;
			break;
		case OPMUL:
			n->n1 = elidecasts(n->n1, m);
			n->n2 = elidecasts(n->n2, m);
			n->databits = min(64, n->n1->databits + n->n2->databits);
			n->upper = n->n1->upper | n->n2->upper;
			break;
		case OPAND:
		case OPOR:
		case OPXOR:
		case OPXNOR:
			n->n1 = elidecasts(n->n1, m);
			n->n2 = elidecasts(n->n2, m);
			if(n->op == OPAND && (n->n1->upper == UPZX || n->n2->upper == UPZX)){
				n->upper = UPZX;
				if(n->n1->upper == UPZX && n->n2->upper == UPZX)
					n->databits = min(n->n1->databits, n->n2->databits);
				else if(n->n1->upper == UPZX)
					n->databits = n->n1->databits;
				else
					n->databits = n->n2->databits;
			}else{
				n->databits = max(n->n1->databits, n->n2->databits);
				n->upper = n->n1->upper | n->n2->upper;
			}
			break;
		case OPLSH:
			n->n1 = elidecasts(n->n1, m);
			n->n2 = elidecasts(n->n2, 64);
			if(n->n2->type == ONUM && n->n2->num >= 0 && n->n1->databits + (uvlong)n->n2->num <= 64)
				n->databits = n->n1->databits + n->n2->num;
			else
				n->databits = 64;
			n->upper = n->n1->upper;
			break;
		case OPRSH:
			n->n1 = elidecasts(n->n1, 64);
			n->n2 = elidecasts(n->n2, 64);
			if(n->n1->upper == n->typ->sign){
				n->databits = n->n1->databits;
				n->upper = n->n1->upper;
			}else{
				n->databits = 64;
				n->upper = UPZX;
			}
			break;
		case OPEQ:
		case OPNE:
		case OPLT:
		case OPLE:
		case OPLAND:
		case OPLOR:
			n->n1 = elidecasts(n->n1, 64);
			n->n2 = elidecasts(n->n2, 64);
			n->databits = 1;
			n->upper = UPZX;
			break;
		case OPDIV:
		case OPMOD:
		default:
			n->n1 = elidecasts(n->n1, 64);
			n->n2 = elidecasts(n->n2, 64);
			n->databits = 64;
			n->upper = UPZX;
			break;
		}
		break;
	case OLNOT:
		n->n1 = elidecasts(n->n1, 64);
		n->databits = 1;
		n->upper = UPZX;
		break;
	case OCAST:
		switch(n->typ->type){
		case TYPINT:
			n->n1 = elidecasts(n->n1, min(n->typ->size * 8, m));
			if(n->n1->databits < n->typ->size * 8 && n->n1->upper == n->typ->sign){
				n->databits = n->n1->databits;
				n->upper = n->n1->upper;
			}else{
				n->databits = n->typ->size * 8;
				n->upper = n->typ->sign ? UPSX : UPZX;
			}
			if(n->typ->size * 8 >= m) return n->n1;
			if(n->typ->size * 8 >= n->n1->databits && n->typ->sign == n->n1->upper) return n->n1;
			if(n->typ->size * 8 > n->n1->databits && n->typ->sign && !n->n1->upper) return n->n1;
			break;
		case TYPSTRING:
			n->n1 = elidecasts(n->n1, 64);
			break;
		default:
			sysfatal("elidecasts: don't know how to cast %τ to %τ", n->n1->typ, n->typ);
		}
		break;
	case ORECORD:
		n->n1 = elidecasts(n->n1, min(n->typ->size * 8, m));
		if(n->n1->databits < n->typ->size * 8 && n->n1->upper == n->typ->sign){
			n->databits = n->n1->databits;
			n->upper = n->n1->upper;
		}else{
			n->databits = n->typ->size * 8;
			n->upper = n->typ->sign ? UPSX : UPZX;
		}
		break;
	case OTERN:
		n->n1 = elidecasts(n->n1, 64);
		n->n2 = elidecasts(n->n2, m);
		n->n3 = elidecasts(n->n3, m);
		if(n->n2->upper == n->n3->upper){
			n->databits = max(n->n2->databits, n->n3->databits);
			n->upper = n->n2->upper;
		}else{
			if(n->n3->upper == UPSX)
				n->databits = max(min(64, n->n2->databits + 1), n->n3->databits);
			else
				n->databits = max(min(64, n->n3->databits + 1), n->n2->databits);
			n->upper = UPSX;
		}
		break;
	default: sysfatal("elidecasts: unknown type %α", n->type);
	}
//	print("need %d got %d%c %ε\n", n->needbits, n->databits, "ZS"[n->upper], n);
	return n;
}


Node *
exprcheck(Node *n, int pred)
{
	if(dflag) print("start       %ε\n", n);
	n = typecheck(n);
	if(errors) return n;
	if(dflag) print("typecheck   %ε\n", n);
	n = cfold(n);
	if(dflag) print("cfold       %ε\n", n);
	if(!pred){
		n = insrecord(calcrecsize(n));
		if(dflag) print("insrecord   %ε\n", n);
	}
	n = elidecasts(n, 64);
	if(dflag) print("elidecasts  %ε\n", n);
	return n;
}
