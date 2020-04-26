/*
 * Text windows
 *	void twhilite(Textwin *t, int sel0, int sel1, int on)
 *		hilite (on=1) or unhilite (on=0) a range of characters
 *	void twselect(Textwin *t, Mouse *m)
 *		set t->sel0, t->sel1 from mouse input.
 *		Also hilites selection.
 *		Caller should first unhilite previous selection.
 *	void twreplace(Textwin *t, int r0, int r1, Rune *ins, int nins)
 *		Replace the given range of characters with the given insertion.
 *		Caller should unhilite selection while this is called.
 *	void twscroll(Textwin *t, int top)
 *		Character with index top moves to the top line of the screen.
 *	int twpt2rune(Textwin *t, Point p)
 *		which character is displayed at point p?
 *	void twreshape(Textwin *t, Rectangle r)
 *		save r and redraw the text
 *	Textwin *twnew(Bitmap *b, Font *f, Rune *text, int ntext)
 *		create a new text window
 *	void twfree(Textwin *t)
 *		get rid of a surplus Textwin
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "pldefs.h"

#define SLACK 100

/*
 * Is text at point a before or after that at point b?
 */
int tw_before(Textwin *t, Point a, Point b){
	return a.y<b.y || a.y<b.y+t->hgt && a.x<b.x;
}
/*
 * Return the character index indicated by point p, or -1
 * if its off-screen.  The screen must be up-to-date.
 *
 * Linear search should be binary search.
 */
int twpt2rune(Textwin *t, Point p){
	Point *el, *lp;
	el=t->loc+(t->bot-t->top);
	for(lp=t->loc;lp!=el;lp++)
		if(tw_before(t, p, *lp)){
			if(lp==t->loc) return t->top;
			return lp-t->loc+t->top-1;
		}
	return t->bot;
}
/*
 * Return ul corner of the character with the given index
 */
Point tw_rune2pt(Textwin *t, int i){
	if(i<t->top) return t->r.min;
	if(i>t->bot) return t->r.max;
	return t->loc[i-t->top];
}
/*
 * Store p at t->loc[l], extending t->loc if necessary
 */
void tw_storeloc(Textwin *t, int l, Point p){
	int nloc;
	if(l>=t->eloc-t->loc){
		nloc=l+SLACK;
		t->loc=pl_erealloc(t->loc, nloc*sizeof(Point));
		t->eloc=t->loc+nloc;
	}
	t->loc[l]=p;
}
/*
 * Set the locations at which the given runes should appear.
 * Returns the index of the first rune not set, which might not
 * be last because we reached the bottom of the window.
 *
 * N.B. this zaps the loc of r[last], so that value should be saved first,
 * if it's important.
 */
int tw_setloc(Textwin *t, int first, int last, Point ul){
	Rune *r, *er;
	int x, dt, lp;
	char buf[UTFmax+1];
	er=t->text+last;
	for(r=t->text+first,lp=first-t->top;r!=er && ul.y+t->hgt<=t->r.max.y;r++,lp++){
		tw_storeloc(t, lp, ul);
		switch(*r){
		case '\n':
			ul.x=t->r.min.x;
			ul.y+=t->hgt;
			break;
		case '\t':
			x=ul.x-t->r.min.x+t->mintab+t->tabstop;
			x-=x%t->tabstop;
			ul.x=x+t->r.min.x;
			if(ul.x>t->r.max.x){
				ul.x=t->r.min.x;
				ul.y+=t->hgt;
				tw_storeloc(t, lp, ul);
				if(ul.y+t->hgt>t->r.max.y) return r-t->text;
				ul.x+=+t->tabstop;
			}
			break;
		default:
			buf[runetochar(buf, r)]='\0';
			dt=stringwidth(t->font, buf);
			ul.x+=dt;
			if(ul.x>t->r.max.x){
				ul.x=t->r.min.x;
				ul.y+=t->hgt;
				tw_storeloc(t, lp, ul);
				if(ul.y+t->hgt>t->r.max.y) return r-t->text;
				ul.x+=dt;
			}
			break;
		}
	}
	tw_storeloc(t, lp, ul);
	return r-t->text;
}
/*
 * Draw the given runes at their locations.
 * Bug -- saving up multiple characters would
 * reduce the number of calls to string,
 * and probably make this a lot faster.
 */
void tw_draw(Textwin *t, int first, int last){
	Rune *r, *er;
	Point *lp, ul, ur;
	char buf[UTFmax+1];
	if(first<t->top) first=t->top;
	if(last>t->bot) last=t->bot;
	if(last<=first) return;
	er=t->text+last;
	for(r=t->text+first,lp=t->loc+(first-t->top);r!=er;r++,lp++){
		if(lp->y+t->hgt>t->r.max.y){
			fprint(2, "chr %C, index %zd of %d, loc %d %d, off bottom\n",
				*r, lp-t->loc, t->bot-t->top, lp->x, lp->y);
			return;
		}
		switch(*r){
		case '\n':
			ur=*lp;
			break;
		case '\t':
			ur=*lp;
			if(lp[1].y!=lp[0].y)
				ul=Pt(t->r.min.x, lp[1].y);
			else
				ul=*lp;
			pl_clr(t->b, Rpt(ul, Pt(lp[1].x, ul.y+t->hgt)));
			break;
		default:
			buf[runetochar(buf, r)]='\0';
	/***/		pl_clr(t->b, Rpt(*lp, addpt(*lp, stringsize(t->font, buf))));
			ur=string(t->b, *lp, display->black, ZP, t->font, buf);
			break;
		}
		if(lp[1].y!=lp[0].y)
	/***/		pl_clr(t->b, Rpt(ur, Pt(t->r.max.x, ur.y+t->hgt)));
	}
}
/*
 * Hilight the characters with tops between ul and ur
 */
void tw_hilitep(Textwin *t, Point ul, Point ur){
	Point swap;
	int y;
	if(tw_before(t, ur, ul)){ swap=ul; ul=ur; ur=swap;}
	y=ul.y+t->hgt;
	if(y>t->r.max.y) y=t->r.max.y;
	if(ul.y==ur.y)
		pl_highlight(t->b, Rpt(ul, Pt(ur.x, y)));
	else{
		pl_highlight(t->b, Rpt(ul, Pt(t->r.max.x, y)));
		ul=Pt(t->r.min.x, y);
		pl_highlight(t->b, Rpt(ul, Pt(t->r.max.x, ur.y)));
		ul=Pt(t->r.min.x, ur.y);
		y=ur.y+t->hgt;
		if(y>t->r.max.y) y=t->r.max.y;
		pl_highlight(t->b, Rpt(ul, Pt(ur.x, y)));
	}
}
/*
 * Hilite/unhilite the given range of characters
 */
void twhilite(Textwin *t, int sel0, int sel1, int on){
	Point ul, ur;
	int swap, y;
	if(sel1<sel0){ swap=sel0; sel0=sel1; sel1=swap; }
	if(sel1<t->top || t->bot<sel0) return;
	if(sel0<t->top) sel0=t->top;
	if(sel1>t->bot) sel1=t->bot;
	if(!on){
		if(sel1==sel0){
			ul=t->loc[sel0-t->top];
			y=ul.y+t->hgt;
			if(y>t->r.max.y) y=t->r.max.y;
			pl_clr(t->b, Rpt(ul, Pt(ul.x+1, y)));
		}else
			tw_draw(t, sel0, sel1);
		return;
	}
	ul=t->loc[sel0-t->top];
	if(sel1==sel0)
		ur=addpt(ul, Pt(1, 0));
	else
		ur=t->loc[sel1-t->top];
	tw_hilitep(t, ul, ur);
}
/*
 * Set t->sel[01] from mouse input.
 * Also hilites the selection.
 * Caller should unhilite the previous
 * selection before calling this.
 */
void twselect(Textwin *t, Mouse *m){
	int sel0, sel1, newsel;
	Point p0, p1, newp;
	sel0=sel1=twpt2rune(t, m->xy);
	p0=tw_rune2pt(t, sel0);
	p1=addpt(p0, Pt(1, 0));
	twhilite(t, sel0, sel1, 1);
	for(;;){
		if(display->bufp > display->buf)
			flushimage(display, 1);
		*m=emouse();
		if((m->buttons&7)!=1) break;
		newsel=twpt2rune(t, m->xy);
		newp=tw_rune2pt(t, newsel);
		if(eqpt(newp, p0)) newp=addpt(newp, Pt(1, 0));
		if(!eqpt(newp, p1)){
			if((sel0<=sel1 && sel1<newsel) || (newsel<sel1 && sel1<sel0))
				tw_hilitep(t, p1, newp);
			else if((sel0<=newsel && newsel<sel1) || (sel1<newsel && newsel<=sel0)){
				twhilite(t, sel1, newsel, 0);
				if(newsel==sel0)
					tw_hilitep(t, p0, newp);
			}else if((newsel<sel0 && sel0<=sel1) || (sel1<sel0 && sel0<=newsel)){
				twhilite(t, sel0, sel1, 0);
				tw_hilitep(t, p0, newp);
			}
			sel1=newsel;
			p1=newp;
		}
	}
	if(sel0<=sel1){
		t->sel0=sel0;
		t->sel1=sel1;
	}
	else{
		t->sel0=sel1;
		t->sel1=sel0;
	}
}
/*
 * Clear the area following the last displayed character
 */
void tw_clrend(Textwin *t){
	Point ul;
	int y;
	ul=t->loc[t->bot-t->top];
	y=ul.y+t->hgt;
	if(y>t->r.max.y) y=t->r.max.y;
	pl_clr(t->b, Rpt(ul, Pt(t->r.max.x, y)));
	ul=Pt(t->r.min.x, y);
	pl_clr(t->b, Rpt(ul, t->r.max));
}
/*
 * Move part of a line of text, truncating the source or padding
 * the destination on the right if necessary.
 */
void tw_moverect(Textwin *t, Point uld, Point urd, Point uls, Point urs){
	int sw, dw, d;
	if(urs.y!=uls.y) urs=Pt(t->r.max.x, uls.y);
	if(urd.y!=uld.y) urd=Pt(t->r.max.x, uld.y);
	sw=uls.x-urs.x;
	dw=uld.x-urd.x;
	if(dw>sw){
		d=dw-sw;
		pl_clr(t->b, Rect(urd.x-d, urd.y, urd.x, urd.y+t->hgt));
		dw=sw;
	}
	pl_cpy(t->b, uld, Rpt(uls, Pt(uls.x+dw, uls.y+t->hgt)));
}
/*
 * Move a block of characters up or to the left:
 *	Identify contiguous runs of characters whose width doesn't change, and
 *	move them in one bitblt per run.
 *	If we get to a point where source and destination are x-aligned,
 *	they will remain x-aligned for the rest of the block.
 *	Then, if they are y-aligned, they're already in the right place.
 *	Otherwise, we can move them in three bitblts; one if all the
 *	remaining characters are on one line.
 */
void tw_moveup(Textwin *t, Point *dp, Point *sp, Point *esp){
	Point uld, uls;			/* upper left of destination/source */
	int y;
	while(sp!=esp && sp->x!=dp->x){
		uld=*dp;
		uls=*sp;
		while(sp!=esp && sp->y==uls.y && dp->y==uld.y && sp->x-uls.x==dp->x-uld.x){
			sp++;
			dp++;
		}
		tw_moverect(t, uld, *dp, uls, *sp);
	}
	if(sp==esp || esp->y==dp->y) return;
	if(esp->y==sp->y){	/* one line only */
		pl_cpy(t->b, *dp, Rpt(*sp, Pt(esp->x, sp->y+t->hgt)));
		return;
	}
	y=sp->y+t->hgt;
	pl_cpy(t->b, *dp, Rpt(*sp, Pt(t->r.max.x, y)));
	pl_cpy(t->b, Pt(t->r.min.x, dp->y+t->hgt),
		Rect(t->r.min.x, y, t->r.max.x, esp->y));
	y=dp->y+esp->y-sp->y;
	pl_cpy(t->b, Pt(t->r.min.x, y),
		Rect(t->r.min.x, esp->y, esp->x, esp->y+t->hgt));
}
/*
 * Same as above, but moving down and in reverse order, so as not to overwrite stuff
 * not moved yet.
 */
void tw_movedn(Textwin *t, Point *dp, Point *bsp, Point *esp){
	Point *sp, urs, urd;
	int dy;
	dp+=esp-bsp;
	sp=esp;
	dy=dp->y-sp->y;
	while(sp!=bsp && dp[-1].x==sp[-1].x){
		--dp;
		--sp;
	}
	if(dy!=0){
		if(sp->y==esp->y)
			pl_cpy(t->b, *dp, Rect(sp->x, sp->y, esp->x, esp->y+t->hgt));
		else{
			pl_cpy(t->b, Pt(t->r.min.x, sp->x+dy),
				Rect(t->r.min.x, sp->y, esp->x, esp->y+t->hgt));
			pl_cpy(t->b, Pt(t->r.min.x, dp->y+t->hgt),
				Rect(t->r.min.x, sp->y+t->hgt, t->r.max.x, esp->y));
			pl_cpy(t->b, *dp,
				Rect(sp->x, sp->y, t->r.max.x, sp->y+t->hgt));
		}
	}
	while(sp!=bsp){
		urd=*dp;
		urs=*sp;
		while(sp!=bsp && sp[-1].y==sp[0].y && dp[-1].y==dp[0].y
		   && sp[-1].x-sp[0].x==dp[-1].x-dp[0].x){
			--sp;
			--dp;
		}
		tw_moverect(t, *dp, urd, *sp, urs);
	}
}
/*
 * Move the given range of characters, already drawn on
 * the given textwin, to the given location.
 * Start and end must both index characters that are initially on-screen.
 */
void tw_relocate(Textwin *t, int first, int last, Point dst){
	Point *srcloc;
	int nbyte;
	if(first<t->top || last<first || t->bot<last) return;
	nbyte=(last-first+1)*sizeof(Point);
	srcloc=pl_emalloc(nbyte);
	memmove(srcloc, &t->loc[first-t->top], nbyte);
	tw_setloc(t, first, last, dst);
	if(tw_before(t, dst, srcloc[0]))
		tw_moveup(t, t->loc+first-t->top, srcloc, srcloc+(last-first));
	else
		tw_movedn(t, t->loc+first-t->top, srcloc, srcloc+(last-first));
}
/*
 * Replace the runes with indices from r0 to r1-1 with the text
 * pointed to by text, and with length ntext.
 *	Open up a hole in t->text, t->loc.
 *	Insert new text, calculate their locs (save the extra loc that's overwritten first)
 *	(swap saved & overwritten locs)
 *	move tail.
 *	calc locs and draw new text after tail, if necessary.
 *	draw new text, if necessary
 */
void twreplace(Textwin *t, int r0, int r1, Rune *ins, int nins){
	int olen, nlen, tlen, dtop;
	olen=t->etext-t->text;
	nlen=olen+nins-(r1-r0);
	tlen=t->eslack-t->text;
	if(nlen>tlen){
		tlen=nlen+SLACK;
		t->text=pl_erealloc(t->text, tlen*sizeof(Rune));
		t->eslack=t->text+tlen;
	}
	if(olen!=nlen)
		memmove(t->text+r0+nins, t->text+r1, (olen-r1)*sizeof(Rune));
	if(nins!=0)	/* ins can be 0 if nins==0 */
		memmove(t->text+r0, ins, nins*sizeof(Rune));
	t->etext=t->text+nlen;
	if(r0>t->bot)		/* insertion is completely below visible text */
		return;
	if(r1<t->top){		/* insertion is completely above visible text */
		dtop=nlen-olen;
		t->top+=dtop;
		t->bot+=dtop;
		return;
	}
	if(1 || t->bot<=r0+nins){	/* no useful text on screen below r0 */
		if(r0<=t->top)	/* no useful text above, either */
			t->top=r0;
		t->bot=tw_setloc(t, r0, nlen, t->loc[r0-t->top]);
		tw_draw(t, r0, t->bot);
		tw_clrend(t);
		return;
	}
	/*
	 * code for case where there is useful text below is missing (see `1 ||' above)
	 */
}
/*
 * This works but is stupid.
 */
void twscroll(Textwin *t, int top){
	while(top!=0 && t->text[top-1]!='\n') --top;
	t->top=top;
	t->bot=tw_setloc(t, top, t->etext-t->text, t->r.min);
	tw_draw(t, t->top, t->bot);
	tw_clrend(t);
}
void twreshape(Textwin *t, Rectangle r){
	t->r=r;
	t->bot=tw_setloc(t, t->top, t->etext-t->text, t->r.min);
	tw_draw(t, t->top, t->bot);
	tw_clrend(t);
}
Textwin *twnew(Image *b, Font *f, Rune *text, int ntext){
	Textwin *t;
	t=pl_emalloc(sizeof(Textwin));
	t->text=pl_emalloc((ntext+SLACK)*sizeof(Rune));
	t->loc=pl_emalloc(SLACK*sizeof(Point));
	t->eloc=t->loc+SLACK;
	t->etext=t->text+ntext;
	t->eslack=t->etext+SLACK;
	if(ntext) memmove(t->text, text, ntext*sizeof(Rune));
	t->top=0;
	t->bot=0;
	t->sel0=0;
	t->sel1=0;
	t->b=b;
	t->font=f;
	t->hgt=f->height;
	t->mintab=stringwidth(f, "0");
	t->tabstop=8*t->mintab;
	return t;
}
void twfree(Textwin *t){
	free(t->loc);
	free(t->text);
	free(t);
}
/*
 * Correct the character locations in a textwin after the panel is moved.
 * This horrid hack would not be necessary if loc values were relative
 * to the panel, rather than absolute.
 */
void twmove(Textwin *t, Point d){
	Point *lp;
	t->r = rectaddpt(t->r, d);
	for(lp=t->loc; lp<t->eloc; lp++)
		*lp = addpt(*lp, d);
}
