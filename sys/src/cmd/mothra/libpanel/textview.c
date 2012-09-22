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
#include "rtext.h"

typedef struct Textview Textview;
struct Textview{
	void (*hit)(Panel *, int, Rtext *); /* call back to user on hit */
	Rtext *text;			/* text */
	int yoffs;			/* offset of top of screen */
	Rtext *hitword;			/* text to hilite */
	Rtext *hitfirst;		/* first word in range select */
	Image *hitsave;			/* for restoring hilit text */
	int twid;			/* text width */
	int thgt;			/* text height */
	Point minsize;			/* smallest acceptible window size */
	int buttons;
	char *snarf;
	char *esnarf;
};

/*
 * Highlight a range of words from s to e.
 */
void pl_hilitewords(Panel *p, Rtext *s, Rtext *e, int on)
{
	Point ul, size;
	Rectangle r;
	Textview *tp;
	Rtext *t;

	if(s==0 || e==0)
		return;
	for(t=s; t!=0 && t!=e; t=t->next)
		;
	if(t==e){
		r=s->r;
		for(t=s; t!=e; t=t->next)
			combinerect(&r, t->r);
	}else{
		r=e->r;
		for(t=e; t!=s; t=t->next)
			combinerect(&r, t->r);
	}
	combinerect(&r, t->r);

	tp=p->data;
	ul=p->r.min;
	size=subpt(p->r.max, p->r.min);
	pl_interior(UP, &ul, &size);
	ul.y-=tp->yoffs;
	r=rectaddpt(r, ul);
	if(rectclip(&r, p->r)==0)
		return;

	if(on){
		if(tp->hitsave) freeimage(tp->hitsave);
		tp->hitsave = allocimage(display, r, screen->chan, 0, DNofill);
		if(tp->hitsave) draw(tp->hitsave, r, p->b, 0, r.min);
		if(t==e){
			for(t=s; t!=e; t=t->next){
				if(t->p!=0) continue;
				r=rectaddpt(t->r, ul);
				if(rectclip(&r, p->r))
					pl_highlight(p->b, r);
			}
		}else{
			for(t=e; t!=s; t=t->next){
				if(t->p!=0) continue;
				r=rectaddpt(t->r, ul);
				if(rectclip(&r, p->r))
					pl_highlight(p->b, r);
			}
		}
		if(t->p==0){
			r=rectaddpt(t->r, ul);
			if(rectclip(&r, p->r))
				pl_highlight(p->b, r);
		}
	} else {
		if(tp->hitsave){
			draw(p->b, r, tp->hitsave, 0, r.min);
			freeimage(tp->hitsave);
			tp->hitsave = 0;
		}
	}
}

void pl_stuffbitmap(Panel *p, Image *b){
	p->b=b;
	for(p=p->child;p;p=p->next)
		pl_stuffbitmap(p, b);
}
/*
 * If we draw the text in a backup bitmap and copy it onto the screen,
 * the bitmap pointers in all the subpanels point to the wrong bitmap.
 * This code fixes them.
 */
void pl_drawnon(Rtext *rp, Image *b){
	for(;rp!=0;rp=rp->next)
		if(rp->b==0 && rp->p!=0)
			pl_stuffbitmap(rp->p, b);
}
/*
 * Mark the hilite and update the scroll bar
 */
void pl_fixtextview(Panel *p, Textview *tp, Rectangle r){
	Panel *sb;
	int lo, hi;
	pl_hilitewords(p, tp->hitword, tp->hitfirst, 1);
	lo=tp->yoffs;
	hi=lo+r.max.y-r.min.y;	/* wrong? */
	sb=p->yscroller;
	if(sb && sb->setscrollbar) sb->setscrollbar(sb, lo, hi, tp->thgt);
}
void pl_drawtextview(Panel *p){
	int twid;
	Rectangle r;
	Textview *tp;
	Image *b;
	tp=p->data;
	b=allocimage(display, p->r, screen->chan, 0, DNofill);
	if(b==0) b=p->b;
	r=pl_outline(b, p->r, p->state);
	twid=r.max.x-r.min.x;
	if(twid!=tp->twid){
		tp->twid=twid;
		tp->thgt=pl_rtfmt(tp->text, tp->twid);
		p->scr.size.y=tp->thgt;
	}
	p->scr.pos.y=tp->yoffs;
	pl_rtdraw(b, r, tp->text, tp->yoffs);
	if(b!=p->b){
		draw(p->b, p->r, b, 0, b->r.min);
		freeimage(b);
		pl_drawnon(tp->text, p->b);
	}
	pl_fixtextview(p, tp, r);
}
/*
 * If t is a panel word, pass the mouse event on to it
 */
void pl_passon(Rtext *t, Mouse *m){
	if(t && t->b==0 && t->p!=0) plmouse(t->p, m);
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
		p->state=UP;
	else if(m->buttons&7){
		tp->buttons=m->buttons;
		p->state=DOWN;

		ul=p->r.min;
		size=subpt(p->r.max, p->r.min);
		pl_interior(p->state, &ul, &size);
		tp->hitword=pl_rthit(tp->text, tp->yoffs, m->xy, ul);
		if(tp->hitword==0)
			if(oldhitword!=0 && oldstate==DOWN)
				tp->hitword=oldhitword;
			else
				tp->hitfirst=0;
		if(tp->hitword!=0 && oldstate!=DOWN)
			tp->hitfirst=tp->hitword;
	}
	else{
		if(p->state==DOWN) hitme=1;
		p->state=UP;
	}
	if(tp->hitfirst!=oldhitfirst || tp->hitword!=oldhitword){
		pl_hilitewords(p, oldhitword, oldhitfirst, 0);
		pl_hilitewords(p, tp->hitword, tp->hitfirst, 1);
		if(tp->hitword==tp->hitfirst)
			pl_passon(tp->hitword, m);
	}
	if(hitme && tp->hit && tp->hitword!=0 && tp->hitword==tp->hitfirst){
		pl_hilitewords(p, tp->hitword, tp->hitfirst, 0);
		tp->hit(p, tp->buttons, tp->hitword);
		tp->hitword=0;
		tp->hitfirst=0;
	}
	return 0;
}
void pl_scrolltextview(Panel *p, int dir, int buttons, int num, int den){
	int yoffs;
	Point ul, size;
	Textview *tp;
	Rectangle r;
	if(dir!=VERT) return;

	tp=p->data;
	ul=p->r.min;
	size=subpt(p->r.max, p->r.min);
	pl_interior(p->state, &ul, &size);
	switch(buttons){
	default:
		SET(yoffs);
		break;
	case 1:		/* left -- top moves to pointer */
		yoffs=(vlong)tp->yoffs-num*size.y/den;
		if(yoffs<0) yoffs=0;
		break;
	case 2:		/* middle -- absolute index of file */
		yoffs=(vlong)tp->thgt*num/den;
		break;
	case 4:		/* right -- line pointed at moves to top */
		yoffs=tp->yoffs+(vlong)num*size.y/den;
		if(yoffs>tp->thgt) yoffs=tp->thgt;
		break;
	}
	if(yoffs!=tp->yoffs){
		pl_hilitewords(p, tp->hitword, tp->hitfirst, 0);
		r=pl_outline(p->b, p->r, p->state);
		pl_rtredraw(p->b, r, tp->text, yoffs, tp->yoffs);
		p->scr.pos.y=tp->yoffs=yoffs;
		pl_fixtextview(p, tp, r);
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
	h=pl_rthit(tp->text, tp->yoffs, xy, ul);
	if(h && h->b==0 && h->p!=0){
		p=pl_ptinpanel(xy, h->p);
		if(p) return p->pri(p, xy);
	}
	return PRI_NORMAL;
}
void plinittextview(Panel *v, int flags, Point minsize, Rtext *t, void (*hit)(Panel *, int, Rtext *)){
	Textview *tp;
	tp=v->data;
	v->flags=flags|LEAF;
	v->state=UP;
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
	tp->yoffs=0;
	tp->hitfirst=0;
	tp->hitword=0;
	tp->esnarf=tp->snarf=0;
	v->scroll=pl_scrolltextview;
	tp->twid=-1;
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
	return ((Textview *)p->data)->yoffs;
}
void plsetpostextview(Panel *p, int yoffs){
	((Textview *)p->data)->yoffs=yoffs;
	pldraw(p, p->b);
}

static void
snarfword(Textview *tp, Rtext *w){
	char *b;
	int n;
	if(w->b!=0 || w->p!=0 || w->text==0)
		return;
	n = strlen(w->text)+4;
	if((b = realloc(tp->snarf, (tp->esnarf+n) - tp->snarf)) == nil)
		return;
	tp->esnarf = (tp->esnarf - tp->snarf) + b;
	tp->snarf = b;
	if(w->space == 0)
		tp->esnarf += sprint(tp->esnarf, "%s", w->text);
	else if(w->space > 0)
		tp->esnarf += sprint(tp->esnarf, " %s", w->text);
	else if(PL_OP(w->space) == PL_TAB)
		tp->esnarf += sprint(tp->esnarf, "\t%s", w->text);
	if(w->nextline == w->next)
		tp->esnarf += sprint(tp->esnarf, "\n");
}

char *plsnarftext(Panel *p){
	Rtext *t, *s, *e;
	Textview *tp;
	tp=p->data;
	free(tp->snarf);
	tp->snarf=tp->esnarf=0;
	s = tp->hitfirst;
	e = tp->hitword;
	if(s==0 || e==0)
		return nil;
	for(t=s; t!=0 && t!=e; t=t->next)
		;
	if(t==e){
		for(t=s; t!=e; t=t->next)
			snarfword(tp, t);
	}else{
		for(t=e; t!=s; t=t->next)
			snarfword(tp, t);
	}
	snarfword(tp, t);
	return tp->snarf;
}
