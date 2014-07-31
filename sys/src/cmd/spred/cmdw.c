#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include "dat.h"
#include "fns.h"

static int
cmdinit(Win *)
{
	return 0;
}

static void
scrollbar(Win *w)
{
	int h, t0, t1;

	h = Dy(w->inner);
	draw(w->im, rectaddpt(Rect(0, 0, SCRBSIZ+1, h), w->inner.min), w->tab->cols[BORD], nil, ZP);
	t0 = w->toprune * h;
	t1 = (w->toprune + w->fr.nchars) * h;
	if(w->nrunes == 0){
		 t0 = 0;
		 t1 = h;
	}else{
		t0 /= w->nrunes;
		t1 /= w->nrunes;
	}
	draw(w->im, rectaddpt(Rect(0, t0, SCRBSIZ, t1), w->inner.min), w->tab->cols[BACK], nil, ZP);
}

static void
cmddraw(Win *w)
{
	Rectangle r;
	
	frclear(&w->fr, 0);
	r = insetrect(w->inner, 1);
	r.min.x += SCRTSIZ;
	scrollbar(w);
	frinit(&w->fr, r, display->defaultfont, w->im, w->tab->cols);
	frinsert(&w->fr, w->runes + w->toprune, w->runes + w->nrunes, 0);
}

void
cmdscroll(Win *w, int l)
{
	int r;
	
	if(l == 0)
		return;
	if(l > 0){
		for(r = w->toprune; r < w->nrunes && l != 0; r++)
			if(w->runes[r] == '\n')
				l--;
		w->toprune = r;
	}else{
		for(r = w->toprune; r > 0; r--)
			if(w->runes[r] == '\n' && ++l == 0){
				r++;
				break;
			}
		w->toprune = r;
	
	}
	frdelete(&w->fr, 0, w->fr.nchars);
		frinsert(&w->fr, w->runes + w->toprune, w->runes + w->nrunes, 0);
	scrollbar(w);
}

static void
cmdclick(Win *w, Mousectl *mc)
{
	if(mc->xy.x <= w->inner.min.x + SCRBSIZ){
		cmdscroll(w, -5);
		return;
	}
	frselect(&w->fr, mc);
}

static int
cmdrmb(Win *w, Mousectl *mc)
{
	if(mc->xy.x > w->inner.min.x + SCRBSIZ)
		return -1;
	cmdscroll(w, 5);
	return 0;
}

int
cmdinsert(Win *w, Rune *r, int nr, int rp)
{
	Rune *s;

	if(nr < 0)
		for(nr = 0, s = r; *s++ != 0; nr++)
			;
	if(rp < 0 || rp > w->nrunes)
		rp = w->nrunes;
	if(w->nrunes + nr > w->arunes){
		w->runes = realloc(w->runes, w->arunes = w->arunes + (nr + RUNEBLK - 1) & ~(RUNEBLK - 1));
		if(w->runes == nil)
			sysfatal("realloc: %r");
	}
	if(rp != w->nrunes)
		memmove(w->runes + rp, w->runes + rp + nr, (w->nrunes - rp) * sizeof(Rune));
	memmove(w->runes + rp, r, nr * sizeof(Rune));
	w->nrunes += nr;
	if(w->toprune > rp)
		w->toprune += nr;
	else{
		frinsert(&w->fr, w->runes + rp, w->runes + rp + nr, rp - w->toprune);
		if(rp == w->nrunes - nr){
			if(w->fr.lastlinefull)
				cmdscroll(w, 1);
		}
	}
	return nr;
}

static void
cmddel(Win *w, int a, int b)
{
	if(a >= b)
		return;
	memmove(w->runes + a, w->runes + b, w->nrunes - b);
	w->nrunes -= b - a;
	if(w->toprune >= b)
		w->toprune -= b - a;
	else{
		frdelete(&w->fr, a - w->toprune, b - w->toprune);
		if(w->toprune >= a)
			w->toprune = a;
	}
}

static void
cmdkey(Win *w, Rune r)
{
	static char buf[4096];
	char *p;
	Rune *q;

	if(w->fr.p0 < w->fr.p1)
		cmddel(w, w->toprune + w->fr.p0, w->toprune + w->fr.p1);
	switch(r){
	case 0x00:
	case 0x1b:
		break;
	case '\b':
		if(w->fr.p0 > 0 && w->toprune + w->fr.p0 != w->opoint)
			cmddel(w, w->toprune + w->fr.p0 - 1, w->toprune + w->fr.p0);
		break;
	case '\n':
		cmdinsert(w, &r, 1, w->fr.p0 + w->toprune);
		if(w->toprune + w->fr.p0 == w->nrunes){
			q = w->runes + w->opoint;
			p = buf;
			while(q < w->runes + w->nrunes && p < buf + nelem(buf) + 1)
				p += runetochar(p, q++);
			*p = 0;
			w->opoint = w->nrunes;
			docmd(buf);
		}
		break;
	case Kview:
		cmdscroll(w, 3);
		break;
	case Kup:
		cmdscroll(w, -3);
		break;
	default:
		cmdinsert(w, &r, 1, w->fr.p0 + w->toprune);
	}
}

void
cmdprint(char *fmt, ...)
{
	Rune *r;
	va_list va;
	
	va_start(va, fmt);
	r = runevsmprint(fmt, va);
	va_end(va);
	if(r != nil)
		cmdw->opoint += cmdinsert(cmdw, r, -1, cmdw->opoint);
}

Wintab cmdtab = {
	.init = cmdinit,
	.draw = cmddraw,
	.click = cmdclick,
	.rmb = cmdrmb,
	.key = cmdkey,
	.hexcols = {
		[BORD] DPurpleblue,
		[DISB] 0xCCCCEEFF,
		[BACK] 0xCCFFFFFF,
		[HIGH] DPalegreygreen
	}
};
