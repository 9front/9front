#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <cursor.h>
#include <frame.h>
#include "dat.h"
#include "fns.h"

Screen *scr;
extern Wintab *tabs[];
Win wlist;
File flist;
Win *actw, *actf, *cmdw;
Image *invcol;

void*
emalloc(ulong sz)
{
	void *v;
	
	v = malloc(sz);
	if(v == nil)
		sysfatal("malloc: %r");
	memset(v, 0, sz);
	setmalloctag(v, getcallerpc(&sz));
	return v;
}

void
initwin(void)
{
	Rectangle r;
	int i, j;

	scr = allocscreen(screen, display->white, 0);
	if(scr == nil)
		sysfatal("allocscreen: %r");
	for(i = 0; i < NTYPES; i++)
		for(j = 0; j < NCOLS; j++)
			tabs[i]->cols[j] = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, tabs[i]->hexcols[j]);
	invcol = allocimage(display, Rect(0, 0, 2, 2), screen->chan, 1, 0);
	draw(invcol, Rect(1, 0, 2, 1), display->white, nil, ZP);
	draw(invcol, Rect(0, 1, 1, 2), display->white, nil, ZP);
	wlist.next = wlist.prev = &wlist;
	flist.next = flist.prev = &flist;
	r = screen->r;
	r.max.y = r.min.y + Dy(r) / 5;
	cmdw = newwin(CMD, r, nil);
	if(cmdw == nil)
		sysfatal("newwin: %r");
}

Win *
newwin(int t, Rectangle r, File *f)
{
	Win *w;
	
	w = emalloc(sizeof(*w));
	w->next = &wlist;
	w->prev = wlist.prev;
	w->next->prev = w;
	w->prev->next = w;
	w->type = t;
	w->tab = tabs[t];
	w->entire = r;
	w->inner = insetrect(r, BORDSIZ);
	w->im = allocwindow(scr, r, Refbackup, 0);
	draw(w->im, w->inner, w->tab->cols[BACK], nil, ZP);
	if(f != nil){
		incref(f);
		w->wprev = f->wins.wprev;
		w->wnext = &f->wins;
		f->wins.wprev->wnext = w;
		f->wins.wprev = w;
		w->f = f;
	}
	w->tab->init(w);
	setfocus(w);
	w->tab->draw(w);
	return w;
}

Win *
newwinsel(int t, Mousectl *mc, File *f)
{
	Rectangle u;

	u = getrect(3, mc);
	if(Dx(u) < MINSIZ || Dy(u) < MINSIZ)
		return nil;
	rectclip(&u, screen->r);
	return newwin(t, u, f);
}

void
winzerox(Win *w, Mousectl *mc)
{
	Win *v;

	if(w->tab->zerox == nil){
		cmdprint("?\n");
		return;
	}
	v = newwinsel(w->type, mc, w->f);
	if(v == nil)
		return;
	w->tab->zerox(w, v);
	v->tab->draw(v);
}

void
winclose(Win *w)
{
	if(w->f == nil){
		cmdprint("?\n");
		return;
	}
	if(!decref(w->f)){
		if(w->f->change > 0){
			cmdprint("?\n");
			incref(w->f);
			w->f->change = -1;
			return;
		}
		putfil(w->f);
		w->f = nil;
	}
	freeimage(w->im);
	if(w->f != nil){
		w->wnext->wprev = w->wprev;
		w->wprev->wnext = w->wnext;
	}
	w->next->prev = w->prev;
	w->prev->next = w->next;
	if(w == actw)
		actw = nil;
	if(w == actf)
		actf = nil;
	free(w);
}

void
setfocus(Win *w)
{
	if(actw != nil)
		border(actw->im, actw->entire, BORDSIZ, actw->tab->cols[DISB], ZP);
	actw = w;
	if(w != cmdw)
		actf = w;
	if(w == nil)
		return;
	if(w->im == nil)
		sysfatal("setfocus: phase error");
	topwindow(w->im);
	w->prev->next = w->next;
	w->next->prev = w->prev;
	w->prev = wlist.prev;
	w->next = &wlist;
	w->prev->next = w;
	w->next->prev = w;
	border(w->im, w->entire, BORDSIZ, w->tab->cols[BORD], ZP);
}

static Win *
winpoint(Point p)
{
	Win *w;
	
	for(w = wlist.prev; w != &wlist; w = w->prev)
		if(ptinrect(p, w->entire))
			return w;
	return nil;
}

void
winclick(Mousectl *mc)
{
	Win *w;
	
	w = winpoint(mc->xy);
	if(w != nil){
		if(w != actw)
			setfocus(w);
		w->tab->click(w, mc);
	}
	while((mc->buttons & 1) != 0)
		readmouse(mc);
}

Win *
winsel(Mousectl *mc, int but)
{
	extern Cursor crosscursor;
	int m;
	Win *w;
	
	m = 1 << but - 1;
	setcursor(mc, &crosscursor);
	for(;;){
		readmouse(mc);
		if((mc->buttons & ~m) != 0){
			w = nil;
			goto end;
		}
		if((mc->buttons & m) != 0)
			break;
	}
	w = winpoint(mc->xy);
end:
	while(readmouse(mc), mc->buttons != 0)
		;
	setcursor(mc, nil);
	return w;
}

void
winresize(Win *w, Mousectl *mc)
{
	Rectangle r;
	
	if(w == nil)
		return;
	r = getrect(3, mc);
	if(Dx(r) < MINSIZ || Dy(r) < MINSIZ)
		return;
	rectclip(&r, screen->r);
	freeimage(w->im);
	w->entire = r;
	w->inner = insetrect(r, BORDSIZ);
	w->im = allocwindow(scr, r, Refbackup, 0);
	draw(w->im, w->inner, w->tab->cols[BACK], nil, ZP);
	setfocus(w);
	w->tab->draw(w);
}

void
resize(void)
{
	Rectangle old, r;
	int dxo, dyo, dxn, dyn;
	Win *w;
	
	old = screen->r;
	dxo = Dx(old);
	dyo = Dy(old);
	if(getwindow(display, Refnone) < 0)
		sysfatal("resize failed: %r");
	dxn = Dx(screen->r);
	dyn = Dy(screen->r);
	freescreen(scr);
	scr = allocscreen(screen, display->white, 0);
	if(scr == nil)
		sysfatal("allocscreen: %r");
	for(w = wlist.next; w != &wlist; w = w->next){
		r = rectsubpt(w->entire, old.min);
		r.min.x = muldiv(r.min.x, dxn, dxo);
		r.max.x = muldiv(r.max.x, dxn, dxo);
		r.min.y = muldiv(r.min.y, dyn, dyo);
		r.max.y = muldiv(r.max.y, dyn, dyo);
		w->entire = rectaddpt(r, screen->r.min);
		w->inner = insetrect(w->entire, BORDSIZ);
		freeimage(w->im);
		w->im = allocwindow(scr, w->entire, Refbackup, 0);
		if(w->im == nil)
			sysfatal("allocwindow: %r");
		draw(w->im, w->inner, w->tab->cols[BACK], nil, ZP);
		border(w->im, w->entire, BORDSIZ, w->tab->cols[w == actw ? BORD : DISB], ZP);
		w->tab->draw(w);
	}
}

extern Wintab cmdtab, paltab, sprtab;

Wintab *tabs[] = {
	[CMD] &cmdtab,
	[PAL] &paltab,
	[SPR] &sprtab,
};
