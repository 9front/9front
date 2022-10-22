#include <u.h>
#include <libc.h>
#include <fcall.h>
#define Extern
#include "exportfs.h"

#define QIDPATH	((1LL<<48)-1)
vlong newqid = 0;

void (*fcalls[])(Fsrpc*) =
{
	[Tversion]	Xversion,
	[Tauth]	Xauth,
	[Tflush]	Xflush,
	[Tattach]	Xattach,
	[Twalk]		Xwalk,
	[Topen]		slave,
	[Tcreate]	Xcreate,
	[Tclunk]	Xclunk,
	[Tread]		slave,
	[Twrite]	slave,
	[Tremove]	Xremove,
	[Tstat]		Xstat,
	[Twstat]	Xwstat,
};

/* accounting and debugging counters */
int	filecnt;
int	freecnt;
int	qidcnt;
int	qfreecnt;
int	ncollision;


/*
 * Start serving file requests from the network
 */
void
io(void)
{
	Fsrpc *r;
	int n;

	for(;;) {
		r = getsbuf();
		n = read9pmsg(0, r->buf, messagesize);
		if(n <= 0)
			fatal(nil);
		if(convM2S(r->buf, n, &r->work) != n)
			fatal("convM2S format error");

		DEBUG(2, "%F\n", &r->work);
		(fcalls[r->work.type])(r);
	}
}

void
reply(Fcall *r, Fcall *t, char *err)
{
	uchar *data;
	int n;

	t->tag = r->tag;
	t->fid = r->fid;
	if(err != nil) {
		t->type = Rerror;
		t->ename = err;
	}
	else 
		t->type = r->type + 1;

	DEBUG(2, "\t%F\n", t);

	data = malloc(messagesize);	/* not mallocz; no need to clear */
	if(data == nil)
		fatal(Enomem);
	n = convS2M(t, data, messagesize);
	if(write(1, data, n) != n){
		/* not fatal, might have got a note due to flush */
		fprint(2, "exportfs: short write in reply: %r\n");
	}
	free(data);
}

void
mounterror(char *err)
{
	Fsrpc *r;
	int n;

	r = getsbuf();
	r->work.tag = NOTAG;
	r->work.fid = NOFID;
	r->work.type = Rerror;
	r->work.ename = err;
	n = convS2M(&r->work, r->buf, messagesize);
	write(1, r->buf, n);
	exits(err);
}

Fid *
getfid(int nr)
{
	Fid *f;

	for(f = fidhash(nr); f != nil; f = f->next)
		if(f->nr == nr)
			return f;

	return nil;
}

int
freefid(int nr)
{
	Fid *f, **l;
	char buf[128];

	l = &fidhash(nr);
	for(f = *l; f != nil; f = f->next) {
		if(f->nr == nr) {
			if(f->mid != -1) {
				snprint(buf, sizeof(buf), "/mnt/exportfs/%d", f->mid);
				unmount(0, buf);
				psmap[f->mid] = 0;
			}
			if(f->f != nil) {
				freefile(f->f);
				f->f = nil;
			}
			if(f->dir != nil){
				free(f->dir);
				f->dir = nil;
			}
			*l = f->next;
			f->next = fidfree;
			fidfree = f;
			return 1;
		}
		l = &f->next;
	}

	return 0;	
}

Fid *
newfid(int nr)
{
	Fid *new, **l;
	int i;

	l = &fidhash(nr);
	for(new = *l; new != nil; new = new->next)
		if(new->nr == nr)
			return nil;

	if(fidfree == nil) {
		fidfree = emallocz(sizeof(Fid) * Fidchunk);

		for(i = 0; i < Fidchunk-1; i++)
			fidfree[i].next = &fidfree[i+1];

		fidfree[Fidchunk-1].next = nil;
	}

	new = fidfree;
	fidfree = new->next;

	memset(new, 0, sizeof(Fid));
	new->next = *l;
	*l = new;
	new->nr = nr;
	new->fid = -1;
	new->mid = -1;

	return new;	
}

static struct {
	Lock;
	Fsrpc	*free;

	/* statistics */
	int	nalloc;
	int	nfree;
}	sbufalloc;

Fsrpc *
getsbuf(void)
{
	Fsrpc *w;

	lock(&sbufalloc);
	w = sbufalloc.free;
	if(w != nil){
		sbufalloc.free = w->next;
		w->next = nil;
		sbufalloc.nfree--;
		unlock(&sbufalloc);
	} else {
		sbufalloc.nalloc++;
		unlock(&sbufalloc);
		w = emallocz(sizeof(*w) + messagesize);
	}
	w->flushtag = NOTAG;
	return w;
}

void
putsbuf(Fsrpc *w)
{
	w->flushtag = NOTAG;
	lock(&sbufalloc);
	w->next = sbufalloc.free;
	sbufalloc.free = w;
	sbufalloc.nfree++;
	unlock(&sbufalloc);
}

void
freefile(File *f)
{
	File *parent, *child;

	while(--f->ref == 0){
		freecnt++;
		DEBUG(2, "free %s\n", f->name);
		/* delete from parent */
		parent = f->parent;
		if(parent->child == f)
			parent->child = f->childlist;
		else{
			for(child = parent->child; child->childlist != f; child = child->childlist) {
				if(child->childlist == nil)
					fatal("bad child list");
			}
			child->childlist = f->childlist;
		}
		freeqid(f->qidt);
		free(f->name);
		free(f);
		f = parent;
	}
}

File *
file(File *parent, char *name)
{
	Dir *dir;
	char *path;
	File *f;

	DEBUG(2, "\tfile: 0x%p %s name %s\n", parent, parent->name, name);

	path = makepath(parent, name);
	if(patternfile != nil && excludefile(path)){
		free(path);
		return nil;
	}
	dir = dirstat(path);
	free(path);
	if(dir == nil)
		return nil;

	for(f = parent->child; f != nil; f = f->childlist)
		if(strcmp(name, f->name) == 0)
			break;

	if(f == nil){
		f = emallocz(sizeof(File));
		f->name = estrdup(name);

		f->parent = parent;
		f->childlist = parent->child;
		parent->child = f;
		parent->ref++;
		f->ref = 0;
		filecnt++;
	}
	f->ref++;
	f->qid.type = dir->qid.type;
	f->qid.vers = dir->qid.vers;
	f->qidt = uniqueqid(dir);
	f->qid.path = f->qidt->uniqpath;

	f->inval = 0;

	free(dir);

	return f;
}

void
initroot(void)
{
	Dir *dir;

	root = emallocz(sizeof(File));
	root->name = estrdup(".");

	dir = dirstat(root->name);
	if(dir == nil)
		fatal("root stat");

	root->ref = 1;
	root->qid.vers = dir->qid.vers;
	root->qidt = uniqueqid(dir);
	root->qid.path = root->qidt->uniqpath;
	root->qid.type = QTDIR;
	free(dir);

	psmpt = emallocz(sizeof(File));
	psmpt->name = estrdup("/");

	dir = dirstat(psmpt->name);
	if(dir == nil)
		return;

	psmpt->ref = 1;
	psmpt->qid.vers = dir->qid.vers;
	psmpt->qidt = uniqueqid(dir);
	psmpt->qid.path = psmpt->qidt->uniqpath;
	free(dir);

	psmpt = file(psmpt, "mnt");
	if(psmpt == nil)
		return;
	psmpt = file(psmpt, "exportfs");
}

char*
makepath(File *p, char *name)
{
	int i, n;
	char *c, *s, *path, *seg[256];

	seg[0] = name;
	n = strlen(name)+2;
	for(i = 1; i < 256 && p; i++, p = p->parent){
		seg[i] = p->name;
		n += strlen(p->name)+1;
	}
	path = emallocz(n);
	s = path;

	while(i--) {
		for(c = seg[i]; *c; c++)
			*s++ = *c;
		*s++ = '/';
	}
	while(s[-1] == '/')
		s--;
	*s = '\0';

	return path;
}

int
qidhash(vlong path)
{
	int h, n;

	h = 0;
	for(n=0; n<64; n+=Nqidbits){
		h ^= path;
		path >>= Nqidbits;
	}
	return h & (Nqidtab-1);
}

void
freeqid(Qidtab *q)
{
	ulong h;
	Qidtab *l;

	if(--q->ref)
		return;
	qfreecnt++;
	h = qidhash(q->path);
	if(qidtab[h] == q)
		qidtab[h] = q->next;
	else{
		for(l=qidtab[h]; l->next!=q; l=l->next)
			if(l->next == nil)
				fatal("bad qid list");
		l->next = q->next;
	}
	free(q);
}

Qidtab*
qidlookup(Dir *d)
{
	ulong h;
	Qidtab *q;

	h = qidhash(d->qid.path);
	for(q=qidtab[h]; q!=nil; q=q->next)
		if(q->type==d->type && q->dev==d->dev && q->path==d->qid.path)
			return q;
	return nil;
}

int
qidexists(vlong path)
{
	int h;
	Qidtab *q;

	for(h=0; h<Nqidtab; h++)
		for(q=qidtab[h]; q!=nil; q=q->next)
			if(q->uniqpath == path)
				return 1;
	return 0;
}

Qidtab*
uniqueqid(Dir *d)
{
	ulong h;
	vlong path;
	Qidtab *q;

	q = qidlookup(d);
	if(q != nil){
		q->ref++;
		return q;
	}
	path = d->qid.path;
	while(qidexists(path)){
		DEBUG(2, "collision on %s\n", d->name);
		/* collision: find a new one */
		ncollision++;
		path &= QIDPATH;
		++newqid;
		if(newqid >= (1<<16)){
			DEBUG(2, "collision wraparound\n");
			newqid = 1;
		}
		path |= newqid<<48;
		DEBUG(2, "assign qid %.16llux\n", path);
	}
	qidcnt++;
	q = emallocz(sizeof(Qidtab));
	q->ref = 1;
	q->type = d->type;
	q->dev = d->dev;
	q->path = d->qid.path;
	q->uniqpath = path;
	h = qidhash(d->qid.path);
	q->next = qidtab[h];
	qidtab[h] = q;
	return q;
}

void
fatal(char *s, ...)
{
	char buf[ERRMAX];
	va_list arg;
	Proc *m;

	if(s != nil) {
		va_start(arg, s);
		vsnprint(buf, ERRMAX, s, arg);
		va_end(arg);
	}

	/* Clear away the slave children */
	for(m = Proclist; m != nil; m = m->next)
		postnote(PNPROC, m->pid, "kill");

	if(s != nil) {
		DEBUG(2, "%s\n", buf);
		sysfatal("%s", buf);	/* caution: buf could contain '%' */
	} else
		exits(nil);
}

void*
emallocz(uint n)
{
	void *p;

	p = mallocz(n, 1);
	if(p == nil)
		fatal(Enomem);
	setmalloctag(p, getcallerpc(&n));
	return p;
}

char*
estrdup(char *s)
{
	char *t;

	t = strdup(s);
	if(t == nil)
		fatal(Enomem);
	setmalloctag(t, getcallerpc(&s));
	return t;
}
