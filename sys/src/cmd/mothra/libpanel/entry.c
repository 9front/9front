#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "pldefs.h"
#include <keyboard.h>

typedef struct Entry Entry;
struct Entry{
	char *entry;
	char *entp;
	char *eent;
	void (*hit)(Panel *, char *);
	Point minsize;
};
#define	SLACK	7	/* enough for one extra rune and ◀ and a nul */
void pl_drawentry(Panel *p){
	Rectangle r;
	Entry *ep;
	ep=p->data;
	r=pl_box(p->b, p->r, p->state);
	if(stringwidth(font, ep->entry)<=r.max.x-r.min.x)
		pl_drawicon(p->b, r, PLACEW, 0, ep->entry);
	else
		pl_drawicon(p->b, r, PLACEE, 0, ep->entry);
}
int pl_hitentry(Panel *p, Mouse *m){
	int oldstate;
	oldstate=p->state;
	if(m->buttons&OUT)
		p->state=UP;
	else if(m->buttons&7)
		p->state=DOWN;
	else{	/* mouse inside, but no buttons down */
		if(p->state==DOWN) plgrabkb(p);
		p->state=UP;
	}
	if(p->state!=oldstate) pldraw(p, p->b);
	return 0;
}
void pl_typeentry(Panel *p, Rune c){
	int n;
	Entry *ep;
	ep=p->data;
	switch(c){
	case '\n':
	case '\r':
		*ep->entp='\0';
		if(ep->hit) ep->hit(p, ep->entry);
		return;
	case Knack:	/* ^U: erase line */
		ep->entp=ep->entry;
		*ep->entp='\0';
		break;
	case Kbs:	/* ^H: erase character */
		while(ep->entp!=ep->entry && !pl_rune1st(ep->entp[-1])) *--ep->entp='\0';
		if(ep->entp!=ep->entry) *--ep->entp='\0';
		break;
	case Ketb:	/* ^W: erase word */
		while(ep->entp!=ep->entry && !pl_idchar(ep->entp[-1]))
			--ep->entp;
		while(ep->entp!=ep->entry && pl_idchar(ep->entp[-1]))
			--ep->entp;
		*ep->entp='\0';
		break;
	default:
		if(c < 0x20 || c == Kesc || c == Kdel || (c & 0xFF00) == KF || (c & 0xFF00) == Spec)
			break;
		ep->entp+=runetochar(ep->entp, &c);
		if(ep->entp>ep->eent){
			n=ep->entp-ep->entry;
			ep->entry=realloc(ep->entry, n+100+SLACK);
			if(ep->entry==0){
				fprint(2, "can't realloc in pl_typeentry\n");
				exits("no mem");
			}
			ep->entp=ep->entry+n;
			ep->eent=ep->entp+100;
		}
		break;
	}
	memset(ep->entp, 0, SLACK);
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
	ep->entry = ep->eent = 0;
}
void plinitentry(Panel *v, int flags, int wid, char *str, void (*hit)(Panel *, char *)){
	int elen;
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
	elen=100;
	if(str) elen+=strlen(str);
	if(ep->entry==nil)
		ep->entry=pl_emalloc(elen+SLACK);
	ep->eent=ep->entry+elen;
	strecpy(ep->entry, ep->eent, str ? str : "");
	ep->entp=ep->entry+strlen(ep->entry);
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
	*ep->entp='\0';
	return ep->entry;
}
