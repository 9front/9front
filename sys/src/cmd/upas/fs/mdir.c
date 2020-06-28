#include "common.h"
#include "dat.h"

typedef struct {
	int	debug;
} Mdir;

#define	mdprint(mdir, ...)	if(mdir->debug) fprint(2, __VA_ARGS__)

static int
slurp(char *f, char *b, uvlong o, long l)
{
	int fd, r;

	if((fd = open(f, OREAD)) == -1)
		return -1;
	if(seek(fd, o, 0) == o)
		r = readn(fd, b, l);
	else
		r = 0;
	close(fd);
	return r != l ? -1: 0;
}

static int
mdirfetch(Mailbox *mb, Message *m, uvlong o, ulong l)
{
	char buf[Pathlen];
	Mdir *mdir;

	mdir = mb->aux;
	mdprint(mdir, "mdirfetch(%D) ...", m->fileid);

	snprint(buf, sizeof buf, "%s/%D", mb->path, m->fileid);
	if(slurp(buf, m->start + o, o, l) == -1){
		logmsg(m, "fetch failed: %r");
		mdprint(mdir, "%r\n");
		return -1;
	}
	mdprint(mdir, "fetched [%llud, %llud]\n", o, o + l);
	return 0;
}

/* must be [0-9]+(\..*)? */
int
dirskip(Dir *a, uvlong *uv)
{
	char *p;

	if(a->length == 0 || (a->qid.type & QTDIR) != 0)
		return 1;
	*uv = strtoul(a->name, &p, 0);
	if(*uv < 1000000 || *p != '.')
		return 1;
	*uv = *uv<<8 | strtoul(p + 1, &p, 10) & 0xFF;
	if(*p)
		return 1;
	return 0;
}

static int
vcmp(vlong a, vlong b)
{
	a -= b;
	if(a > 0)
		return 1;
	if(a < 0)
		return -1;
	return 0;
}

static int
dircmp(Dir *a, Dir *b)
{
	uvlong x, y;

	if(dirskip(a, &x))
		x = 0;
	if(dirskip(b, &y))
		y = 0;
	return vcmp(x, y);
}

static char*
mdirread(Mdir* mdir, Mailbox* mb)
{
	int i, fd, n, c;
	uvlong uv;
	Dir *d;
	Message *m, **ll;
	static char err[ERRMAX];

	err[0] = '\0';
	if((fd = open(mb->path, OREAD)) == -1){
		errstr(err, sizeof err);
		return err;
	}
	if((d = dirfstat(fd)) == nil){
		errstr(err, sizeof err);
		close(fd);
		return err;
	}
	if(mb->d){
		if(d->qid.path == mb->d->qid.path)
		if(d->qid.vers == mb->d->qid.vers){
			mdprint(mdir, "\tqids match\n");
			close(fd);
			free(d);
			goto finished;
		}
		free(mb->d);
	}
	logmsg(nil, "reading %s (mdir)", mb->path);
	mb->d = d;

	n = dirreadall(fd, &d);
	close(fd);
	if(n == -1){
		errstr(err, sizeof err);
		return err;
	}

	qsort(d, n, sizeof *d, (int(*)(void*, void*))dircmp);
	ll = &mb->root->part;
	for(i = 0; (m = *ll) != nil || i < n; ){
		if(i < n && dirskip(d + i, &uv)){
			i++;
			continue;
		}
		c = -1;
		if(i >= n)
			c = 1;
		else if(m)
			c = vcmp(uv, m->fileid);
		mdprint(mdir, "consider %s and %D -> %d\n", i<n? d[i].name: 0, m? m->fileid: 1ull, c);
		if(c < 0){
			/* new message */
			mdprint(mdir, "new: %s\n", d[i].name);
			if(d[i].length > Maxmsg){
				mdprint(mdir, "skipping bad size: %llud\n", d[i].length);
				i++;
				continue;
			}
			m = newmessage(mb->root);
			m->fileid = uv;
			m->size = d[i].length;
			m->inmbox = 1;
			m->next = *ll;
			*ll = m;
			ll = &m->next;
			i++;
		}else if(c > 0){
			/* deleted message; */
			mdprint(mdir, "deleted: %s (%D)\n", i<n? d[i].name: 0, m? m->fileid: 0);
			m->inmbox = 0;
			m->deleted = Disappear;
			ll = &m->next;
		}else{
			//logmsg(*ll, "duplicate %s", d[i].name);
			ll = &m->next;
			i++;
		}
	}
	free(d);
finished:
	return nil;
}

static void
mdirdelete(Mailbox *mb, Message *m)
{
	char mpath[Pathlen];
	Mdir* mdir;

	mdir = mb->aux;
	snprint(mpath, sizeof mpath, "%s/%D", mb->path, m->fileid);
	mdprint(mdir, "remove: %s\n", mpath);
	/* may have been removed by other fs.  just log the error. */
	if(remove(mpath) == -1)
		logmsg(m, "remove: %s: %r", mpath);
	m->inmbox = 0;
}

static char*
mdirsync(Mailbox* mb)
{
	Mdir *mdir;

	mdir = mb->aux;
	mdprint(mdir, "mdirsync()\n");
	return mdirread(mdir, mb);
}

static char*
mdirctl(Mailbox *mb, int c, char **v)
{
	Mdir *mdir;

	mdir = mb->aux;
	if(c == 1 && strcmp(*v, "debug") == 0)
		mdir->debug = 1;
	else if(c == 1 && strcmp(*v, "nodebug") == 0)
		mdir->debug = 0;
	else
		return "bad mdir control message";
	return nil;
}

static void
mdirclose(Mailbox *mb)
{
	free(mb->aux);
}

static int
qidcmp(Qid *a, Qid *b)
{
	if(a->path != b->path)
		return 1;
	return a->vers - b->vers;
}

/*
 * .idx strategy. we save the qid.path and .vers
 * of the mdir directory and the date to the index.
 * we accept the work done by the other upas/fs if
 * the index is based on the same (or a newer)
 * qid.  in any event, we recheck the directory after
 * the directory is four hours old.
 */
static int
idxr(char *s, Mailbox *mb)
{
	char *f[5];
	long t, δt, n;
	Dir d;

	n = tokenize(s, f, nelem(f));
	if(n != 4 || strcmp(f[0], "mdirv1") != 0)
		return -1;
	t = strtoul(f[1], 0, 0);
	δt = time(0) - t;
	if(δt < 0 || δt > 4*3600)
		return 0;
	memset(&d, 0, sizeof d);
	d.qid.path = strtoull(f[2], 0, 0);
	d.qid.vers = strtoull(f[3], 0, 0);
	if(mb->d && qidcmp(&mb->d->qid, &d.qid) >= 0)
		return 0;
	if(mb->d == 0)
		mb->d = emalloc(sizeof d);
	mb->d->qid = d.qid;
	mb->d->mtime = t;
	return 0;
}

static void
idxw(Biobuf *b, Mailbox *mb)
{
	Qid q;

	memset(&q, 0, sizeof q);
	if(mb->d)
		q = mb->d->qid;
	Bprint(b, "mdirv1 %lud %llud %lud\n", time(0), q.path, q.vers);
}

char*
mdirmbox(Mailbox *mb, char *path)
{
	int m;
	Dir *d;
	Mdir *mdir;

	d = dirstat(path);
	if(!d && mb->flags & DMcreate){
		createfolder(getuser(), path);
		d = dirstat(path);
	}
	m = d && (d->mode & DMDIR);
	free(d);
	if(!m)
		return Enotme;
	snprint(mb->path, sizeof mb->path, "%s", path);
	mdir = emalloc(sizeof *mdir);
	mdir->debug = 0;
	mb->aux = mdir;
	mb->sync = mdirsync;
	mb->close = mdirclose;
	mb->fetch = mdirfetch;
	mb->delete = mdirdelete;
	mb->remove = localremove;
	mb->rename = localrename;
	mb->idxread = idxr;
	mb->idxwrite = idxw;
	mb->ctl = mdirctl;
	return nil;
}
