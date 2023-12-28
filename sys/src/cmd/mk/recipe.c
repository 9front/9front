#include	"mk.h"

int
dorecipe(Node *node)
{
	int did = 0;
	char buf[BIGBLOCK], cwd[256];
	Arc *a, *aa;
	Node *n;
	Rule *r = 0;
	Symtab *s;
	Word *head, *ahead, *lp, *ln, *w, **ww, **aw;

	aa = 0;
	/*
		pick up the rule
	*/
	for(a = node->prereqs; a; a = a->next)
		if(*a->r->recipe)
			r = (aa = a)->r;
	/*
		no recipe? go to buggery!
	*/
	if(r == 0){
		if(!(node->flags&VIRTUAL) && !(node->flags&NORECIPE)){
			if(getwd(cwd, sizeof cwd))
				fprint(2, "mk: no recipe to make '%s' in directory %s\n", node->name, cwd);
			else
				fprint(2, "mk: no recipe to make '%s'\n", node->name);
			Exit();
		}
		if(strchr(node->name, '(') && node->time == 0)
			MADESET(node, MADE);
		else
			update(0, node);
		if(tflag){
			if(!(node->flags&VIRTUAL))
				touch(node->name);
			else if(explain)
				Bprint(&bout, "no touch of virtual '%s'\n", node->name);
		}
		return(did);
	}
	/*
		build the node list
	*/
	node->next = 0;
	head = 0, ww = &head;
	ahead = 0, aw = &ahead;
	if(r->attr&REGEXP){
		*ww = newword(node->name);
		*aw = newword(node->name);
	} else {
		for(w = r->alltargets; w; w = w->next){
			if(r->attr&META)
				subst(aa->stem, w->s, buf, sizeof(buf));
			else
				strecpy(buf, buf + sizeof buf - 1, w->s);
			*aw = newword(buf), aw = &(*aw)->next;
			if((s = symlook(buf, S_NODE, 0)) == 0)
				continue;	/* not a node we are interested in */
			n = (Node*)s->u.ptr;
			if(aflag == 0 && n->time) {
				for(a = n->prereqs; a; a = a->next)
					if(a->n && outofdate(n, a, 0))
						break;
				if(a == 0)
					continue;
			}
			*ww = newword(buf), ww = &(*ww)->next;
			if(n == node) continue;
			n->next = node->next;
			node->next = n;
		}
	}
	for(n = node; n; n = n->next)
		if((n->flags&READY) == 0)
			return(did);
	/*
		gather the params for the job
	*/
	lp = ln = 0;
	for(n = node; n; n = n->next){
		for(a = n->prereqs; a; a = a->next){
			if(a->n){
				wadd(&lp, a->n->name);
				if(outofdate(n, a, 0)){
					wadd(&ln, a->n->name);
					if(explain)
						fprint(1, "%s(%ld) < %s(%ld)\n",
							n->name, n->time, a->n->name, a->n->time);
				}
			} else {
				if(explain)
					fprint(1, "%s has no prerequisites\n", n->name);
			}
		}
		MADESET(n, BEINGMADE);
	}
	run(newjob(r, node, aa->stem, aa->match, lp, ln, head, ahead));
	return(1);
}
