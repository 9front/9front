#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>
#include <bio.h>

#include "dat.h"
#include "fns.h"


static void
fmtdf(Fmt *fmt)
{
	char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", nil};
	vlong size, used;
	double hsize, hused;
	Arena *a;
	int i, us, uu;

	size = 0;
	used = 0;
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		qlock(a);
		size += a->size;
		used += a->used;
		qunlock(a);
	}
	hsize = size;
	hused = used;
	for(us = 0; us < nelem(units)-1 && hsize >= 500 ; us++)
		hsize /= 1024;
	for(uu = 0; uu < nelem(units)-1 && hused >= 500 ; uu++)
		hused /= 1024;
	fmtprint(fmt, "usage \t%.2f%%\t%.2f%s\t%.2f%s\n",
		100.0*used/size,
		hused, units[uu],
		hsize, units[us]);
}

static void
showconf(Fmt *fmt, Tree *t, char *name)
{
	char pfx[1];
	Scan s;

	pfx[0] = Kconf;
	btnewscan(&s, pfx, 1);
	btenter(t, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		fmtprint(fmt, "conf %q %.*q %.*q\n",
			name, (int)s.kv.nk-1, s.kv.k+1, (int)s.kv.nv, s.kv.v);
	}
	btexit(&s);
}

static void
iterconf(Fmt *fmt, int flg, char *name)
{
	Mount *mnt;
	Tree *t;

	if((flg & Lmut) && !waserror()){
		mnt = getmount(name);
		t = agetp(&mnt->root);
		showconf(fmt, t, name);
		clunkmount(mnt);
		poperror();
	}
}

static void
showsnap(Fmt *fmt, int flg, char *name)
{
	if(flg & Lmut)
		fmtprint(fmt, "fork %q\n", name);
	else
		fmtprint(fmt, "snap %q\n", name);
}

static void
itersnaps(Fmt *fmt, void(*show)(Fmt*, int, char*))
{
	char pfx[Snapsz], name[Keymax];
	Scan s;
	uint flg;
	int sz;

	pfx[0] = Klabel;
	sz = 1;
	btnewscan(&s, pfx, sz);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		flg = UNPACK32(s.kv.v+1+8);
		memcpy(name, s.kv.k+1, s.kv.nk-1);
		name[s.kv.nk-1] = 0;
		show(fmt, flg, name);
	}
	btexit(&s);
}

void
readstatus(Fmsg *m, Fid *f, Fcall *r)
{
	Fmt fmt;
	int n;

	if(f->aux == nil || m->offset == 0){
		free(f->aux);
		f->aux = nil;
		if((fmtstrinit(&fmt)) == -1)
			error("%r");
		if(waserror()){
			free(fmtstrflush(&fmt));
			nexterror();
		}
		fmtprint(&fmt, "gefs p9\n");
		fmtdf(&fmt);
		showconf(&fmt, &fs->snap, "");
		itersnaps(&fmt, iterconf);
		itersnaps(&fmt, showsnap);
		f->aux = fmtstrflush(&fmt);
		poperror();
	}
	if(f->aux == nil)
		error(Efs);

	n = strlen(f->aux);
	if(m->offset >= n)
		r->count = 0;
	else if(m->offset + m->count > n)
		r->count = n - m->offset;
	else
		r->count = m->count;
	memcpy(r->data, (char*)f->aux+m->offset, r->count);
}

static void
asend(int op, Fmsg *m)
{
	Amsg *a;

	a = emalloc(sizeof(Amsg), 1);
	a->op = op;
	a->m = m;
	chsend(fs->admchan, a);
}

static void
setconf(char *snap, char *k, char *v, Fmsg *m)
{
	Amsg *a;

	a = emalloc(sizeof(Amsg), 1);
	a->op = AOsetcfg;
	a->m = m;
	if(snap != nil)
		strecpy(a->snap, a->snap+sizeof(a->snap), snap);
	strecpy(a->key, a->key+sizeof(a->key), k);
	strecpy(a->val, a->val+sizeof(a->val), v);
	chsend(fs->admchan, a);
}

static void
clrsnap(char *tag, Fmsg *m)
{
	Amsg *a;

	a = emalloc(sizeof(Amsg), 1);
	a->op = AOsnap;
	a->delete = 1;
	a->m = m;
	strecpy(a->old, a->old+sizeof(a->old), tag);
	chsend(fs->admchan, a);
}

static void
takesnap(char *old, char *new, int flag, Fmsg *m)
{
	Amsg *a;

	a = emalloc(sizeof(Amsg), 1);
	a->op = AOsnap;
	a->delete = 0;
	a->flag = flag;
	a->m = m;
	strecpy(a->old, a->old+sizeof(a->old), old);
	strecpy(a->new, a->new+sizeof(a->new), new);
	chsend(fs->admchan, a);
}

static void
clrconf(char *snap, char *k, Fmsg *m)
{
	Amsg *a;

	a = emalloc(sizeof(Amsg), 1);
	a->op = AOclrcfg;
	a->m = m;
	if(snap != nil)
		strecpy(a->snap, a->snap+sizeof(a->snap), snap);
	strecpy(a->key, a->key+sizeof(a->key), k);
	chsend(fs->admchan, a);
}

static void
refreshusers(void)
{
	Mount *mnt;

	mnt = getmount("adm");
	if(waserror()){
		clunkmount(mnt);
		nexterror();
	}
	loadusers(agetp(&mnt->root));
	clunkmount(mnt);
	poperror();
}

void
writectl(Fmsg *m, Fid *f)
{
	char buf[256], *sp[8];
	Fcall r;
	int nsp;

	USED(f);
	if(m->offset != 0)
		error(Ebotch);
	if(m->count >= 256)
		error(Elength);
	memcpy(buf, m->data, m->count);
	buf[m->count] = 0;
	nsp = tokenize(buf, sp, nelem(sp));

	r.type = Rwrite;
	r.count = m->count;
	switch(nsp){
	default:
		error(Ebadctl);
		break;
	case 1:
		if(strcmp(sp[0], "sync") == 0)
			asend(AOsync, m);
		else if(strcmp(sp[0], "halt") == 0){
			aincl(&fs->rdonly, 1);
			respond(m, &r);
			asend(AOhalt, nil);
		}else if(strcmp(sp[0], "check") == 0){
			checkfs(2);
			respond(m, &r);
		}else if(strcmp(sp[0], "users") == 0){
			refreshusers();
			respond(m, &r);
		}else
			error(Ebadctl);
		break;
	case 2:
		if(strcmp(sp[0], "clr") == 0)
			clrconf(nil, sp[1], m);
		else if(strcmp(sp[0], "delsnap") == 0)
			clrsnap(sp[1], m);
		else if(strcmp(sp[0], "reserve") == 0){
			usereserve = atoi(sp[1]);
			respond(m, &r);
		}else if(strcmp(sp[0], "debug") == 0){
			debug = atoi(sp[1]);
			respond(m, &r);
		}else
			error(Ebadctl);
		break;
	case 3:
		if(strcmp(sp[0], "set") == 0)
			setconf(nil, sp[1], sp[2], m);
		else if(strcmp(sp[0], "clr") == 0)
			clrconf(sp[1], sp[2], m);
		else if(strcmp(sp[0], "snap") == 0)
			takesnap(sp[1], sp[2], 0, m);
		else if(strcmp(sp[0], "fork") == 0)
			takesnap(sp[1], sp[2], Lmut, m);
		else
			error(Ebadctl);
		break;
	case 4:
		if(strcmp(sp[0], "set") == 0)
			setconf(sp[1], sp[2], sp[3], m);
		else
			error(Ebadctl);
		break;
	}
}
