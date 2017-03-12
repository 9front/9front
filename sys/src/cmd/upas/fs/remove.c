#include "common.h"
#include "dat.h"

#define deprint(...)	/* eprint(__VA_ARGS__) */

extern int dirskip(Dir*, uvlong*);

static int
ismbox(char *path)
{
	char buf[512];
	int fd, r;

	fd = open(path, OREAD);
	if(fd == -1)
		return 0;
	r = 1;
	if(read(fd, buf, sizeof buf) < 28 + 5)
		r = 0;
	else if(strncmp(buf, "From ", 5))
		r = 0;
	close(fd);
	return r;
}

static int
isindex(Dir *d)
{
	char *p;

	p = strrchr(d->name, '.');
	if(!p)
		return -1;
	if(strcmp(p, ".idx") || strcmp(p, ".imp"))
		return 1;
	return 0;
}

static int
idiotcheck(char *path, Dir *d, int getindex)
{
	uvlong v;

	if(d->mode & DMDIR)
		return 0;
	if(strncmp(d->name, "L.", 2) == 0)
		return 0;
	if(getindex && isindex(d))
		return 0;
	if(!dirskip(d, &v) || ismbox(path))
		return 0;
	return -1;
}

int
vremove(char *buf)
{
	deprint("rm %s\n", buf);
	return remove(buf);
}

static int
rm(char *dir, int flags, int level)
{
	char buf[Pathlen];
	int i, n, r, fd, isdir, rflag;
	Dir *d;

	d = dirstat(dir);
	isdir = d->mode & DMDIR;
	free(d);
	if(!isdir)
		return 0;
	fd = open(dir, OREAD);
	if(fd == -1)
		return -1;
	n = dirreadall(fd, &d);
	close(fd);
	r = 0;
	rflag = flags & Rrecur;
	for(i = 0; i < n; i++){
		snprint(buf, sizeof buf, "%s/%s", dir, d[i].name);
		if(rflag)
			r |= rm(buf, flags, level + 1);
		if(idiotcheck(buf, d + i, level + rflag) == -1)
			continue;
		if(vremove(buf) != 0)
			r = -1;
	}
	free(d);
	return r;
}

void
rmidx(char *buf, int flags)
{
	char buf2[Pathlen];

	snprint(buf2, sizeof buf2, "%s.idx", buf);
	vremove(buf2);
	if((flags & Rtrunc) == 0){
		snprint(buf2, sizeof buf2, "%s.imp", buf);
		vremove(buf2);
	}
}

char*
localremove(Mailbox *mb, int flags)
{
	char *msg, *path;
	int r, isdir;
	Dir *d;
	static char err[2*Pathlen];

	path = mb->path;
	if((d = dirstat(path)) == 0){
		snprint(err, sizeof err, "%s: doesn't exist\n", path);
		return 0;
	}
	isdir = d->mode & DMDIR;
	free(d);
	msg = "deleting";
	if(flags & Rtrunc)
		msg = "truncating";
	deprint("%s: %s\n", msg, path);

	/* must match folder.c:/^openfolder */
	r = rm(path, flags, 0);
	if((flags & Rtrunc) == 0)
		r = vremove(path);
	else if(!isdir)
		close(r = open(path, OWRITE|OTRUNC));

	rmidx(path, flags);

	if(r == -1){
		snprint(err, sizeof err, "%s: can't %s\n", path, msg);
		return err;
	}
	return 0;
}
