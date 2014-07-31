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

Pal *
newpal(char *f)
{
	Pal *p;
	
	p = emalloc(sizeof(*p));
	p->type = PAL;
	p->sel = -1;
	filinit(p, f);
	return p;
}

void
putpal(Pal *p)
{
	int i;

	for(i = 0; i < p->ncol; i++)
		freeimage(p->ims[i]);
	free(p->cols);
	free(p->ims);
}

int
readpal(Pal *p, Biobuf *bp)
{
	char *s, *sp;
	char *args[8];
	int nc, i, c;
	
	s = nil;
	if(tline(bp, &s, args, nelem(args)) != 2)
		goto err;
	if(strcmp(args[0], "pal") != 0)
		goto err;
	nc = strtol(args[1], &sp, 0);
	if(*sp != 0 || nc < 0)
		goto err;
	free(s);
	s = nil;
	p->ncol = nc;
	p->cols = emalloc(nc * sizeof(*p->cols));
	p->ims = emalloc(nc * sizeof(*p->ims));
	for(i = 0; i < nc; i++){
		if(tline(bp, &s, args, nelem(args)) != 1)
			goto err;
		c = strtol(args[0], &sp, 0);
		if(*sp != 0 || c < 0 || c > 0xffffff)
			goto err;
		p->cols[i] = c;
		free(s);
		s = nil;
	}
	for(i = 0; i < nc; i++)
		p->ims[i] = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, p->cols[i] << 8 | 0xff);
	p->id = getident(bp->fid);
	return 0;
err:
	if(s != nil)
		free(s);
	werrstr("invalid format");
	return -1;
}

int
writepal(Pal *p, char *f)
{
	Biobuf *bp;
	int i, rc, n;

	if(f == nil)
		f = p->name;
	bp = Bopen(f, OWRITE);
	if(bp == nil){
		cmdprint("?%r\n");
		return -1;
	}
	n = 0;
	rc = Bprint(bp, "pal %d\n", p->ncol);
	if(rc < 0) goto err;
	n += rc;
	for(i = 0; i < p->ncol; i++){
		rc = Bprint(bp, "%#.6x\n", p->cols[i]);
		if(rc < 0) goto err;
		n += rc;
	}
	if(Bterm(bp) < 0){
		cmdprint("?%r\n");
		return -1;
	}
	p->change = 0;
	cmdprint("%s: #%d\n", f, n);
	return 0;
err:
	cmdprint("?%r\n");
	Bterm(bp);
	return -1;
}

Pal *
findpal(char *sf, char *fn, int op)
{
	File *f;
	char *s, *q;
	Ident i;
	int fd;
	Biobuf *bp;
	Pal *p;
	
	if(sf == nil)
		sf = "";
	s = emalloc(strlen(sf) + strlen(fn) + 2);
	strcpy(s, sf);
	q = strrchr(s, '/');
	if(q != nil)
		*++q = 0;
	else
		*s = 0;
	strcpy(s, fn);
	fd = open(s, OREAD);
	if(fd < 0){
		free(s);
		return nil;
	}
	i = getident(fd);
	if(i.type == (uint)-1){
		close(fd);
		return nil;
	}
	for(f = flist.next; f != &flist; f = f->next)
		if(f->type == PAL && identcmp(&f->id, &i) == 0){
			close(fd);
			putident(i);
			return (Pal *) f;
		}
	putident(i);
	if(op == 0){
		close(fd);
		return nil;
	}
	bp = emalloc(sizeof(*bp));
	Binit(bp, fd, OREAD);
	p = newpal(s);
	if(readpal(p, bp) < 0){
		putfil(p);
		p = nil;
		goto end;
	}
end:
	Bterm(bp);
	close(fd);
	free(bp);
	free(s);
	return p;
}

static void
palredraw(Pal *p)
{
	File *f;

	filredraw(p);
	for(f = flist.next; f != &flist; f = f->next)
		if(f->type == SPR && ((Spr *) f)->pal == p)
			filredraw(f);
}

void
palsize(Pal *p, int sz, int ch)
{
	int i;

	if(sz == p->ncol)
		return;
	p->cols = realloc(p->cols, sz * sizeof(*p->cols));
	p->ims = realloc(p->ims, sz * sizeof(*p->ims));
	if(sz > p->ncol)
		for(i = p->ncol; i < sz; i++){
			p->cols[i] = 0;
			p->ims[i] = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0);
		}
	p->ncol = sz;
	if(ch)
		change(p);
	palredraw(p);
}

void
paldraw(Win *w)
{
	Pal *p;
	int n, i;
	Rectangle r;
	
	if(w->type != PAL || w->f == nil)
		sysfatal("paldraw: phase error");
	p = (Pal *) w->f;
	n = Dx(w->inner) / w->zoom;
	draw(w->im, w->inner, w->tab->cols[BACK], nil, ZP);
	for(i = 0; i < p->ncol; i++){
		r.min = addpt(w->inner.min, mulpt(Pt(i%n, i/n), w->zoom));
		r.max.x = r.min.x + w->zoom;
		r.max.y = r.min.y + w->zoom;
		draw(w->im, r, p->ims[i], nil, ZP);
		if(p->sel == i)
			border(w->im, r, SELSIZ, display->white, ZP);
	}
}

void
palset(Pal *p, int s, u32int c)
{
	if(s < 0 || s >= p->ncol || p->cols[s] == c)
		return;
	p->cols[s] = c;
	freeimage(p->ims[s]);
	p->ims[s] = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, c << 8 | 0xff);
	change(p);
	palredraw(p);
}

static int
palinit(Win *w)
{
	w->zoom = 32;
	return 0;
}

static void
palzerox(Win *w, Win *v)
{
	v->zoom = w->zoom;
}

static void
palclick(Win *w, Mousectl *mc)
{
	int n, i;
	Point pt;
	Pal *p;
	
	if(!ptinrect(mc->xy, w->inner))
		return;
	if(w->f == nil)
		sysfatal("palclick: phase error");
	p = (Pal *) w->f;
	n = Dx(w->inner) / w->zoom;
	pt = subpt(mc->xy, w->inner.min);
	if(pt.x >= n * w->zoom)
		return;
	i = pt.x / w->zoom + pt.y / w->zoom * n;
	if(i >= p->ncol)
		return;
	p->sel = i;
	palredraw(p);
}

Wintab paltab = {
	.init = palinit,
	.click = palclick,
	.draw = paldraw,
	.zerox = palzerox,
	.hexcols = {
		[BORD] 0xAA0000FF,
		[DISB] 0xCC8888FF,
		[BACK] 0xFFCCFFFF,
	},
};
