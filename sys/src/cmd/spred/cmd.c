#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <frame.h>
#include "dat.h"
#include "fns.h"

extern Mousectl *mc;

static void
dopal(int, char **argv)
{
	Pal *p;
	
	p = findpal("", argv[1], 2);
	if(p == nil){
		cmdprint("?%r\n");
		p = newpal(argv[1]);
		palsize(p, 8, 0);
	}
	if(newwinsel(PAL, mc, p) == nil){
		if(p->ref == 0)
			putfil(p);
		return;
	}
}

static void
dosize(int, char **argv)
{
	char *p;
	int n, m;

	if(actf == nil)
		goto err;
	switch(actf->type){
	case PAL:
		n = strtol(argv[1], &p, 0);
		if(*p != 0 || n < 0)
			goto err;
		palsize((Pal *) actf->f, n, 1);
		return;
	case SPR:
		n = strtol(argv[1], &p, 0);
		if(*p != '*' || n < 0)
			goto err;
		m = strtol(++p, &p, 0);
		if(*p != 0 || m < 0)
			goto err;
		sprsize((Spr *) actf->f, n, m, 1);
		return;
	}
err:
	cmdprint("?\n");
}

static void
doset(int, char **argv)
{
	int n;
	char *p;
	Pal *q;

	if(actf == nil)
		goto err;
	switch(actf->type){
	case PAL:
		n = strtol(argv[1], &p, 0);
		if(*p != 0 || n < 0 || n > 0xffffff)
			goto err;
		q = (Pal *) actf->f;
		palset(q, q->sel, n);
		return;
	}
err:
	cmdprint("?\n");
}

static void
dozoom(int, char **argv)
{
	int n;
	char *p;
	
	if(actf == nil)
		goto err;
	n = strtol(argv[1], &p, 0);
	if(*p != 0 || n <= 0)
		goto err;
	actf->zoom = n;
	actf->tab->draw(actf);
	return;
err:
	cmdprint("?\n");
}

static void
dospr(int, char **argv)
{
	Win *w;
	Spr *s;
	Biobuf *bp;
	
	s = newspr(argv[1]);
	bp = Bopen(argv[1], OREAD);
	if(bp == nil){
		cmdprint("?%r\n");
		sprsize(s, 8, 8, 0);
	}else{
		if(readspr(s, bp) < 0){
			cmdprint("?%r\n");
			sprsize(s, 8, 8, 0);
		}
		Bterm(bp);
	}
	w = newwinsel(SPR, mc, s);
	if(w == nil){
		putfil(s);
		return;
	}
	if(s->palfile != nil){
		s->pal = findpal(argv[1], s->palfile, 1);
		if(s->pal == nil)
			cmdprint("?palette: %r\n");
		else{
			incref(s->pal);
			w->tab->draw(w);
		}
	}
}

static void
dowrite(int argc, char **argv)
{
	char *f;
	
	if(argc > 2)
		cmdprint("?\n");
	if(argc == 2)
		f = argv[1];
	else
		f = nil;
	if(actf == nil)
		cmdprint("?\n");
	winwrite(actf, f);
}

static void
doquit(int, char **)
{
	if(quit() < 0)
		threadexitsall(nil);
}

static struct cmd {
	char *name;
	int argc;
	void (*f)(int, char **);
} cmds[] = {
	{"pal", 2, dopal},
	{"size", 2, dosize},
	{"set", 2, doset},
	{"spr", 2, dospr},
	{"w", 0, dowrite},
	{"q", 1, doquit},
	{"zoom", 2, dozoom},
	{nil, nil}
};

void
docmd(char *s)
{
	char *t[32];
	int nt;
	struct cmd *c;

	nt = tokenize(s, t, nelem(t));
	if(nt == 0)
		return;
	for(c = cmds; c->name != 0; c++)
		if(strcmp(t[0], c->name) == 0){
			if(c->argc != 0 && c->argc != nt)
				cmdprint("?\n");
			else
				c->f(nt, t);
			return;
		}
	cmdprint("?\n");
}
