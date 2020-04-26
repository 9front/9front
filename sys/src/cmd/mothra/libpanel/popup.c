/*
 * popup
 *	looks like a group, except diverts hits on certain buttons to
 *	panels that it temporarily pops up.
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "pldefs.h"
typedef struct Popup Popup;
struct Popup{
	Image *save;			/* where to save what the popup covers */
	Panel *pop[3];			/* what to pop up */
};
void pl_drawpopup(Panel *p){
	USED(p);
}
int pl_hitpopup(Panel *g, Mouse *m){
	Panel *p;
	Point d;
	Popup *pp;

	pp=g->data;
	if(g->state==UP){
		switch(m->buttons&7){
		case 0: p=g->child; break;
		case 1:	p=pp->pop[0]; g->state=DOWN1; break;
		case 2: p=pp->pop[1]; g->state=DOWN2; break;
		case 4: p=pp->pop[2]; g->state=DOWN3; break;
		default: p=0; break;
		}
		if(p==0){
			p=g->child;
			g->state=DOWN;
		}
		else if(g->state!=UP){
			plpack(p, screen->clipr);
			if(p->lastmouse)
				d=subpt(m->xy, divpt(addpt(p->lastmouse->r.min,
						     p->lastmouse->r.max), 2));
			else
				d=subpt(m->xy, divpt(addpt(p->r.min, p->r.max), 2));
			if(p->r.min.x+d.x<g->r.min.x) d.x=g->r.min.x-p->r.min.x;
			if(p->r.max.x+d.x>g->r.max.x) d.x=g->r.max.x-p->r.max.x;
			if(p->r.min.y+d.y<g->r.min.y) d.y=g->r.min.y-p->r.min.y;
			if(p->r.max.y+d.y>g->r.max.y) d.y=g->r.max.y-p->r.max.y;
			plmove(p, d);
			pp->save=allocimage(display, p->r, g->b->chan, 0, DNofill);
			if(pp->save!=0) draw(pp->save, p->r, g->b, 0, p->r.min);
			pl_invis(p, 0);
			pldraw(p, g->b);
		}
	}
	else{
		switch(g->state){
		default: SET(p); break;			/* can't happen! */
		case DOWN1: p=pp->pop[0]; break;
		case DOWN2: p=pp->pop[1]; break;
		case DOWN3: p=pp->pop[2]; break;
		case DOWN:  p=g->child;  break;
		}
		if((m->buttons&7)==0){
			if(g->state!=DOWN){
				if(pp->save!=0){
					draw(g->b, p->r, pp->save, 0, p->r.min);
					freeimage(pp->save);
					pp->save=0;
				}
				pl_invis(p, 1);
			}
			g->state=UP;
		}
	}
	plmouse(p, m);
	if((m->buttons&7)==0)
		g->state=UP;
	return (m->buttons&7)!=0;
}
void pl_typepopup(Panel *g, Rune c){
	USED(g, c);
}
Point pl_getsizepopup(Panel *g, Point children){
	USED(g);
	return children;
}
void pl_childspacepopup(Panel *g, Point *ul, Point *size){
	USED(g, ul, size);
}
int pl_pripopup(Panel *, Point){
	return PRI_POPUP;
}
void plinitpopup(Panel *v, int flags, Panel *pop0, Panel *pop1, Panel *pop2){
	Popup *pp;
	pp=v->data;
	v->flags=flags;
	v->pri=pl_pripopup;
	v->state=UP;
	v->draw=pl_drawpopup;
	v->hit=pl_hitpopup;
	v->type=pl_typepopup;
	v->getsize=pl_getsizepopup;
	v->childspace=pl_childspacepopup;
	pp->pop[0]=pop0;
	pp->pop[1]=pop1;
	pp->pop[2]=pop2;
	pp->save=0;
	v->kind="popup";
}
Panel *plpopup(Panel *parent, int flags, Panel *pop0, Panel *pop1, Panel *pop2){
	Panel *v;
	v=pl_newpanel(parent, sizeof(Popup));
	plinitpopup(v, flags, pop0, pop1, pop2);
	return v;
}
