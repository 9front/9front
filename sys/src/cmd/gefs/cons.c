#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>
#include <bio.h>

#include "dat.h"
#include "fns.h"

typedef struct Cmd	Cmd;

struct Cmd {
	char	*name;
	char	*sub;
	int	minarg;
	int	maxarg;
	int	epoch;
	void	(*fn)(int, char**, int);
};

static void
setdbg(int fd, char **ap, int na)
{
	debug = (na == 1) ? atoi(ap[0]) : !debug;
	fprint(fd, "debug → %d\n", debug);
}

static void
asend(int fd, int op)
{
	Amsg *a;

	a = emalloc(sizeof(Amsg), 1);
	a->op = op;
	a->fd = fd;
	chsend(fs->admchan, a);		
}

static void
syncfs(int fd, char **, int)
{
	asend(fd, AOsync);
	fprint(fd, "synced\n");
}

static void
haltfs(int fd, char **, int)
{
	asend(fd, AOhalt);
	fprint(fd, "gefs: ending...\n");
}

static void
listsnap(int fd)
{
	char pfx[Snapsz];
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
		fprint(fd, "snap %.*s", s.kv.nk-1, s.kv.k+1);
		if(flg & Lmut)
			fprint(fd, " [mutable]");
		fprint(fd, "\n");
	}
	btexit(&s);
}

static void
snapfs(int fd, char **ap, int na)
{
	Amsg *a;
	int i;

	a = emalloc(sizeof(Amsg), 1);
	a->op = AOsnap;
	a->fd = fd;
	while(ap[0][0] == '-'){
		for(i = 1; ap[0][i]; i++){
			switch(ap[0][i]){
			case 'm':	a->flag |= Lmut;	break;
			case 'd':	a->delete++;		break;
			case 'l':
				listsnap(fd);
				free(a);
				return;
			default:
				fprint(fd, "usage: snap -[Smdl] [old [new]]\n");
				free(a);
				return;
			}
		}
		na--;
		ap++;
	}
	if(a->delete && na != 1 || !a->delete && na != 2){
		fprint(fd, "usage: snap -[md] old [new]\n");
		free(a);
		return;
	}
	if(na >= 1)
		strecpy(a->old, a->old+sizeof(a->old), ap[0]);
	if(na >= 2)
		strecpy(a->new, a->new+sizeof(a->new), ap[1]);
	asend(fd, AOsync);
	chsend(fs->admchan, a);
}

static void
fsckfs(int fd, char**, int)
{
	if(checkfs(fd))
		fprint(fd, "ok\n");
	else
		fprint(fd, "broken\n");
}

static void
refreshusers(int fd, char **, int)
{
	Mount *mnt;

	if((mnt = getmount("adm")) == nil){
		fprint(fd, "load users: missing 'adm'\n");
		return;
	}
	if(waserror()){
		fprint(fd, "load users: %s\n", errmsg());
		clunkmount(mnt);
		return;
	}
	loadusers(fd, agetp(&mnt->root));
	fprint(fd, "refreshed users\n");
	clunkmount(mnt);
	poperror();
}

static void
showbstate(int fd, char**, int)
{
	char *p, fbuf[8];
	Blk *b;
	int f;

	for(b = blkbuf; b != blkbuf+fs->cmax; b++){
		p = fbuf;
		f = agetl(&b->flag);
		if(f & Bdirty)	*p++ = 'd';
		if(f & Bfinal)	*p++ = 'f';
		if(f & Bfreed)	*p++ = 'F';
		if(f & Bcached)	*p++ = 'c';
		if(f & Bqueued)	*p++ = 'q';
		if(f & Blimbo)	*p++ = 'L';
		*p = 0;
		fprint(fd, "blk %#p type %d flag %s bp %B ref %ld alloc %#p queued %#p, hold %#p drop %#p cached %#p\n",
			b, b->type, fbuf, b->bp, agetl(&b->ref), b->alloced, b->queued, b->lasthold, b->lastdrop, b->cached);
	}
}

static void
showusers(int fd, char**, int)
{
	User *u, *v;
	int i, j;
	char *sep;

	rlock(&fs->userlk);
	for(i = 0; i < fs->nusers; i++){
		u = &fs->users[i];
		fprint(fd, "%d:%s:", u->id, u->name);
		if(u->lead == noneid)
			fprint(fd, ":");
		else if((v = uid2user(u->lead)) == nil)
			fprint(fd, "???:");
		else
			fprint(fd, "%s:", v->name);
		sep = "";
		for(j = 0; j < u->nmemb; j++){
			if((v = uid2user(u->memb[j])) == nil)
				fprint(fd, "%s???", sep);
			else
				fprint(fd, "%s%s", sep, v->name);
			sep = ",";
		}
		fprint(fd, "\n");
	}
	runlock(&fs->userlk);
}

static void
showdf(int fd, char**, int)
{
	char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", nil};
	vlong size, used, free;
	double hsize, hused, hfree;
	double pct;
	Arena *a;
	int i, us, uu, uf;

	size = 0;
	used = 0;
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		qlock(a);
		size += a->size;
		used += a->used;
		qunlock(a);
		fprint(fd, "arena %d: %llx/%llx (%.2f%%)\n", i, a->used, a->size, 100*(double)a->used/(double)a->size);
	}
	free = size - used;
	hsize = size;
	hused = used;
	hfree = free;
	for(us = 0; us < nelem(units)-1 && hsize >= 500 ; us++)
		hsize /= 1024;
	for(uu = 0; uu < nelem(units)-1 && hused >= 500 ; uu++)
		hused /= 1024;
	for(uf = 0; uf < nelem(units)-1 && hfree >= 500 ; uf++)
		hfree /= 1024;
	pct = 100.0*(double)used/(double)size;
	fprint(fd, "fill:\t%.2f%%\n", pct);
	fprint(fd, "used:\t%lld (%.2f %s)\n", used, hused, units[uu]);
	fprint(fd, "size:\t%lld (%.2f %s)\n", size, hsize, units[us]);
	fprint(fd, "free:\t%lld (%.2f %s)\n", free, hfree, units[uf]);
}

void
showfid(int fd, char**, int)
{
	int i;
	Fid *f;
	Conn *c;

	for(c = fs->conns; c != nil; c = c->next){
		fprint(fd, "-- conn %p: fids --\n", c);
		for(i = 0; i < Nfidtab; i++){
			lock(&c->fidtablk[i]);
			for(f = c->fidtab[i]; f != nil; f = f->next){
				rlock(f->dent);
				fprint(fd, "\tfid[%d] from %#zx: %d [refs=%ld, k=%K, qid=%Q m=%d, dmode:%d duid: %d, dgid: %d]\n",
					i, getmalloctag(f), f->fid,
					agetl(&f->dent->ref), &f->dent->Key, f->dent->qid,
					f->mode, f->dmode, f->duid, f->dgid);
				runlock(f->dent);
			}
			unlock(&c->fidtablk[i]);
		}
	}
}

void
showtree(int fd, char **ap, int na)
{
	char *name;
	Tree *t;
	Blk *b;
	int h;

	name = "main";
	memset(&t, 0, sizeof(t));
	if(na == 1)
		name = ap[0];
	if(strcmp(name, "snap") == 0)
		t = &fs->snap;
	else if((t = opensnap(name, nil)) == nil){
		fprint(fd, "open %s: %r\n", name);
		return;
	}
	b = getroot(t, &h);
	fprint(fd, "=== [%s] %B @%d\n", name, t->bp, t->ht);
	showblk(fd, b, "contents", 1);
	dropblk(b);
	if(t != &fs->snap)
		closesnap(t);
}

static void
permflip(int fd, char **ap, int)
{
	if(strcmp(ap[0], "on") == 0)
		permissive = 1;
	else if(strcmp(ap[0], "off") == 0)
		permissive = 0;
	else
		fprint(2, "unknown permissive %s\n", ap[0]);
	fprint(fd, "permissive: %d → %d\n", !permissive, permissive);
}

static void
savetrace(int fd, char **ap, int na)
{
	Biobuf *bfd;
	Trace *t;
	long ti;
	int i;

	if(na == 0)
		bfd = Bfdopen(dup(fd, -1), OWRITE);
	else
		bfd = Bopen(ap[0], OWRITE);
	if(bfd == nil){
		fprint(fd, "error opening output");
		return;
	}
	for(i = 0; i < fs->ntrace; i++){
		ti = agetl(&fs->traceidx);
		t = &fs->trace[(ti+ i) % fs->ntrace];
		if(t->msg[0] == 0)
			continue;
		Bprint(bfd, "[%d@%d] %s", t->tid, t->qgen, t->msg);
		if(t->bp.addr != -1)
			Bprint(bfd, " %B", t->bp);
		if(t->v0 != -1)
			Bprint(bfd, " %llx", t->v0);
		if(t->v1 != -1)
			Bprint(bfd, " %llx", t->v1);
		Bprint(bfd, "\n");
	}
	Bterm(bfd);
	fprint(fd, "saved\n");
}

static void
showfree(int fd, char **, int)
{
	Arange *r;
	Arena *a;
	int i;

	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		qlock(a);
		fprint(fd, "arena %d %llx+%llx{\n", i, a->h0->bp.addr, a->size);
		for(r = (Arange*)avlmin(a->free); r != nil; r = (Arange*)avlnext(r))
			fprint(fd, "\t%llx..%llx (%llx)\n", r->off, r->off+r->len, r->len);
		fprint(fd, "}\n");
		qunlock(a);
	}
}

static void
unreserve(int fd, char **ap, int)
{
	if(strcmp(ap[0], "on") == 0)
		usereserve = 0;
	else if(strcmp(ap[0], "off") == 0)
		usereserve = 1;
	else
		fprint(2, "unknown reserve %s\n", ap[0]);
	fprint(fd, "reserve: %d → %d\n", !permissive, permissive);
}

static void
setcfg(int fd, char **ap, int n)
{
	Amsg *a;

	a = emalloc(sizeof(Amsg), 1);
	a->op = AOsetcfg;
	a->fd = fd;
	if(n == 3)
		strecpy(a->snap, a->snap+sizeof(a->snap), *ap++);
	strecpy(a->key, a->key+sizeof(a->key), *ap++);
	strecpy(a->val, a->val+sizeof(a->val), *ap);
	chsend(fs->admchan, a);
}

static void
clrcfg(int fd, char **ap, int n)
{
	Amsg *a;

	a = emalloc(sizeof(Amsg), 1);
	a->op = AOclrcfg;
	a->fd = fd;
	if(n == 2)
		strecpy(a->snap, a->snap+sizeof(a->snap), *ap++);
	strecpy(a->key, a->key+sizeof(a->key), *ap);
	chsend(fs->admchan, a);
}

static void
help(int fd, char**, int)
{
	char *msg =
		"help -- show this help\n"
		"check -- check for consistency\n"
		"set [snap] key val -- set configuration key\n"
		"clear [snap] key -- clear configuration key\n"
		"df -- show disk usage\n"
		"halt -- stop all writers, sync, and go read-only\n"
		"permit [on|off] -- switch to/from permissive mode\n"
		"reserve [on|off] -- enable block reserves\n"
		"snap -[Smdl] [old [new]] -- manage snapshots\n"
		"sync -- flush all pending writes to disk\n"
		"users -- reload user table from adm snapshot\n"
		"save trace [name] -- save a trace of recent activity\n"
		"show -- debug dumps\n"
		"	tree [name]\n"
		"	fid\n"
		"	users\n";
	fprint(fd, "%s", msg);
}

Cmd cmdtab[] = {
	/* admin */
	{.name="check",		.sub=nil,	.minarg=0, .maxarg=0, .fn=fsckfs, .epoch=1},
	{.name="set",		.sub=nil,	.minarg=2, .maxarg=3, .fn=setcfg},
	{.name="clear",		.sub=nil,	.minarg=1, .maxarg=2, .fn=clrcfg},
	{.name="df",		.sub=nil, 	.minarg=0, .maxarg=0, .fn=showdf},
	{.name="halt",		.sub=nil,	.minarg=0, .maxarg=0, .fn=haltfs},
	{.name="help",		.sub=nil,	.minarg=0, .maxarg=0, .fn=help},
	{.name="permit",	.sub=nil,	.minarg=1, .maxarg=1, .fn=permflip},
	{.name="snap",		.sub=nil,	.minarg=1, .maxarg=3, .fn=snapfs, .epoch=1},
	{.name="sync",		.sub=nil,	.minarg=0, .maxarg=0, .fn=syncfs},
	{.name="reserve",	.sub=nil,	.minarg=0, .maxarg=1, .fn=unreserve},
	{.name="users",		.sub=nil,	.minarg=0, .maxarg=1, .fn=refreshusers, .epoch=1},

	/* debugging */
	{.name="show",		.sub="fid",	.minarg=0, .maxarg=0, .fn=showfid},
	{.name="show",		.sub="tree",	.minarg=0, .maxarg=1, .fn=showtree, .epoch=1},
	{.name="show",		.sub="users",	.minarg=0, .maxarg=0, .fn=showusers},
	{.name="show",		.sub="bstate",	.minarg=0, .maxarg=0, .fn=showbstate, .epoch=1},
	{.name="show",		.sub="free",	.minarg=0, .maxarg=0, .fn=showfree},
	{.name="debug",		.sub=nil,	.minarg=0, .maxarg=1, .fn=setdbg},
	{.name="save",		.sub="trace",	.minarg=0, .maxarg=1, .fn=savetrace},
	{.name=nil, .sub=nil},
};

void
runcons(int tid, void *pfd)
{
	char buf[256], *f[4], **ap;
	int i, n, nf, na, fd;
	Cmd *c;

	fd = (uintptr)pfd;
	while(1){
		fprint(fd, "gefs# ");
		if((n = read(fd, buf, sizeof(buf)-1)) == -1)
			break;
		buf[n] = 0;
		nf = tokenize(buf, f, nelem(f));
		if(nf == 0 || strlen(f[0]) == 0)
			continue;
		for(c = cmdtab; c->name != nil; c++){
			ap = f;
			na = nf;
			if(strcmp(c->name, *ap) != 0)
				continue;
			ap++;
			na--;
			if(c->sub != nil){
				if(na == 0 || strcmp(c->sub, *ap) != 0)
					continue;
				ap++;
				na--;
			}
			if(na < c->minarg || na > c->maxarg)
				continue;
			if(c->epoch)
				epochstart(tid);
			if(!waserror()){
				c->fn(fd, ap, na);
				poperror();
			}else
				fprint(fd, "%s: %s\n", f[0], errmsg());
			if(c->epoch)
				epochend(tid);
			break;
		}
		if(c->name == nil){
			fprint(fd, "unknown command '%s", f[0]);
			for(i = 1; i < nf; i++)
				fprint(fd, " %s", f[i]);
			fprint(fd, "'\n");
		}
	}
}
