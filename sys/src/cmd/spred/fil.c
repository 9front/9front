#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <frame.h>
#include "dat.h"
#include "fns.h"

int
tline(Biobuf *bp, char **str, char **args, int max)
{
	char *s, *p;
	int q, dq, rc;

	do{
		s = Brdstr(bp, '\n', 10);
		if(s == nil)
			return -1;
		q = dq = 0;
		for(p = s; *p != 0; p++)
			if(*p == '\'')
				dq = !dq;
			else{
				if(dq){
					q = !q;
					dq = 0;
				}
				if(*p == '#' && !q){
					*p = 0;
					break;
				}
			}
		rc = tokenize(s, args, max);
	}while(rc == 0 && (free(s), 1));
	*str = s;
	return rc;
}

Ident
getident(int fd)
{
	Dir *d;
	Ident i;
	
	d = dirfstat(fd);
	if(d == nil)
		return (Ident){-1, -1, (Qid){0, 0, 0}};
	i = (Ident){d->type, d->dev, d->qid};
	free(d);
	return i;
}

void
putident(Ident)
{
}

int
identcmp(Ident *a, Ident *b)
{
	return a->type != b->type || a->dev != b->dev || a->path != b->path;
}

int
filcmp(File *f, File *g)
{
	if(f->type != g->type)
		return f->type - g->type;
	if(f->name == nil || g->name == nil)
		return -1;
	return strcmp(f->name, g->name);
}

void
filinit(File *f, char *t)
{
	File *g;

	f->wins.wnext = f->wins.wprev = &f->wins;
	f->name = strdup(t);
	for(g = flist.next; g != &flist && filcmp(g, f) < 0; g = g->next)
		;
	f->prev = g->prev;
	f->next = g;
	g->prev->next = f;
	g->prev = f;
}

void
putfil(File *f)
{
	switch(f->type){
	case PAL: putpal((Pal *) f); break;
	case SPR: putspr((Spr *) f); break;
	}
	f->prev->next = f->next;
	f->next->prev = f->prev;
	free(f->name);
	free(f);
}

static char phasetitle[] = "??? phase error ???";

int
filtitlelen(File *f)
{
	if(f->name != nil)
		return utflen(f->name) + 4;
	return strlen(phasetitle);
}

char *
filtitle(File *f, char *s, char *e)
{
	if(f->name == nil)
		return strecpy(s, e, phasetitle);
	*s++ = f->change ? '\'' : ' ';
	if(f->wins.wnext != &f->wins)
		if(f->wins.wnext->wnext != &f->wins)
			*s++ = '*';
		else
			*s++ = '+';
	else
		*s++ = '-';
	*s++ = actf != nil && f == actf->f ? '.' : ' ';
	*s++ = ' ';
	return strecpy(s, e, f->name);
}

void
winwrite(Win *w, char *f)
{
	if(w->f == nil){
		cmdprint("?\n");
		return;
	}
	switch(w->type){
	case PAL:
		writepal((Pal *) w->f, f);
		return;
	case SPR:
		writespr((Spr *) w->f, f);
		return;
	}
	cmdprint("?\n");
}

void
filredraw(File *f)
{
	Win *w;
	
	for(w = f->wins.wnext; w != &f->wins; w = w->wnext)
		w->tab->draw(w);
}

void
change(File *f)
{
	f->change = 1;
	quitok = 0;
}
