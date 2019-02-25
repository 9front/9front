#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static char*
append(char **p, char *s)
{
	int n;
	char *o;

	if(s == nil)
		return nil;
	n = strlen(s)+1;
	memmove(o = *p, s, n);
	*p += n;
	return o;
}

static Dir*
xdirdup(Dir *d, int n)
{
	char *p;
	Dir *o;
	int i;

	p = nil;
	for(i=0; i<n; i++){
		p += strlen(d[i].name)+1;
		if(d[i].uid) p += strlen(d[i].uid)+1;
		if(d[i].gid) p += strlen(d[i].gid)+1;
		if(d[i].muid) p += strlen(d[i].muid)+1;
	}
	o = malloc(n*sizeof(*d) + (uintptr)p);
	memmove(o, d, n*sizeof(*d));
	p = (char*)&o[n];
	for(i=0; i<n; i++){
		o[i].name = append(&p, d[i].name);
		o[i].uid = append(&p, d[i].uid);
		o[i].gid = append(&p, d[i].gid);
		o[i].muid = append(&p, d[i].muid);
	}
	return o;
}

static int xdirread0(char **path, int (*namecmp)(char *, char *), Dir **d);

int
xdirread(char **path, int (*namecmp)(char *, char *), Dir **d)
{
	Dir *t;
	int n;

	if((n = xdirread0(path, namecmp, &t)) > 0)
		*d =  xdirdup(t, n);
	else
		*d = nil;
	return n;
}

static Dir*
xdirstat0(char **path, int (*namecmp)(char *, char *), char *err)
{
	char *base, *name;
	Dir *d, *t;
	int n, i;

	if(d = dirstat(*path)){
		if(d->name[0] == 0)
			d->name = "/";
		return d;
	}
	if(!splitpath(*path, &base, &name))
		return nil;
	if((n = xdirread0(&base, namecmp, &t)) < 0)
		goto out;
	for(i=0; i<n; i++){
		if(namecmp(t[i].name, name))
			continue;
		free(*path); *path = conspath(base, t[i].name);
		d = xdirdup(&t[i], 1);
		goto out;
	}
	werrstr("%s", err);
out:
	free(base);
	free(name);
	return d;
}

Dir*
xdirstat(char **path, int (*namecmp)(char *, char *))
{
	return xdirstat0(path, namecmp, "name not found");
}

typedef struct XDir XDir;
struct XDir
{
	Qid	qid;
	char	*path;
	int	ndir;
	Dir	*dir;
	XDir	*next;
};

static void
freexdir(XDir *x)
{
	free(x->path);
	free(x->dir);
	free(x);
}

static int
qidcmp(Qid *q1, Qid *q2)
{
	return (q1->type != q2->type) || (q1->path != q2->path) || (q1->vers != q2->vers);
}

static XDir *xdirlist;
static int xdircount;

static int
xdirread0(char **path, int (*namecmp)(char *, char *), Dir **d)
{
	XDir *x, *p;
	int fd, n;
	Dir *t;

	t = nil;
	for(p = nil, x = xdirlist; x; p=x, x=x->next){
		if(namecmp(x->path, *path))
			continue;
		if(x == xdirlist)
			xdirlist = x->next;
		else
			p->next = x->next;
		while(t = dirstat(x->path)){
			if(qidcmp(&t->qid, &x->qid))
				break;
			free(t);
			x->next = xdirlist;
			xdirlist = x;
			if(strcmp(x->path, *path)){
				free(*path);
				*path = strdup(x->path);
			}
			if(d) *d = x->dir;
			return x->ndir;
		}
		xdircount--;
		freexdir(x);
		break;
	}
	if((fd = open(*path, OREAD)) < 0){
		free(t);
		if(t = xdirstat0(path, namecmp, "directory entry not found"))
			fd = open(*path, OREAD);
	} else if(t == nil)
		t = dirfstat(fd);

	n = -1;
	if(fd < 0 || t == nil)
		goto out;
	if((t->qid.type & QTDIR) == 0){
		werrstr("not a directory");
		goto out;
	}
	if((n = dirreadall(fd, d)) < 0)
		goto out;

	if(xdircount >= 8){
		xdircount--;
		for(p = xdirlist, x = xdirlist->next; x->next; p = x, x = x->next)
			;
		p->next = nil;
		freexdir(x);
	}

	x = mallocz(sizeof(*x), 1);
	x->qid = t->qid;
	x->path = strdup(*path);
	x->ndir = n;
	x->dir = *d;

	x->next = xdirlist;
	xdirlist = x;
	xdircount++;

out:
	if(fd >= 0)
		close(fd);
	free(t);
	return n;
}

void
xdirflush(char *path, int (*namecmp)(char *, char *))
{
	XDir **pp, **xx, *x;
	char *d, *s;
	int n;

	n = strlen(path);
	if(s = strrchr(path, '/'))
		n = s - path;
	d = smprint("%.*s", utfnlen(path, n), path);
	s = malloc(++n);
	for(pp = &xdirlist; x = *pp; pp = xx){
		xx = &x->next;
		snprint(s, n, "%s", x->path);
		if(namecmp(d, s) == 0){
			*pp = *xx; xx = pp;
			xdircount--;
			freexdir(x);
		}
	}
	free(s);
	free(d);
}
