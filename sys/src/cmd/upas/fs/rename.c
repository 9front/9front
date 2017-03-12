#include "common.h"
#include "dat.h"

#define deprint(...)	/* eprint(__VA_ARGS__) */

static int
delivery(char *s)
{
	if(strncmp(s, "/mail/fs/", 9) == 0)
	if((s = strrchr(s, '/')) && strcmp(s + 1, "mbox") == 0)
		return 1;
	return 0;
}

static int
isdir(char *s)
{
	int isdir;
	Dir *d;

	d = dirstat(s);
	isdir = d && d->mode & DMDIR;
	free(d);
	return isdir;
}

static int
docreate(char *file, int perm)
{
	int fd;
	Dir ndir;
	Dir *d;

	fd = create(file, OREAD, perm);
	if(fd < 0)
		return -1;
	d = dirfstat(fd);
	if(d == nil)
		return -1;
	nulldir(&ndir);
	ndir.mode = perm;
	ndir.gid = d->uid;
	dirfwstat(fd, &ndir);
	close(fd);
	return 0;
}

static int
rollup(char *s)
{
	char *p;
	int mode;

	if(access(s, 0) == 0)
		return -1;

	/*
	 * if we can deliver to this mbox, it needs
	 * to be read/execable all the way down
	 */
	mode = 0711;
	if(delivery(s))
		mode = 0755;

	for(p = s; p; p++) {
		if(*p == '/')
			continue;
		p = strchr(p, '/');
		if(p == 0)
			break;
		*p = 0;
		if(access(s, 0) != 0)
		if(docreate(s, DMDIR|mode) < 0)
			return -1;
		*p = '/';
	}
	return 0;
}

static int
copyfile(char *a, char *b, int flags)
{
	char *s;
	int fd, fd1, mode, i, m, n, r;
	Dir *d;

	mode = 0600;
	if(delivery(b))
		mode = 0622;
	fd = open(a, OREAD);
	fd1 = create(b, OWRITE|OEXCL, DMEXCL|mode);
	if(fd == -1 || fd1 == -1){
		close(fd);
		close(fd1);
		return -1;
	}
	s = malloc(64*1024);
	i = m = 0;
	while((n = read(fd, s, sizeof s)) > 0)
		for(i = 0; i != n; i += m)
			if((m = write(fd1, s + i, n - i)) == -1)
				goto lose;
lose:
	free(s);
	close(fd);
	close(fd1);
	if(i != m || n != 0)
		return -1;

	if((flags & Rtrunc) == 0)
		return vremove(a);

	fd = open(a, ORDWR);
	if(fd == -1)
		return -1;
	r = -1;
	if(d = dirfstat(fd)){
		d->length = 0;
		r = dirfwstat(fd, d);
		free(d);
	}
	return r;
}

static int
copydir(char *a, char *b, int flags)
{
	char *p, buf[Pathlen], ns[Pathlen], owd[Pathlen];
	int fd, fd1, len, i, n, r;
	Dir *d;

	fd = open(a, OREAD);
	fd1 = create(b, OWRITE|OEXCL, DMEXCL|0777);
	close(fd1);
	if(fd == -1 || fd1 == -1){
		close(fd);
		return -1;
	}

	/* fixup mode */
	if(delivery(b))
	if(d = dirfstat(fd)){
		d->mode |= 0777;
		dirfwstat(fd, d);
		free(d);
	}

	getwd(owd, sizeof owd);
	if(chdir(a) == -1)
		return -1;

	p = seprint(buf, buf + sizeof buf, "%s/", b);
	len = buf + sizeof buf - p;
	n = dirreadall(fd, &d);
	r = 0;
	for(i = 0; i < n; i++){
		snprint(p, len, "%s", d[i].name);
		if(d->mode & DMDIR){
			snprint(ns, sizeof ns, "%s/%s", a, d[i].name);
			r |= copydir(ns, buf, 0);
			chdir(a);
		}else
			r |= copyfile(d[i].name, buf, 0);
		if(r)
			break;
	}
	free(d);

	if((flags & Rtrunc) == 0)
		r |= vremove(a);

	chdir(owd);
	return r;
}

int
rename(char *a, char *b, int flags)
{
	char *e0, *e1;
	int fd, r;
	Dir *d;

	e0 = strrchr(a, '/');
	e1 = strrchr(b, '/');
	if(!e0 || !e1 || !e1[1])
		return -1;

	if(e0 - a == e1 - b)
	if(strncmp(a, b, e0 - a) == 0)
	if(!delivery(a) || isdir(a)){
		fd = open(a, OREAD);
		if(!(d = dirfstat(fd))){
			close(fd);
			return -1;
		}
		d->name = e1 + 1;
		r = dirfwstat(fd, d);
		deprint("rename %s %s -> %d\n", a, b, r);
		if(r != -1 && flags & Rtrunc)
			close(create(a, OWRITE, d->mode));
		free(d);
		close(fd);
		return r;
	}

	if(rollup(b) == -1)
		return -1;
	if(isdir(a))
		return copydir(a, b, flags);
	return copyfile(a, b, flags);
}

char*
localrename(Mailbox *mb, char *p2, int flags)
{
	char *path, *msg;
	int r;
	static char err[2*Pathlen];

	path = mb->path;
	msg = "rename";
	if(flags & Rtrunc)
		msg = "move";
	deprint("localrename %s: %s %s\n", msg, path, p2);

	r = rename(path, p2, flags);
	if(r == -1){
		snprint(err, sizeof err, "%s: can't %s\n", path, msg);
		deprint("localrename %s\n", err);
		return err;
	}
	close(r);
	return 0;
}
