#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>
#include <bio.h>

#include "dat.h"
#include "fns.h"

static void
showfid(int fd)
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

static void
showtree(int fd, int tid, char *name)
{
	Tree *t;
	Blk *b;
	int h;

	epochstart(tid);
	if(name == nil)
		name = "main";
	if(strcmp(name, "snap") == 0)
		t = &fs->snap;
	else if((t = opensnap(name, nil)) == nil){
		fprint(fd, "open %s: %r\n", name);
		epochend(tid);
		return;
	}
	b = getroot(t, &h);
	fprint(fd, "=== [%s] %B @%d\n", name, t->bp, t->ht);
	showblk(fd, b, "contents", 1);
	dropblk(b);
	if(t != &fs->snap)
		closesnap(t);
	epochend(tid);
}

static void
showtrace(int fd, char *name)
{
	Biobuf *bfd;
	Trace *t;
	long ti;
	int i;

	if(name != nil)
		bfd = Bopen(name, OWRITE);
	else
		bfd = Bfdopen(fd, OWRITE);
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
showfree(int fd)
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

void
runcons(int tid, void *pfd)
{
	char buf[256], *f[4];
	int n, nf, fd;
	Amsg *a;

	fd = (uintptr)pfd;
	while(1){
		fprint(fd, "gefs# ");
		if((n = read(fd, buf, sizeof(buf)-1)) == -1)
			break;
		buf[n] = 0;
		memset(f, 0, sizeof(f));
		nf = tokenize(buf, f, nelem(f));
		if(nf == 0)
			continue;
		if(strcmp(f[0], "help") == 0){
			fprint(fd, "help -- show this message\n");
			fprint(fd, "halt -- halt the fs\n");
			fprint(fd, "check -- check fs consistency\n");
			fprint(fd, "show -- show debug info\n");
			fprint(fd, "\tfree -- free blocks\n");
			fprint(fd, "\ttrace -- trace of recent operations\n");
			fprint(fd,"\ttree [name] -- dump tree 'name'\n");
		}else if(strcmp(f[0], "check") == 0){
			qlock(&fs->mutlk);
			checkfs(fd);
			qunlock(&fs->mutlk);
		}else if(strcmp(f[0], "halt") == 0){
			a = emalloc(sizeof(Amsg), 1);
			a->op = AOhalt;
			chsend(fs->admchan, a);		
			fprint(fd, "gefs: ending...\n");
		}else if(nf > 1 && strcmp(f[0], "show") == 0){
			if(strcmp(f[1], "fid") == 0)
				showfid(fd);
			else if(strcmp(f[1], "free") == 0)
				showfree(fd);
			else if(nf <= 3 && strcmp(f[1], "trace") == 0)
				showtrace(fd, f[2]);
			else if(nf <= 3 && strcmp(f[1], "tree") == 0)
				showtree(fd, tid, f[2]);
			else
				fprint(fd, "garbled show '%s'\n", f[1]);
		}else{
			fprint(fd, "garbled command\n");
		}
	}
}
