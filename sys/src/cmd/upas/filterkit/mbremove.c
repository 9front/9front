/*
 * why did i write this and not use upas/fs?
 */
#include "dat.h"
#include "common.h"

int	qflag;
int	pflag;
int	rflag;
int	tflag;
int	vflag;

/* must be [0-9]+(\..*)? */
static int
dirskip(Dir *a, uvlong *uv)
{
	char *p;

	if(a->length == 0)
		return 1;
	*uv = strtoul(a->name, &p, 0);
	if(*uv < 1000000 || *p != '.')
		return 1;
	*uv = *uv<<8 | strtoul(p+1, &p, 10);
	if(*p)
		return 1;
	return 0;
}

static int
ismbox(char *path)
{
	char buf[512];
	int fd, r;

	fd = open(path, OREAD);
	if(fd == -1)
		return 0;
	r = 1;
	if(read(fd, buf, sizeof buf) < 28+5)
		r = 0;
	else if(strncmp(buf, "From ", 5))
		r = 0;
	close(fd);
	return r;
}

int
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

int
idiotcheck(char *path, Dir *d, int getindex)
{
	uvlong v;

	if(d->mode & DMDIR)
		return 0;
	if(!strncmp(d->name, "L.", 2))
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
	if(vflag)
		fprint(2, "rm %s\n", buf);
	if(!pflag)
		return remove(buf);
	return 0;
}

int
rm(char *dir, int level)
{
	char buf[Pathlen];
	int i, n, r, fd, isdir;
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
	for(i = 0; i < n; i++){
		snprint(buf, sizeof buf, "%s/%s", dir, d[i].name);
		if(rflag)
			r |= rm(buf, level+1);
		if(idiotcheck(buf, d+i, level+rflag) == -1)
			continue;
		if(vremove(buf) != 0)
			r = -1;
	}
	free(d);
	return r;
}

void
nukeidx(char *buf)
{
	char buf2[Pathlen];

	snprint(buf2, sizeof buf2, "%s.idx", buf);
	vremove(buf2);
	snprint(buf2, sizeof buf2, "%s.imp", buf);
	vremove(buf2);
}

void
truncidx(char *buf)
{
	char buf2[Pathlen];

	snprint(buf2, sizeof buf2, "%s.idx", buf);
	vremove(buf2);
//	snprint(buf2, sizeof buf2, "%s.imp", buf);
//	vremove(buf2);
}

static int
removefolder0(char *user, char *folder, char *ftype)
{
	char *msg, buf[Pathlen];
	int r, isdir;
	Dir *d;

	assert(folder != 0);
	mboxpathbuf(buf, sizeof buf, user, folder);
	if((d = dirstat(buf)) == 0){
		fprint(2, "%s: %s doesn't exist\n", buf, ftype);
		return 0;
	}
	isdir = d->mode & DMDIR;
	free(d);
	msg = "deleting";
	if(tflag)
		msg = "truncating";
	fprint(2, "%s %s: %s\n", msg, ftype, buf);

	/* must match folder.c:/^openfolder */
	r = rm(buf, 0);
	if(!tflag)
		r = vremove(buf);
	else if(!isdir)
		r = open(buf, OWRITE|OTRUNC);

	if(tflag)
		truncidx(buf);
	else
		nukeidx(buf);

	if(r == -1){
		fprint(2, "%s: can't %s %s\n", buf, msg, ftype);
		return -1;
	}
	close(r);
	return 0;
}

int
removefolder(char *user, char *folder)
{
	return removefolder0(user, folder, "folder");
}

int
removembox(char *user, char *mbox)
{
	char buf[Pathlen];

	if(mbox == 0)
		snprint(buf, sizeof buf, "mbox");
	else
		snprint(buf, sizeof buf, "%s/mbox", mbox);
	return removefolder0(user, buf, "mbox");
}

void
usage(void)
{
	fprint(2, "usage: mbremove [-fpqrtv] ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int r;
	int (*f)(char*, char*);

	f = removembox;
	ARGBEGIN{
	case 'f':
		f = removefolder;
		break;
	case 'p':
		pflag++;
		break;
	case 'q':
		qflag++;
		close(2);
		open("/dev/null", OWRITE);
		break;
	case 'r':
		rflag++;
		break;
	case 't':
		tflag++;
		break;
	case 'v':
		vflag++;
		break;
	default:
		usage();
	}ARGEND

	r = 0;
	tmfmtinstall();
	for(; *argv; argv++)
		r |= f(getuser(), *argv);
	if(r)
		exits("errors");
	exits("");
}
