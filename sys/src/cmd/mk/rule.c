#include	"mk.h"

static Rule *lr, *lmr;
static int nrules = 0;

void
addrule(char *head, Word *tail, char *body, Word *ahead, int attr, int line, char *prog)
{
	Rule *r, *rr;
	Symtab *sym;
	int reuse;

	r = 0;
	reuse = 0;
	sym = symlook(head, S_TARGET, 0);
	if(sym){
		for(r = sym->u.ptr; r; r = r->chain){
			if(wcmp(r->tail, tail) == 0){
				reuse = 1;
				break;
			}
		}
	}
	if(r == 0)
		r = (Rule *)Malloc(sizeof(Rule));

	r->tail = tail;
	r->recipe = body;
	r->line = line;
	r->file = mkinfile;
	r->attr = attr;
	r->alltargets = ahead;
	r->prog = prog;
	r->rule = nrules++;

	if(!reuse){
		r->next = 0;
		r->chain = 0;
		if(sym == 0){
			sym = symlook(head, S_TARGET, 1);
			sym->u.ptr = r;
		} else {
			rr = sym->u.ptr;
			r->chain = rr->chain;
			rr->chain = r;
		}
	}
	r->target = sym;

	if((attr&REGEXP) || charin(head, "%&")){
		r->attr |= META;
		if(reuse)
			return;
		if(attr&REGEXP){
			patrule = r;
			r->pat = regcomp(head);
		}
		if(metarules == 0)
			metarules = lmr = r;
		else {
			lmr->next = r;
			lmr = r;
		}
	} else {
		if(reuse)
			return;
		r->pat = 0;
		if(rules == 0)
			rules = lr = r;
		else {
			lr->next = r;
			lr = r;
		}
	}
}

void
dumpr(char *s, Rule *r)
{
	char *t;

	Bprint(&bout, "%s: start=%p\n", s, r);
	for(; r; r = r->next){
		Bprint(&bout, "\tRule %p: %s:%d attr=%x next=%p chain=%p alltarget='%s'",
			r, r->file, r->line, r->attr, r->next, r->chain,
			t = wtos(r->alltargets)), free(t);
		if(r->prog)
			Bprint(&bout, " prog='%s'", r->prog);
		Bprint(&bout, "\n\ttarget=%s: %s\n", r->target->name,
			t = wtos(r->tail)), free(t);
		Bprint(&bout, "\trecipe@%p='%s'\n", r->recipe, r->recipe);
	}
}

char *
rulecnt(void)
{
	char *s;

	s = Malloc(nrules);
	memset(s, 0, nrules);
	return(s);
}
