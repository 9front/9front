/*
 * Fonted text viewer, calls out to code in rtext.c
 *
 * Should redo this to copy the already-visible parts on scrolling & only
 * update the newly appearing stuff -- then the offscreen assembly bitmap can go away.
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "pldefs.h"

typedef struct Textview Textview;
struct Textview{
	void (*hit)(Panel *, int, Rtext *); /* call back to user on hit */
	Rtext *text;			/* text */
	Point offs;			/* offset of left/top of screen */
	Rtext *hitword;			/* text to hilite */
	Rtext *hitfirst;		/* first word in range select */
	int twid;			/* text width (visible) */
	int thgt;			/* text height (total) */
	int maxwid;			/* width of longest line */
	Point minsize;			/* smallest acceptible window size */
	int buttons;
};

void pl_setscrpos(Panel *p, Textview *tp, Rectangle r){
	Panel *sb;
	int lo, hi;

	lo=tp->offs.y;
	hi=lo+r.max.y-r.min.y;	/* wrong? */
	sb=p->yscroller;	
	if(sb && sb->setscrollbar)
		sb->setscrollbar(sb, lo, hi, tp->thgt);
	lo=tp->offs.x;
	hi=lo+r.max.x-r.min.x;
	sb=p->xscroller;
	if(sb && sb->setscrollbar)
		sb->setscrollbar(sb, lo, hi, tp->maxwid);
}
void pl_drawtextview(Panel *p){
	int twid;
	Rectangle r;
	Textview *tp;
	Point size;

	tp=p->data;
	r=pl_outline(p->b, p->r, TUP);
	twid=r.max.x-r.min.x;
	if(twid!=tp->twid){
		tp->twid=twid;
		size=pl_rtfmt(tp->text, tp->twid);
		p->scr.size.x=tp->maxwid=size.x;
		p->scr.size.y=tp->thgt=size.y;
	}
	p->scr.pos = tp->offs;
	pl_rtdraw(p->b, r, tp->text, tp->offs);
	pl_setscrpos(p, tp, r);
}
/*
 * If t is a panel word, pass the mouse event on to it
 */
void pl_passon(Rtext *t, Mouse *m){
	if(t && t->b==0 && t->p!=0)
		plmouse(t->p, m);
}
int pl_hittextview(Panel *p, Mouse *m){
	Rtext *oldhitword, *oldhitfirst;
	int hitme, oldstate;
	Point ul, size;
	Textview *tp;

	tp=p->data;
	hitme=0;
	oldstate=p->state;
	oldhitword=tp->hitword;
	oldhitfirst=tp->hitfirst;
	if(oldhitword==oldhitfirst)
		pl_passon(oldhitword, m);
	if(m->buttons&OUT)
		p->state=PASSIVE;
	else if(m->buttons&7){
		p->state=DOWN;
		tp->buttons=m->buttons;
		if(oldhitword==0 || oldhitword->p==0 || (oldhitword->p->flags&REMOUSE)==0){
			ul=p->r.min;
			size=subpt(p->r.max, p->r.min);
			pl_interior(p->state, &ul, &size);
			tp->hitword=pl_rthit(tp->text, tp->offs, m->xy, ul);
			if(tp->hitword==0)
				if(oldhitword!=0 && oldstate==DOWN)
					tp->hitword=oldhitword;
				else
					tp->hitfirst=0;
			if(tp->hitword!=0 && oldstate!=DOWN)
				tp->hitfirst=tp->hitword;
		}
	}
	else{
		if(p->state==DOWN) hitme=1;
		p->state=PASSIVE;
	}
	if(tp->hitfirst!=oldhitfirst || tp->hitword!=oldhitword){
		plrtseltext(tp->text, tp->hitword, tp->hitfirst);
		pl_drawtextview(p);
		if(tp->hitword==tp->hitfirst)
			pl_passon(tp->hitword, m);
	}
	if(hitme && tp->hit && tp->hitword!=0 && tp->hitword==tp->hitfirst){
		plrtseltext(tp->text, 0, 0);
		pl_drawtextview(p);
		tp->hit(p, tp->buttons, tp->hitword);
		tp->hitword=0;
		tp->hitfirst=0;
	}
	return 0;
}
void pl_scrolltextview(Panel *p, int dir, int buttons, int num, int den){
	int xoffs, yoffs;
	Point ul, size;
	Textview *tp;
	Rectangle r;

	tp=p->data;
	ul=p->r.min;
	size=subpt(p->r.max, p->r.min);
	pl_interior(p->state, &ul, &size);
	if(dir==VERT){
		switch(buttons){
		default:
			SET(yoffs);
			break;
		case 1:		/* left -- top moves to pointer */
			yoffs=(vlong)tp->offs.y-num*size.y/den;
			if(yoffs<0) yoffs=0;
			break;
		case 2:		/* middle -- absolute index of file */
			yoffs=(vlong)tp->thgt*num/den;
			break;
		case 4:		/* right -- line pointed at moves to top */
			yoffs=tp->offs.y+(vlong)num*size.y/den;
			if(yoffs>tp->thgt) yoffs=tp->thgt;
			break;
		}
		if(yoffs!=tp->offs.y){
			r=pl_outline(p->b, p->r, p->state);
			pl_rtredraw(p->b, r, tp->text,
				Pt(tp->offs.x, yoffs), tp->offs, dir);
			p->scr.pos.y=tp->offs.y=yoffs;
			pl_setscrpos(p, tp, r);
		}
	}else{ /* dir==HORIZ */
		switch(buttons){
		default:
			SET(xoffs);
			break;
		case 1:		/* left */
			xoffs=(vlong)tp->offs.x-num*size.x/den;
			if(xoffs<0) xoffs=0;
			break;
		case 2:		/* middle */
			xoffs=(vlong)tp->maxwid*num/den;
			break;
		case 4:		/* right */
			xoffs=tp->offs.x+(vlong)num*size.x/den;
			if(xoffs>tp->maxwid) xoffs=tp->maxwid;
			break;
		}
		if(xoffs!=tp->offs.x){
			r=pl_outline(p->b, p->r, p->state);
			pl_rtredraw(p->b, r, tp->text,
				Pt(xoffs, tp->offs.y), tp->offs, dir);
			p->scr.pos.x=tp->offs.x=xoffs;
			pl_setscrpos(p, tp, r);
		}
	}
}
void pl_typetextview(Panel *g, Rune c){
	USED(g, c);
}
Point pl_getsizetextview(Panel *p, Point children){
	USED(children);
	return pl_boxsize(((Textview *)p->data)->minsize, p->state);
}
void pl_childspacetextview(Panel *g, Point *ul, Point *size){
	USED(g, ul, size);
}
/*
 * Priority depends on what thing inside the panel we're pointing at.
 */
int pl_pritextview(Panel *p, Point xy){
	Point ul, size;
	Textview *tp;
	Rtext *h;
	tp=p->data;
	ul=p->r.min;
	size=subpt(p->r.max, p->r.min);
	pl_interior(p->state, &ul, &size);
	h=pl_rthit(tp->text, tp->offs, xy, ul);
	if(h && h->b==0 && h->p!=0){
		p=pl_ptinpanel(xy, h->p);
		if(p) return p->pri(p, xy);
	}
	return PRI_NORMAL;
}

char* pl_snarftextview(Panel *p){
	return plrtsnarftext(((Textview *)p->data)->text);
}

void plinittextview(Panel *v, int flags, Point minsize, Rtext *t, void (*hit)(Panel *, int, Rtext *)){
	Textview *tp;
	tp=v->data;
	v->flags=flags|LEAF;
	v->state=PASSIVE;
	v->draw=pl_drawtextview;
	v->hit=pl_hittextview;
	v->type=pl_typetextview;
	v->getsize=pl_getsizetextview;
	v->childspace=pl_childspacetextview;
	v->kind="textview";
	v->pri=pl_pritextview;
	tp->hit=hit;
	tp->minsize=minsize;
	tp->text=t;
	tp->offs=ZP;
	tp->hitfirst=0;
	tp->hitword=0;
	v->scroll=pl_scrolltextview;
	v->snarf=pl_snarftextview;
	tp->twid=-1;
	tp->maxwid=0;
	v->scr.pos=Pt(0,0);
	v->scr.size=Pt(0,1);
}
Panel *pltextview(Panel *parent, int flags, Point minsize, Rtext *t, void (*hit)(Panel *, int, Rtext *)){
	Panel *v;
	v=pl_newpanel(parent, sizeof(Textview));
	plinittextview(v, flags, minsize, t, hit);
	return v;
}
int plgetpostextview(Panel *p){
	return ((Textview *)p->data)->offs.y;
}
void plsetpostextview(Panel *p, int yoffs){
	((Textview *)p->data)->offs.y=yoffs;
	pldraw(p, p->b);
}
