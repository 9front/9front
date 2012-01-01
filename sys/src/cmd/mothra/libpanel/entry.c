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
void pl_snarfentry(Panel *p, int cut){
	Entry *ep;
	int fd, n;
	char *s;

	ep=p->data;
	if((fd=open("/dev/snarf", cut ? OWRITE|OTRUNC : OREAD))<0)
		return;
	if(cut){
		if((n=ep->entp-ep->entry)>0)
			write(fd, ep->entry, n);
		ep->entp=ep->entry;
	}else{
		n = 1024;
		if((s=malloc(n+SLACK))==0){
			close(fd);
			return;
		}
		if((n=readn(fd, s, n))<0)
			n=0;
		free(ep->entry);
		s=realloc(s, n+SLACK);
		ep->entry=s;
		ep->eent=s+n;
		ep->entp=s+n;
	}
	close(fd);
	*ep->entp='\0';
	pldraw(p, p->b);
}
void pl_drawentry(Panel *p){
	Rectangle r;
	Entry *ep;
	char *s;

	ep=p->data;
	r=pl_box(p->b, p->r, p->state);
	s=ep->entry;
	if(p->flags & USERFL){
		char *p;
		s=strdup(s);
		for(p=s; *p; p++)
			*p='*';
	}
	if(stringwidth(font, s)<=r.max.x-r.min.x)
		pl_drawicon(p->b, r, PLACEW, 0, s);
	else
		pl_drawicon(p->b, r, PLACEE, 0, s);
	if(s != ep->entry)
		free(s);
}
int pl_hitentry(Panel *p, Mouse *m){
	if((m->buttons&OUT)==0 && (m->buttons&7)){
		plgrabkb(p);

		p->state=DOWN;
		pldraw(p, p->b);
		while(m->buttons&7){
			int old;
			old=m->buttons;
			*m=emouse();
			if((old&7)==1){
				if((m->buttons&7)==3)
					pl_snarfentry(p, 1);
				if((m->buttons&7)==5)
					pl_snarfentry(p, 0);
			}
		}
		p->state=UP;
		pldraw(p, p->b);
	}
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
	case Kesc:
		pl_snarfentry(p, 1);
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
		if(c < 0x20 || c == Kdel || (c & 0xFF00) == KF || (c & 0xFF00) == Spec)
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
		*ep->entp='\0';
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
