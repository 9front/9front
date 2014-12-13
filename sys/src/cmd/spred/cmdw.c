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
	if(w->opoint > rp)
		w->opoint += nr;
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
	if(a <= w->opoint && w->opoint < b)
		w->opoint = a;
	else if(w->opoint >= b)
		w->opoint -= b -  a;
}

static void
setsel(Win *w, int p0, int p1)
{
	frdrawsel(&w->fr, frptofchar(&w->fr, w->fr.p0), w->fr.p0, w->fr.p1, 0);
	w->fr.p0 = p0;
	w->fr.p1 = p1;
	frdrawsel(&w->fr, frptofchar(&w->fr, p0), p0, p1, 1);
}

static void
cmdline(Win *w)
{
	static char buf[4096];
	Rune *q;
	char *p;

	q = w->runes + w->opoint;
	p = buf;
	while(q < w->runes + w->nrunes && p < buf + nelem(buf) + 1)
		p += runetochar(p, q++);
	*p = 0;
	w->opoint = w->nrunes;
	docmd(buf);
}

static void
cmdkey(Win *w, Rune r)
{
	switch(r){
	case Kview:
		cmdscroll(w, 3);
		return;
	case Kup:
		cmdscroll(w, -3);
		return;
	case Kleft:
		if(w->fr.p0 == 0)
			return;
		setsel(w, w->fr.p0 - 1, w->fr.p0 - 1);
		return;
	case Kright:
		if(w->toprune + w->fr.p1 == w->nrunes)
			return;
		setsel(w, w->fr.p1 + 1, w->fr.p1 + 1);
		return;
	}
	if(w->fr.p0 < w->fr.p1)
		cmddel(w, w->toprune + w->fr.p0, w->toprune + w->fr.p1);
	switch(r){
	case 0x00:
	case Kesc:
		break;
	case '\b':
		if(w->fr.p0 > 0 && w->toprune + w->fr.p0 != w->opoint)
			cmddel(w, w->toprune + w->fr.p0 - 1, w->toprune + w->fr.p0);
		break;
	case '\n':
		cmdinsert(w, &r, 1, w->fr.p0 + w->toprune);
		if(w->toprune + w->fr.p0 == w->nrunes)
			cmdline(w);
		break;
	default:
		cmdinsert(w, &r, 1, w->fr.p0 + w->toprune);
	}
}

static int
tosnarf(Win *w, int p0, int p1)
{
	int fd;
	static char buf[512];
	char *c, *ce;
	Rune *rp, *re;
	
	if(p0 >= p1)
		return 0;
	fd = open("/dev/snarf", OWRITE|OTRUNC);
	if(fd < 0){
		cmdprint("tosnarf: %r");
		return -1;
	}
	c = buf;
	ce = buf + sizeof(buf);
	rp = w->runes + p0;
	re = w->runes + p1;
	for(; rp < re; rp++){
		if(c + UTFmax > ce){
			write(fd, buf, c - buf);
			c = buf;
		}
		c += runetochar(c, rp);
	}
	if(c > buf)
		write(fd, buf, c - buf);
	close(fd);
	return 0;
}

static int
fromsnarf(Win *w, int p0)
{
	int fd, rc;
	char *buf, *p;
	Rune *rbuf, *r;
	int nc, end;
	
	fd = open("/dev/snarf", OREAD);
	if(fd < 0){
		cmdprint("fromsnarf: %r");
		return -1;
	}
	buf = nil;
	nc = 0;
	for(;;){
		buf = realloc(buf, nc + 4096);
		rc = readn(fd, buf + nc, nc + 4096);
		if(rc <= 0)
			break;
		nc += rc;
		if(rc < 4096)
			break;
	}
	close(fd);
	rbuf = emalloc(sizeof(Rune) * nc);
	r = rbuf;
	for(p = buf; p < buf + nc; r++)
		p += chartorune(r, p);
	end = p0 == w->nrunes;
	cmdinsert(w, rbuf, r - rbuf, p0);
	if(end && r > rbuf && r[-1] == '\n')
		cmdline(w);
	return 0;
}

static void
cmdmenu(Win *w, Mousectl *mc)
{
	enum {
		CUT,
		PASTE,
		SNARF,
	};
	static char *ms[] = {
		[CUT] "cut",
		[PASTE] "paste",
		[SNARF] "snarf",
		nil,
	};
	static Menu m = {ms};
	
	switch(menuhit(2, mc, &m, nil)){
	case CUT:
		if(tosnarf(w, w->toprune + w->fr.p0, w->toprune + w->fr.p1) >= 0)
			cmddel(w, w->toprune + w->fr.p0, w->toprune + w->fr.p1);
		break;
	case SNARF:
		tosnarf(w, w->toprune + w->fr.p0, w->toprune + w->fr.p1);
		break;
	case PASTE:
		if(w->fr.p0 < w->fr.p1)
			cmddel(w, w->toprune + w->fr.p0, w->toprune + w->fr.p1);
		fromsnarf(w, w->toprune + w->fr.p0);
		break;
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
	.menu = cmdmenu,
	.rmb = cmdrmb,
	.key = cmdkey,
	.hexcols = {
		[BORD] DPurpleblue,
		[DISB] 0xCCCCEEFF,
		[BACK] 0xCCFFFFFF,
		[HIGH] DPalegreygreen
	}
};
