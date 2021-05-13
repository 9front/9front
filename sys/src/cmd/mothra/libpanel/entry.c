#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "pldefs.h"
#include <keyboard.h>

typedef struct Entry Entry;
struct Entry{
	Rectangle lastr;
	Rune *entry;
	char *sentry;
	int sz, n;
	int a, b;
	Point text;
	void (*hit)(Panel *, char *);
	Point minsize;
};
#define	SLACK	7	/* enough for one extra rune and ◀ and a nul */
void pl_cutentry(Panel *p){
	Entry *ep;

	ep=p->data;
	memmove(ep->entry+ep->a, ep->entry+ep->b, (ep->n-ep->b)*sizeof(Rune));
	ep->n -= ep->b-ep->a;
	ep->entry[ep->n]=0;
	ep->b=ep->a;
}
char *pl_snarfentry(Panel *p){
	Entry *ep;
	int n;

	if(p->flags&USERFL)	/* no snarfing from password entry */
		return nil;
	ep=p->data;
	n=ep->b-ep->a;
	if(n<1) return nil;
	return smprint("%.*S", n, ep->entry+ep->a);
}
void pl_pasteentry(Panel *p, char *s){
	Entry *ep;
	int m;

	ep=p->data;
	m=utflen(s);
	ep->sz=ep->n+m+100+SLACK;
	ep->entry=pl_erealloc(ep->entry,ep->sz*sizeof(Rune));
	memmove(ep->entry+ep->a+m, ep->entry+ep->b, (ep->n-ep->b)*sizeof(Rune));
	ep->n+=m-(ep->b-ep->a);
	while(m-- > 0)
		s += chartorune(&ep->entry[ep->a++], s);
	ep->b=ep->a;
	ep->entry[ep->n]=0;
	pldraw(p, p->b);
}
static void drawentry(Panel *p, Rectangle r, Rune *s){
	Rectangle save;
	Point tick;
	Entry *ep;
	Image *b;
	int d;

	ep = p->data;
	b = p->b;

	ep->text = r.min;
	ep->lastr = r;
	tick = ep->text;
	tick.x += runestringnwidth(font, s, ep->a);
	if(plkbfocus == p)
		r.max.x -= TICKW;
	ep->text.y = r.min.y;
	if(!ptinrect(tick, r)){
		d = 0;
		if(tick.x < r.min.x)
			d = r.min.x - tick.x;
		else if(tick.x > r.max.x)
			d = r.max.x - tick.x;
		tick.x += d;
		ep->text.x += d;
	}
	if(plkbfocus == p)
		r.max.x += TICKW;

	save = b->clipr;
	if(!rectclip(&r, save))
		return;
	replclipr(b, b->repl, r);
	runestring(b, ep->text, pl_black, ZP, font, s);
	if(plkbfocus == p){
		r.min = tick;
		if(ep->a != ep->b){
			r.max.x = ep->text.x+runestringnwidth(font, s, ep->b);
			if(r.max.x < r.min.x){
				d = r.min.x;
				r.min.x = r.max.x;
				r.max.x = d;
			}
			pl_highlight(b, r);
		}else
			pl_drawtick(b, r);
	}
	replclipr(b, b->repl, save);
}
void pl_drawentry(Panel *p){
	Rectangle r;
	Entry *ep;
	Rune *s;

	ep=p->data;
	r=pl_box(p->b, p->r, p->state|BORDER);
	s=ep->entry;
	if(p->flags & USERFL){
		Rune *p;
		s=runestrdup(s);
		for(p=s; *p; p++)
			*p='*';
	}
	drawentry(p, r, s);
	if(s != ep->entry)
		free(s);
}
int pl_hitentry(Panel *p, Mouse *m){
	Entry *ep;
	int i, n, selecting;
	if((m->buttons&7)==1){
		if(plkbfocus != p)
			p->state=DOWN;
		plgrabkb(p);
		ep = p->data;
		for(i = 1; i <= ep->n; i++)
			if(runestringnwidth(font, ep->entry, i) > m->xy.x-ep->text.x)
				break;
		n = i-1;
		ep->a = ep->b = n;
		pldraw(p, p->b);
		selecting = 1;
		while(m->buttons&1){
			int old;
			old=m->buttons;
			if(display->bufp > display->buf)
				flushimage(display, 1);
			*m=emouse();
			p->state=UP;
			if((old&7)==1){
				if((m->buttons&7)==3){
					plsnarf(p);
					pl_cutentry(p);
					pldraw(p, p->b);
					ep->b = n = ep->a;
				}
				if(selecting && (m->buttons&7)==1){
					p->state=UP;
					for(i = 0; i < ep->n; i++)
						if(runestringnwidth(font, ep->entry, i)+TICKW > m->xy.x-ep->text.x)
							break;
					/*
					 * tick is moved towards the mouse pointer dragging the selection
					 * after drawing it has to be set so that (a <= b), since
					 * the rest of the logic assumes that's always the case
					 */
					ep->a = i;
					ep->b = n;
					pldraw(p, p->b);
					if(ep->a > ep->b){
						ep->a = n;
						ep->b = i;
					}
				}else
					selecting = 0;
				if((m->buttons&7)==5)
					plpaste(p);
			}
		}
		p->state=UP;
		pldraw(p, p->b);
	}
	return 0;
}
void pl_typeentry(Panel *p, Rune c){
	Entry *ep;
	ep=p->data;
	switch(c){
	case '\n':
	case '\r':
		if(ep->hit) ep->hit(p, plentryval(p));
		return;
	case Kleft:
		if(ep->a > 0)
			ep->a--;
		ep->b=ep->a;
		break;
	case Kright:
		if(ep->a<ep->n)
			ep->a++;
		ep->b = ep->a;
		break;
	case Ksoh:
		ep->a=ep->b=0;
		break;
	case Kenq:
		ep->a=ep->b=ep->n;
		break;
	case Kesc:
		ep->a=0;
		ep->b=ep->n;
		plsnarf(p);
		/* no break */
	case Kdel:	/* clear */
		ep->a = ep->b = ep->n = 0;
		*ep->entry = 0;
		break;
	case Knack:	/* ^U: erase line */
		ep->a = 0;
		pl_cutentry(p);
		break;
	case Kbs:	/* ^H: erase character */
		if(ep->a > 0 && ep->a == ep->b)
			ep->a--;
		/* wet floor */
		if(0){
	case Ketb:	/* ^W: erase word */
			while(ep->a>0 && !pl_idchar(ep->entry[ep->a-1]))
				--ep->a;
			while(ep->a>0 && pl_idchar(ep->entry[ep->a-1]))
				--ep->a;
		}
		pl_cutentry(p);
		break;
	default:
		if(c < 0x20 || (c & 0xFF00) == KF || (c & 0xFF00) == Spec)
			break;
		memmove(ep->entry+ep->a+1, ep->entry+ep->b, (ep->n-ep->b)*sizeof(Rune));
		ep->n -= ep->b - ep->a - 1;
		ep->entry[ep->a++] = c;
		ep->b = ep->a;
		if(ep->n>ep->sz){
			ep->sz = ep->n+100;
			ep->entry=pl_erealloc(ep->entry, (ep->sz+SLACK)*sizeof(Rune));
		}
		ep->entry[ep->n]=0;
		break;
	}
	pldraw(p, p->b);
}
Point pl_getsizeentry(Panel *p, Point children){
	USED(children);
	return pl_boxsize(((Entry *)p->data)->minsize, p->state);
}
void pl_childspaceentry(Panel *p, Point *ul, Point *size){
	USED(p, ul, size);
}
void pl_freeentry(Panel *p){
	Entry *ep;
	ep = p->data;
	free(ep->entry);
	free(ep->sentry);
	ep->entry = nil;
	ep->sentry = nil;
}
void plinitentry(Panel *v, int flags, int wid, char *str, void (*hit)(Panel *, char *)){
	Entry *ep;
	ep=v->data;
	v->flags=flags|LEAF;
	v->state=UP;
	v->draw=pl_drawentry;
	v->hit=pl_hitentry;
	v->type=pl_typeentry;
	v->getsize=pl_getsizeentry;
	v->childspace=pl_childspaceentry;
	ep->minsize=Pt(wid, font->height);
	v->free=pl_freeentry;
	v->snarf=pl_snarfentry;
	v->paste=pl_pasteentry;
	ep->a = ep->b = 0;
	ep->n = str ? utflen(str) : 0;
	ep->sz = ep->n + 100;
	ep->entry=pl_erealloc(ep->entry, (ep->sz+SLACK)*sizeof(Rune));
	runesnprint(ep->entry, ep->sz, "%s", str ? str : "");
	ep->hit=hit;
	v->kind="entry";
}
Panel *plentry(Panel *parent, int flags, int wid, char *str, void (*hit)(Panel *, char *)){
	Panel *v;
	v=pl_newpanel(parent, sizeof(Entry));
	plinitentry(v, flags, wid, str, hit);
	return v;
}
char *plentryval(Panel *p){
	Entry *ep;
	ep=p->data;
	free(ep->sentry);
	ep->sentry = smprint("%S", ep->entry);
	return ep->sentry;
}
