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

Spr *
newspr(char *f)
{
	Spr *s;
	
	s = emalloc(sizeof(*s));
	s->type = SPR;
	filinit(s, f);
	return s;
}

void
putspr(Spr *s)
{
	if(s->pal != nil && !decref(s->pal) && s->pal->change <= 0)
		putfil(s->pal);
	free(s->palfile);
	free(s->data);
}

int
readspr(Spr *s, Biobuf *bp)
{
	char *args0[8], *p, *ss, **args;
	int n, i, j;

	args = nil;
	ss = nil;
	if(tline(bp, &ss, args0, nelem(args0)) != 4)
		goto err;
	if(strcmp(args0[0], "sprite") != 0)
		goto err;
	n = strtol(args0[1], &p, 0);
	if(*p != 0 || n < 0)
		goto err;
	s->w = n;
	n = strtol(args0[2], &p, 0);
	if(*p != 0 || n < 0)
		goto err;
	s->h = n;
	if(*args0[3] != 0)
		s->palfile = strdup(args0[3]);
	else
		s->palfile = nil;
	free(ss);
	ss = nil;
	s->data = emalloc(s->w * s->h * sizeof(u32int));
	args = emalloc((s->w + 1) * sizeof(char *));
	for(i = 0; i < s->h; i++){
		if(tline(bp, &ss, args, s->w + 1) != s->w)
			goto err;
		for(j = 0; j < s->w; j++){
			n = strtol(args[j], &p, 0);
			if(*p != 0 || n < 0)
				goto err;
			s->data[i * s->w + j] = n;
		}
		free(ss);
		ss = nil;
	}
	free(args);
	return 0;
err:
	werrstr("invalid format");
	free(s->data);
	free(args);
	s->w = 0;
	s->h = 0;
	return -1;
}

int
writespr(Spr *s, char *file)
{
	Biobuf *bp;
	int n, rc;
	int i, j;

	if(file == nil)
		file = s->name;
	bp = Bopen(file, OWRITE);
	if(bp == nil){
		cmdprint("?%r\n");
		return -1;
	}
	rc = Bprint(bp, "sprite %d %d %q\n", s->w, s->h, s->palfile != nil ? s->palfile : "");
	if(rc < 0) goto err;
	n = rc;
	for(i = 0; i < s->h; i++)
		for(j = 0; j < s->w; j++){
			rc = Bprint(bp, "%d%c", s->data[s->w * i + j], j == s->w - 1 ? '\n' : ' ');
			if(rc < 0) goto err;
			n += rc;
		}
	if(Bterm(bp) < 0){
		cmdprint("?%r\n");
		return -1;
	}
	s->change = 0;
	quitok = 0;
	cmdprint("%s: #%d\n", file, n);
	return 0;
err:
	cmdprint("?%r\n");
	Bterm(bp);
	return -1;
}

int
sprinit(Win *w)
{
	w->zoom = 4;
	return 0;
}

static Rectangle
sprrect(Win *w, Rectangle s)
{
	Rectangle r;
	Point p, q;
	Spr *t;

	t = (Spr *) w->f;
	p = Pt(t->w * w->zoom, t->h * w->zoom);
	q = addpt(divpt(addpt(s.min, s.max), 2), w->scr);
	r.min = subpt(q, divpt(p, 2));
	r.max = addpt(r.min, p);
	return r;
}

static void
scrollbars(Win *w)
{
	Rectangle r, s;
	int dx, dy;
	int t0, t1;

	if(rectinrect(w->sprr, w->inner))
		return;
	r = w->inner;
	dx = Dx(r) - SCRTSIZ;
	dy = Dy(r) - SCRTSIZ;
	if(dx <= 0 || dy <= 0)
		return;
	s = r;
	if(!rectclip(&s, w->sprr))
		return;
	draw(w->im, Rect(r.min.x, r.max.y - SCRBSIZ, r.max.x - SCRTSIZ, r.max.y), w->tab->cols[BORD], nil, ZP);
	draw(w->im, Rect(r.max.x - SCRBSIZ, r.min.y, r.max.x, r.max.y - SCRTSIZ), w->tab->cols[BORD], nil, ZP);
	t0 = (s.min.x - w->sprr.min.x) * dx / Dx(w->sprr) + r.min.x;
	t1 = (s.max.x - w->sprr.min.x) * dx / Dx(w->sprr) + r.min.x;
	draw(w->im, Rect(t0, r.max.y - SCRBSIZ + 1, t1, r.max.y), w->tab->cols[BACK], nil, ZP);
	t0 = (s.min.y - w->sprr.min.y) * dy / Dy(w->sprr) + r.min.y;
	t1 = (s.max.y - w->sprr.min.y) * dy / Dy(w->sprr) + r.min.y;
	draw(w->im, Rect(r.max.x - SCRBSIZ, t0, r.max.x, t1), w->tab->cols[BACK], nil, ZP);
}

void
sprdraw(Win *w)
{
	Rectangle r, t;
	Spr *s;
	Pal *p;
	int i, j;
	Image *im;
	u32int *d;

	if(w->type != SPR || w->f == nil)
		sysfatal("sprdraw: phase error");
	s = (Spr *) w->f;
	p = s->pal;
	draw(w->im, w->inner, w->tab->cols[BACK], nil, ZP);
	r = sprrect(w, w->inner);
	w->sprr = r;
	if(!rectinrect(r, w->inner)){
		t = w->inner;
		t.max.x -= SCRTSIZ;
		t.max.y -= SCRTSIZ;
		r = sprrect(w, t);
		w->sprr = r;
		rectclip(&r, t);
		scrollbars(w);
	}
	d = s->data;
	for(j = 0; j < s->h; j++)
		for(i = 0; i < s->w; i++, d++){
			t.min = addpt(w->sprr.min, Pt(i * w->zoom, j * w->zoom));
			t.max = addpt(t.min, Pt(w->zoom, w->zoom));
			if(!rectclip(&t, r))
				continue;
			if(p != nil && *d < p->ncol)
				im = p->ims[*d];
			else
				im = invcol;
			draw(w->im, t, im, nil, ZP);
		}
}

static int
sprbars(Win *w, Mousectl *mc)
{
	int d;

	if(rectinrect(w->sprr, w->inner))
		return -1;
	if(mc->xy.x >= w->inner.max.x - SCRBSIZ){
		d = Dy(w->inner) / 5;
		switch(mc->buttons){
		case 1: w->scr.y += d; break;
		case 4: w->scr.y -= d; break;
		default: return 0;
		}
		sprdraw(w);
		return 0;
	}
	if(mc->xy.y >= w->inner.max.y - SCRBSIZ){
		d = Dx(w->inner) / 5;
		switch(mc->buttons){
		case 1: w->scr.x += d; break;
		case 4: w->scr.x -= d; break;
		default: return 0;
		}
		sprdraw(w);
		return 0;
	}
	return -1;
}

void
sprclick(Win *w, Mousectl *mc)
{
	Spr *s;
	Pal *p;
	Point q;
	
	if(w->f == nil)
		sysfatal("sprclick: phase error");
	if(sprbars(w, mc) >= 0)
		return;
	s = (Spr *) w->f;
	p = s->pal;
	if(p == nil || p->sel < 0 || p->sel >= p->ncol)
		return;
	do{
		q = divpt(subpt(mc->xy, w->sprr.min), w->zoom);
		if(q.x < 0 || q.y < 0 || q.x >= s->w || q.y >= s->h)
			continue;
		if(s->data[q.y * s->w + q.x] != p->sel){
			s->data[q.y * s->w + q.x] = p->sel;
			change(s);
			sprdraw(w);
		}
	}while(readmouse(mc) >= 0 && (mc->buttons & 1) != 0);
}

void
sprsize(Spr *s, int n, int m, int ch)
{
	u32int *v;
	int i, j, w, h;
	
	v = s->data;
	if(s->w == n && s->h == m)
		return;
	s->data = emalloc(n * m * sizeof(u32int));
	w = n < s->w ? n : s->w;
	h = m < s->h ? m : s->h;
	for(j = 0; j < h; j++)
		for(i = 0; i < w; i++)
			s->data[j * n + i] = v[j * w + i];
	s->w = n;
	s->h = m;
	if(ch)
		change(s);
	filredraw(s);
}

static char *
palfile(char *, char *n)
{
	return strdup(n);
}

static void
sprmenu(Win *w, Mousectl *mc)
{
	enum { MPAL };
	static char *menus[] = {
		"pal",
		nil,
	};
	static Menu menu = {menus};
	Win *wp;
	Spr *s;
	
	if(w->f == nil)
		sysfatal("sprmenu: phase error");
	s = (Spr *) w->f;
	switch(menuhit(2, mc, &menu, scr)){
	case MPAL:
		wp = winsel(mc, 2);
		if(wp == nil || wp->type != PAL)
			break;
		if(wp->f == nil)
			sysfatal("sprmenu: pal phase error");
		if(s->pal != (Pal *) wp->f){
			if(s->pal != nil && decref(s->pal) == 0 && s->pal->change <= 0)
				putfil(s->pal);
			incref(wp->f);
			s->pal = (Pal *) wp->f;
			free(s->palfile);
			s->palfile = palfile(s->name, s->pal->name);
			cmdprint("palette set to %q\n", s->palfile);
			change(s);
			filredraw(s);
		}
		break;
	}
}

static void
sprzerox(Win *w, Win *v)
{
	v->zoom = w->zoom;
	v->scr = w->scr;
}

static void
sprkey(Win *w, Rune r)
{
	static char keys[] = "1234567890qwertyuiop";
	char *p;
	Spr *s;
	
	s = (Spr *) w->f;
	if(s == nil)
		sysfatal("sprkey: phase error");
	if(r < 0x100 && (p = strchr(keys, r)) != nil){
		if(s->pal == nil || p - keys >= s->pal->ncol)
			return;
		s->pal->sel = p - keys;
		filredraw(s->pal);
	}
}

Wintab sprtab = {
	.init = sprinit,
	.click = sprclick,
	.draw = sprdraw,
	.menu = sprmenu,
	.rmb = sprbars,
	.zerox = sprzerox,
	.key = sprkey,
	.hexcols = {
		[BORD] 0x00AA00FF,
		[DISB] 0x88CC88FF,
		[BACK] 0xCCFFCCFF,
	},
};
