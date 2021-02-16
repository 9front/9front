#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "pldefs.h"

void plgrabkb(Panel *g){
	Panel *o;
	o=plkbfocus;
	plkbfocus=nil;
	if(o && o!=g) /* redraw if lost focus */
		pldraw(o, o->b);
	plkbfocus=g;
}
void plkeyboard(Rune c){
	if(plkbfocus)
		plkbfocus->type(plkbfocus, c);
}

/*
 * Return the most leafward, highest priority panel containing p
 */
Panel *pl_ptinpanel(Point p, Panel *g){
	Panel *v;
	for(;g;g=g->next) if(ptinrect(p, g->r)){
		v=pl_ptinpanel(p, g->child);
		if(v && v->pri(v, p)>=g->pri(g, p)) return v;
		return g;
	}
	return 0;
}
void plmouse(Panel *g, Mouse *m){
	Panel *hit, *last;
	if(g->flags&REMOUSE)
		hit=g->lastmouse;
	else{
		hit=pl_ptinpanel(m->xy, g);
		last=g->lastmouse;
		if(last && last!=hit){
			m->buttons|=OUT;
			last->hit(last, m);
			m->buttons&=~OUT;
		}
	}
	if(hit){
		if(hit->hit(hit, m))
			g->flags|=REMOUSE;
		else
			g->flags&=~REMOUSE;
		g->lastmouse=hit;
	}
}
