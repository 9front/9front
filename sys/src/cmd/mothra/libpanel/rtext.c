/*
 * Rich text with images.
 * Should there be an offset field, to do subscripts & kerning?
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "pldefs.h"
#include "rtext.h"

#define LEAD	4	/* extra space between lines */
#define BORD	2	/* extra border for images */

Rtext *pl_rtnew(Rtext **t, int space, int indent, Image *b, Panel *p, Font *f, char *s, int flags, void *user){
	Rtext *new;
	new=pl_emalloc(sizeof(Rtext));
	new->flags=flags;
	new->user=user;
	new->space=space;
	new->indent=indent;
	new->b=b;
	new->p=p;
	new->font=f;
	new->text=s;
	new->next=0;
	new->nextline=0;
	new->r=Rect(0,0,0,0);
	if(*t)
		(*t)->last->next=new;
	else
		*t=new;
	(*t)->last=new;
	return new;
}
Rtext *plrtpanel(Rtext **t, int space, int indent, Panel *p, void *user){
	return pl_rtnew(t, space, indent, 0, p, 0, 0, 1, user);
}
Rtext *plrtstr(Rtext **t, int space, int indent, Font *f, char *s, int flags, void *user){
	return pl_rtnew(t, space, indent, 0, 0, f, s, flags, user);
}
Rtext *plrtbitmap(Rtext **t, int space, int indent, Image *b, int flags, void *user){
	return pl_rtnew(t, space, indent, b, 0, 0, 0, flags, user);
}
void plrtfree(Rtext *t){
	Rtext *next;
	while(t){
		next=t->next;
		free(t);
		t=next;
	}
}
int pl_tabmin, pl_tabsize;
void pltabsize(int min, int size){
	pl_tabmin=min;
	pl_tabsize=size;
}
int pl_space(int space, int pos, int indent){
	if(space>=0) return space;
	switch(PL_OP(space)){
	default:
		return 0;
	case PL_TAB:
		return ((pos-indent+pl_tabmin)/pl_tabsize+PL_ARG(space))*pl_tabsize+indent-pos;
	}
}
/*
 * initialize rectangles & nextlines of text starting at t,
 * galley width is wid.  Returns the total length of the text
 */
int pl_rtfmt(Rtext *t, int wid){
	Rtext *tp, *eline;
	int ascent, descent, x, space, a, d, w, topy, indent;
	Point p;
	p=Pt(0,0);
	eline=t;
	while(t){
		ascent=0;
		descent=0;
		indent=space=pl_space(t->indent, 0, 0);
		x=0;
		tp=t;
		for(;;){
			if(tp->b){
				a=tp->b->r.max.y-tp->b->r.min.y+BORD;
				d=BORD;
				w=tp->b->r.max.x-tp->b->r.min.x+BORD*2;
			}
			else if(tp->p){
				/* what if plpack fails? */
				plpack(tp->p, Rect(0,0,wid,wid));
				plmove(tp->p, subpt(Pt(0,0), tp->p->r.min));
				a=tp->p->r.max.y-tp->p->r.min.y;
				d=0;
				w=tp->p->r.max.x-tp->p->r.min.x;
			}
			else{
				a=tp->font->ascent;
				d=tp->font->height-a;
				w=tp->wid=stringwidth(tp->font, tp->text);
			}
			if(x+w+space>wid) break;
			if(a>ascent) ascent=a;
			if(d>descent) descent=d;
			x+=w+space;
			tp=tp->next;
			if(tp==0){
				eline=0;
				break;
			}
			space=pl_space(tp->space, x, indent);
			if(space) eline=tp;
		}
		if(eline==t){	/* No progress!  Force fit the first block! */
			if(tp==t){
				if(a>ascent) ascent=a;
				if(d>descent) descent=d;
				eline=tp->next;
			}else
				eline=tp;
		}
		topy=p.y;
		p.y+=ascent;
		p.x=indent=pl_space(t->indent, 0, 0);
		for(;;){
			t->topy=topy;
			t->r.min.x=p.x;
			if(t->b){
				t->r.max.y=p.y+BORD;
				t->r.min.y=p.y-(t->b->r.max.y-t->b->r.min.y)-BORD;
				p.x+=(t->b->r.max.x-t->b->r.min.x)+BORD*2;
			}
			else if(t->p){
				t->r.max.y=p.y;
				t->r.min.y=p.y-t->p->r.max.y;
				p.x+=t->p->r.max.x;
			}
			else{
				t->r.min.y=p.y-t->font->ascent;
				t->r.max.y=t->r.min.y+t->font->height;
				p.x+=t->wid;
			}
			t->r.max.x=p.x;
			t->nextline=eline;
			t=t->next;
			if(t==eline) break;
			p.x+=pl_space(t->space, p.x, indent);
		}
		p.y+=descent+LEAD;
	}
	return p.y;
}
void pl_rtdraw(Image *b, Rectangle r, Rtext *t, int yoffs){
	Point offs, lp;
	Rectangle dr;
	Rectangle cr;
	Rectangle xr;

	xr=r;
	cr=b->clipr;
	if(!rectclip(&xr, cr))
		return;
	replclipr(b, b->repl, xr);
	pl_clr(b, r);
	lp=ZP;
	offs=subpt(r.min, Pt(0, yoffs));
	for(;t;t=t->next) if(!eqrect(t->r, Rect(0,0,0,0))){
		dr=rectaddpt(t->r, offs);
		if(dr.max.y>r.min.y
		&& dr.min.y<r.max.y){
			if(t->b){
				draw(b, insetrect(dr, BORD), t->b, 0, t->b->r.min);
				if(t->flags&PL_HOT) border(b, dr, 1, display->black, ZP);
			}
			else if(t->p){
				plmove(t->p, subpt(dr.min, t->p->r.min));
				pldraw(t->p, b);
			}
			else{
				string(b, dr.min, display->black, ZP, t->font, t->text);
				if(t->flags&PL_HOT){
					if(lp.y+1 != dr.max.y)
						lp = Pt(dr.min.x, dr.max.y-1);
					line(b, lp, Pt(dr.max.x, dr.max.y-1),
						Endsquare, Endsquare, 0,
						display->black, ZP);
					lp = Pt(dr.max.x, dr.max.y-1);
					goto Cont;
				}
			}
			lp=ZP;
		Cont:
			if(t->flags&PL_SEL)
				pl_highlight(b, dr);
		}
	}
	replclipr(b, b->repl, cr);
}
/*
 * Reposition text already drawn in the window.
 * We just move the pixels and update the positions of any
 * enclosed panels
 */
void pl_reposition(Rtext *t, Image *b, Point p, Rectangle r){
	Point offs;
	pl_cpy(b, p, r);
	offs=subpt(p, r.min);
	for(;t;t=t->next)
		if(!eqrect(t->r, Rect(0,0,0,0)) && !t->b && t->p)
			plmove(t->p, offs);
}
/*
 * Rectangle r of Image b contains an image of Rtext t, offset by oldoffs.
 * Redraw the text to have offset yoffs.
 */
void pl_rtredraw(Image *b, Rectangle r, Rtext *t, int yoffs, int oldoffs){
	int dy, size;
	dy=oldoffs-yoffs;
	size=r.max.y-r.min.y;
	if(dy>=size || -dy>=size)
		pl_rtdraw(b, r, t, yoffs);
	else if(dy<0){
		pl_reposition(t, b, r.min,
			Rect(r.min.x, r.min.y-dy, r.max.x, r.max.y));
		pl_rtdraw(b, Rect(r.min.x, r.max.y+dy, r.max.x, r.max.y),
			t, yoffs+size+dy);
	}
	else if(dy>0){
		pl_reposition(t, b, Pt(r.min.x, r.min.y+dy),
			Rect(r.min.x, r.min.y, r.max.x, r.max.y-dy));
		pl_rtdraw(b, Rect(r.min.x, r.min.y, r.max.x, r.min.y+dy), t, yoffs);
	}
}
Rtext *pl_rthit(Rtext *t, int yoffs, Point p, Point ul){
	Rectangle r;
	Point lp;
	if(t==0) return 0;
	p.x-=ul.x;
	p.y+=yoffs-ul.y;
	while(t->nextline && t->nextline->topy<=p.y) t=t->nextline;
	lp=ZP;
	for(;t!=0;t=t->next){
		if(t->topy>p.y) return 0;
		r = t->r;
		if((t->flags&PL_HOT) != 0 && t->b == nil && t->p == nil){
			if(lp.y == r.max.y && lp.x < r.min.x)
				r.min.x=lp.x;
			lp=r.max;
		} else
			lp=ZP;
		if(ptinrect(p, r)) return t;
	}
	return 0;
}

void plrtseltext(Rtext *t, Rtext *s, Rtext *e){
	while(t){
		t->flags &= ~PL_SEL;
		t = t->next;
	}
	if(s==0 || e==0)
		return;
	for(t=s; t!=0 && t!=e; t=t->next)
		;
	if(t==e){
		for(t=s; t!=e; t=t->next)
			t->flags |= PL_SEL;
	}else{
		for(t=e; t!=s; t=t->next)
			t->flags |= PL_SEL;
	}
	t->flags |= PL_SEL;
}

char *plrtsnarftext(Rtext *w){
	char *b, *p, *e;
	int n;

	p = e = 0;
	for(; w; w = w->next){
		if((w->flags&PL_SEL)==0 || w->b!=0 || w->p!=0 || w->text==0)
			continue;
		n = strlen(w->text)+4;
		if((b = realloc(p, (e+n) - p)) == nil)
			break;
		e = (e - p) + b;
		p = b;
		if(w->space == 0)
			e += sprint(e, "%s", w->text);
		else if(w->space > 0)
			e += sprint(e, " %s", w->text);
		else if(PL_OP(w->space) == PL_TAB)
			e += sprint(e, "\t%s", w->text);
		if(w->nextline == w->next)
			e += sprint(e, "\n");
	}
	return p;
}
