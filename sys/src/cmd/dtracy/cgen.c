#include <u.h>
#include <libc.h>
#include <dtracy.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

u16int regsused = 1;
u32int cbuf[256];
int ncbuf;
int labtab[256];
int nlab;

static void
emit(u32int x)
{
	assert(ncbuf < nelem(cbuf));
	cbuf[ncbuf++] = x;
}

static int
regalloc(void)
{
	u16int v;
	int n;

	if(regsused == 0xffff){
		error("out of registers");
		return 0;
	}
	v = regsused + 1 & ~regsused;
	regsused ^= v;
	n = 0;
	if((u8int)v == 0) {v >>= 8; n += 8;}
	if((v & 0xf) == 0) {v >>= 4; n += 4;}
	if((v & 3) == 0) {v >>= 2; n += 2;}
	return n + (v >> 1);
}

static void
regfree(int n)
{
	assert((regsused & 1<<n) != 0);
	assert(n != 0);
	regsused &= ~(1<<n);
}

static int
popcount(u64int s)
{
	s = (s & 0x5555555555555555ULL) + (s >> 1 & 0x5555555555555555ULL);
	s = (s & 0x3333333333333333ULL) + (s >> 2 & 0x3333333333333333ULL);
	s = (s & 0x0F0F0F0F0F0F0F0FULL) + (s >> 4 & 0x0F0F0F0F0F0F0F0FULL);
	s = (s & 0x00FF00FF00FF00FFULL) + (s >> 8 & 0x00FF00FF00FF00FFULL);
	s = (s & 0x0000FFFF0000FFFFULL) + (s >> 16 & 0x0000FFFF0000FFFFULL);
	return (u32int)s + (u32int)(s >> 32);
}

static int
constenc(s64int val)
{
	int i, r;
	s64int x;

	r = 0;
	do{
		i = popcount(val ^ val - 1) - 1;
		x = val << 54 - i >> 54;
		if(r == 0){
			r = regalloc();
			emit(DTE_LDI << 24 | (x & 0x3ff) << 14 | i << 8 | r);
		}else
			emit(DTE_XORI << 24 | (x & 0x3ff) << 14 | i << 8 | r);
		val ^= x << i;
	}while(val != 0);
	return r;
}

static int egen(Node *);

static void
condgen(Node *n, int invert, int truelab)
{
	int r1, r2, l1, op;

	if(n->type != OBIN) goto other;
	switch(n->op){
	case OPEQ: op = DTE_BEQ; goto cmp;
	case OPNE: op = DTE_BNE; goto cmp;
	case OPLT: op = DTE_BLT; goto cmp;
	case OPLE: op = DTE_BLE;
	cmp:
		r1 = egen(n->n1);
		r2 = egen(n->n2);
		if(invert)
			emit(DTE(op ^ 1, r2, r1, truelab));
		else
			emit(DTE(op, r1, r2, truelab));
		regfree(r1);
		regfree(r2);
		break;
	case OPLOR:
	case OPLAND:
		if(invert ^ n->op == OPLOR){
			condgen(n->n1, invert, truelab);
			condgen(n->n2, invert, truelab);
		}else{
			l1 = nlab++;
			condgen(n->n1, !invert, l1);
			condgen(n->n2, invert, truelab);
			labtab[l1] = ncbuf;
		}
		break;
	default:
	other:
		r1 = egen(n);
		emit(DTE(DTE_BNE ^ invert, r1, 0, truelab));
		regfree(r1);
		break;
	}
}

static int
condvgen(Node *n, int invert)
{
	int r, l1, l2, op;

	if(n->type == OLNOT)
		return condvgen(n->n1, !invert);
	if(n->type != OBIN) goto other;
	switch(n->op){
	case OPEQ: op = DTE_SEQ; goto cmp;
	case OPNE: op = DTE_SNE; goto cmp;
	case OPLT: op = DTE_SLT; goto cmp;
	case OPLE: op = DTE_SLE;
	cmp:
		if(invert)
			return egen(node(OBIN, op ^ 1, n->n2, n->n1));
		return egen(n);
	case OPLOR:
	case OPLAND:
		if(invert ^ n->op == OPLOR){
			l1 = nlab++;
			l2 = nlab++;
			condgen(n->n1, invert, l1);
			r = condvgen(n->n2, invert);
			emit(DTE(DTE_BEQ, 0, 0, l2));
			labtab[l1] = ncbuf;
			emit(DTE(DTE_LDI, 0, 1<<6, r));
			labtab[l2] = ncbuf;
			return r;
		}else{
			l1 = nlab++;
			l2 = nlab++;
			condgen(n->n1, !invert, l1);
			r = condvgen(n->n2, invert);
			emit(DTE(DTE_BEQ, 0, 0, l2));
			labtab[l1] = ncbuf;
			emit(DTE(DTE_LDI, 0, 0<<6, r));
			labtab[l2] = ncbuf;
			return r;
		}
	default:
	other:
		r = egen(n);
		emit(DTE(DTE_SNE ^ invert, r, 0, r));
		return r;
	}
}

static int
egen(Node *n)
{
	int r1, r2, rt, l1, l2, op;

	switch(/*nodetype*/n->type){
	case ONUM:
		return constenc(n->num);
	case OSYM:
		switch(n->sym->type){
		case SYMVAR:
			rt = regalloc();
			emit(DTE(DTE_LDV, n->sym->idx, rt, 0));
			return rt;
		default: sysfatal("egen: unknown symbol type %d", n->sym->type); return 0;
		}
	case OBIN:
		switch(/*oper*/n->op){
		case OPLAND:
		case OPLOR:
			return condvgen(n, 0);
		case OPADD: op = DTE_ADD; break;
		case OPSUB: op = DTE_SUB; break;
		case OPMUL: op = DTE_MUL; break;
		case OPDIV: op = n->typ->sign ? DTE_SDIV : DTE_UDIV; break;
		case OPMOD: op = n->typ->sign ? DTE_SMOD : DTE_UMOD; break;
		case OPAND: op = DTE_AND; break;
		case OPOR: op = DTE_OR; break;
		case OPXOR: op = DTE_XOR; break;
		case OPLSH: op = DTE_LSL; break;
		case OPRSH: op = n->typ->sign ? DTE_ASR : DTE_LSR; break;
		case OPEQ: op = DTE_SEQ; break;
		case OPNE: op = DTE_SNE; break;
		case OPLT: op = DTE_SLT; break;
		case OPLE: op = DTE_SLE; break;
		case OPXNOR: op = DTE_XNOR; break;
		default: sysfatal("egen: unknown op %d", n->op); return 0;
		}
		r1 = egen(n->n1);
		r2 = egen(n->n2);
		regfree(r1);
		regfree(r2);
		rt = regalloc();
		emit(DTE(op, r1, r2, rt));
		return rt;
	case OTERN:
		l1 = nlab++;
		l2 = nlab++;
		condgen(n->n1, 1, l1);
		r1 = egen(n->n2);
		emit(DTE(DTE_BEQ, 0, 0, l2));
		labtab[l1] = ncbuf;
		r2 = egen(n->n3);
		if(r1 != r2)
			emit(DTE(DTE_OR, 0, r2, r1));
		labtab[l2] = ncbuf;
		return r1;
	case OLNOT:
		return condvgen(n, 0);
	case OCAST:
		switch(n->typ->type){
		case TYPINT:
			r1 = egen(n->n1);
			emit(DTE(n->typ->sign ? DTE_SXT : DTE_ZXT, r1, n->typ->size * 8, r1));
			return r1;
		case TYPSTRING:
			return egen(n->n1);
		default:
			sysfatal("egen: don't know how to cast %τ to %τ", n->n1->typ, n->typ);
		}
	case ORECORD:
	case OSTR:
	default: sysfatal("egen: unknown type %α", n->type); return 0;
	}
}

DTExpr *
codegen(Node *n)
{
	int r, i, t;
	DTExpr *ep;
	
	regsused = 1;
	ncbuf = 0;
	nlab = 0;
	r = egen(n);
	emit(DTE(DTE_RET, r, 0, 0));
	
	for(i = 0; i < ncbuf; i++)
		if((cbuf[i] >> 24 & 0xf0) == 0x30){
			t = labtab[cbuf[i] & 0xff];
			assert((uint)(t - i - 1) < 0x100);
			cbuf[i] = cbuf[i] & 0xffffff00 | t - i - 1;
		}
	
	ep = emalloc(sizeof(DTExpr) + ncbuf * sizeof(u32int));
	ep->n = ncbuf;
	ep->b = (void *)(ep + 1);
	memcpy(ep->b, cbuf, ncbuf * sizeof(u32int));
	return ep;
}

Node *
tracegen(Node *n, DTActGr *g, int *recoff)
{
	switch(/*nodetype*/n->type){
	case OSYM:
	case ONUM:
	case OSTR:
		break;
	case OBIN:
		n->n1 = tracegen(n->n1, g, recoff);
		n->n2 = tracegen(n->n2, g, recoff);
		break;
	case OLNOT:
		n->n1 = tracegen(n->n1, g, recoff);
		break;
	case OTERN:
		n->n1 = tracegen(n->n1, g, recoff);
		n->n2 = tracegen(n->n2, g, recoff);
		n->n3 = tracegen(n->n3, g, recoff);
		break;
	case OCAST:
		n->n1 = tracegen(n->n1, g, recoff);
		break;
	case ORECORD:
		switch(n->typ->type){
		case TYPINT:
			actgradd(g, (DTAct){ACTTRACE, codegen(n->n1), n->typ->size, noagg});
			break;
		case TYPSTRING:
			actgradd(g, (DTAct){ACTTRACESTR, codegen(n->n1), n->typ->size, noagg});
			break;
		default:
			sysfatal("tracegen: don't know how to record %τ", n->typ);
		}
		n->num = *recoff;
		*recoff += n->typ->size;
		return n;
	default: sysfatal("tracegen: unknown type %α", n->type); return nil;
	}
	return n;
}
