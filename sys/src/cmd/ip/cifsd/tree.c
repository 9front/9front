#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static void*
getid(void **a, int n, int i)
{
	void *p;
	if(i <= 0 || i > n || (p = a[i-1]) == nil)
		return nil;
	return p;
}

static void
setid(void **a, int n, int i, void *p)
{
	assert(i > 0 || i <= n);
	a[i-1] = p;
}

static int
newid(void ***pa, int *pn, void *p)
{
	int i;
	for(i=0; i < *pn; i++)
		if((*pa)[i] == nil)
			break;
	if(i == *pn){
		(*pn)++;
		if((i % 8) == 0)
			*pa = realloc(*pa, (i + 8) * sizeof(p));
	}
	(*pa)[i] = p;
	return i+1;
}

static void **tree;
static int ntree;

static void
freetree(Tree *t)
{
	int i;

	if(t == nil)
		return;
	for(i = 0; i < t->nfile; i++)
		putfile(t->file[i]);
	for(i = 0; i < t->nfind; i++)
		putfile(t->find[i]);
	free(t->file);
	free(t->find);
	free(t);
}

Tree*
connecttree(char *service, char *path, int *perr)
{
	Share *s;
	Tree *t;
	int err;

	t = nil;
	if((s = mapshare(path)) == nil){
		err = STATUS_BAD_NETWORK_NAME;
		goto out;
	}
	if(strcmp(service, "?????") && cistrcmp(service, s->service)){
		err = STATUS_BAD_DEVICE_TYPE;
		goto out;
	}
	t = mallocz(sizeof(*t), 1);
	t->share = s;
	t->tid = newid(&tree, &ntree, t);
	err = 0;
out:
	if(perr)
		*perr = err;
	return t;
}

int
disconnecttree(int tid)
{
	Tree *t;

	if((t = gettree(tid)) == nil)
		return STATUS_SMB_BAD_TID;
	setid(tree, ntree, tid, nil);
	freetree(t);
	return 0;
}

void
logoff(void)
{
	int i;

	for(i=0; i<ntree; i++)
		freetree(tree[i]);
	free(tree);
	tree = nil;
	ntree = 0;
}

Tree*
gettree(int tid)
{
	Tree *t;

	if(t = getid(tree, ntree, tid))
		if(debug)
			fprint(2, "tree [%.4x] %s\n", tid, t->share->root);
	return t;
}

int
newfid(Tree *t, File *f)
{
	f->ref++;
	return newid(&t->file, &t->nfile, f);
}

void
delfid(Tree *t, int fid)
{
	File *f;

	if(f = getid(t->file, t->nfile, fid)){
		setid(t->file, t->nfile, fid, nil);
		putfile(f);
	}
}

File*
getfile(int tid, int fid, Tree **ptree, int *perr)
{
	Tree *t;
	File *f;
	int err;

	f = nil;
	if((t = gettree(tid)) == nil){
		err = STATUS_SMB_BAD_TID;
		goto out;
	}
	if((f = getid(t->file, t->nfile, fid)) == nil){
		err = STATUS_SMB_BAD_FID;
		goto out;
	}
	f->ref++;
	err = 0;
	if(debug)
		fprint(2, "file [%x] %s\n", fid, f->path);
out:
	if(perr)
		*perr = err;
	if(ptree)
		*ptree = t;
	return f;
}

char*
getpath(int tid, char *name, Tree **ptree, int *perr)
{
	Tree *t;
	char *p;
	int err;

	if(t = gettree(tid)){
		err = 0;
		p = conspath(t->share->root, name);
		if(debug)
			fprint(2, "path %s\n", p);
	} else {
		err = STATUS_SMB_BAD_TID;
		p = nil;
	}
	if(perr)
		*perr = err;
	if(ptree)
		*ptree = t;
	return p;
}

int
newsid(Tree *t, Find *f)
{
	f->ref++;
	return newid(&t->find, &t->nfind, f);
}

void
delsid(Tree *t, int sid)
{
	Find *f;

	if(f = getid(t->find, t->nfind, sid)){
		setid(t->find, t->nfind, sid, nil);
		putfind(f);
	}
}

Find*
getfind(int tid, int sid, Tree **ptree, int *perr)
{
	Tree *t;
	Find *f;
	int err;

	f = nil;
	if((t = gettree(tid)) == nil){
		err = STATUS_SMB_BAD_TID;
		goto out;
	}
	if((f = getid(t->find, t->nfind, sid)) == nil){
		err = STATUS_SMB_BAD_FID;
		goto out;
	}
	f->ref++;
	err = 0;
	if(debug)
		fprint(2, "find [%x] %s %s\n", sid, f->base, f->pattern);
out:
	if(perr)
		*perr = err;
	if(ptree)
		*ptree = t;
	return f;
}


