/* hg debug stuff, will become update/merge program */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

typedef struct Workdir Workdir;
typedef struct Dstate Dstate;

struct Dstate
{
	Dstate	*next;
	int	mode;
	ulong	size;
	long	mtime;
	char	status;
	char	path[];
};

struct Workdir
{
	char	path[MAXPATH];
	uchar	p1hash[HASHSZ];
	uchar	p2hash[HASHSZ];
	Dstate	*ht[256];
};

static Dstate**
dslookup(Workdir *wd, char *path)
{
	Dstate **hp, *h;

	hp = &wd->ht[hashstr(path) % nelem(wd->ht)];
	for(h = *hp; h != nil; h = *hp){
		if(strcmp(path, h->path) == 0)
			break;
		hp = &h->next;
	}
	return hp;
}

static void
clearworkdir(Workdir *wd)
{
	Dstate *h;
	int i;

	for(i=0; i<nelem(wd->ht); i++)
		while(h = wd->ht[i]){
			wd->ht[i] = h->next;
			free(h);
		}
	memset(wd, 0, sizeof(*wd));
}

static int
loadworkdir(Workdir *wd, char *path)
{
	uchar hdr[1+4+4+4+4];
	char buf[MAXPATH], *err;
	Dstate **hp, *h;
	int fd, n;

	memset(wd, 0, sizeof(*wd));
	if(getworkdir(wd->path, path) < 0)
		return -1;
	snprint(buf, sizeof(buf), "%s/.hg/dirstate", wd->path);
	if((fd = open(buf, OREAD)) < 0)
		return -1;
	err = "dirstate truncated";
	if(read(fd, wd->p1hash, HASHSZ) != HASHSZ)
		goto Error;
	if(read(fd, wd->p2hash, HASHSZ) != HASHSZ)
		goto Error;
	for(;;){
		if((n = read(fd, hdr, sizeof(hdr))) == 0)
			break;
		if(n < 0){
			err = "reading dirstate: %r";
			goto Error;
		}
		if(n != sizeof(hdr))
			goto Error;
		n = hdr[16] | hdr[15]<<8 | hdr[14]<<16 | hdr[13]<<24;
		if(n < 0 || n >= sizeof(buf)){
			err = "bad path length in dirstate";
			goto Error;
		}
		if(read(fd, buf, n) != n)
			goto Error;
		buf[n++] = 0;
		hp = dslookup(wd, buf);
		if(*hp != nil){
			err = "duplicate entry in dirstate";
			goto Error;
		}
		h = malloc(sizeof(*h) + n);
		if(h == nil){
			err = "out of memory";
			goto Error;
		}
		memmove(h->path, buf, n);
		h->status = hdr[0];
		h->mode = hdr[4] | hdr[3]<<8 | hdr[2]<<16 | hdr[1]<<24;
		h->size = hdr[8] | hdr[7]<<8 | hdr[6]<<16 | hdr[5]<<24;
		h->mtime = hdr[12] | hdr[11]<<8 | hdr[10]<<16 | hdr[9]<<24;
		h->next = *hp;
		*hp = h;
	}
	close(fd);
	return 0;
Error:
	clearworkdir(wd);
	close(fd);
	werrstr(err);
	return -1;
}

void
apply1(char *state, char *name, char *lpath, char *rpath, Workdir *wd)
{
	char buf[MAXPATH];
	Dir *d;
	Dstate *ds;

	ds = *dslookup(wd, name);
	if(ds != nil){
		snprint(buf, sizeof(buf), "%s/%s", wd->path, name);
		d = dirstat(buf);
	}
	if(strcmp(state, "na") == 0){
		snprint(buf, sizeof(buf), "%s/%s", rpath, name);
		d = dirstat(buf);
		if(d != nil){
			if(d->qid.type & QTDIR)
				print("mkdir %s/%s\n", wd->path, name);
			else
				print("cp %s %s/%s\n", buf, wd->path, name);
			free(d);
		}
	}
	else if(strcmp(state, "nm") == 0)
		print("cp %s/%s %s/%s\n", rpath, name, wd->path, name);
	else if(strcmp(state, "nd") == 0)
		print("rm %s/%s\n", wd->path, name);
}

void
applychanges(int fd, char *lpath, char *rpath, Workdir *wd)
{
	char *state, *name;
	Biobuf bin;

	Binit(&bin, fd, OREAD);
	while((state = Brdstr(&bin, '\n', 1)) != nil){
		if((name = strchr(state, '\t')) == nil)
			continue;
		while(*name == '\t' || *name == ' ')
			*name++ = '\0';
		if(name[0] == '.' && name[1] == '/')
			name += 2;
		apply1(state, name, lpath, rpath, wd);
	}
	Bterm(&bin);
}

void
changes(char *lpath, char *rpath, char *apath, Workdir *wd)
{
	int pfd[2];

	if(pipe(pfd) < 0)
		sysfatal("pipe: %r");
	switch(rfork(RFPROC|RFMEM|RFFDG)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		close(pfd[0]);
		dup(pfd[1], 1);
		close(pfd[1]);
		execl("/bin/derp", "derp", "-L", "-p", "0111", lpath, apath, rpath, nil);
		sysfatal("execl: %r");
	}
	close(pfd[1]);
	applychanges(pfd[0], lpath, rpath, wd);
	close(pfd[0]);
}

void
usage(void)
{
	fprint(2, "usage: %s [-m mtpt] [-r rev] [root]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char lpath[MAXPATH], rpath[MAXPATH], apath[MAXPATH];
	uchar rhash[HASHSZ], ahash[HASHSZ];
	char *mtpt, *rev;
	Workdir wd;

	fmtinstall('H', Hfmt);

	rev = "tip";
	mtpt = "/mnt/hg";

	ARGBEGIN {
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 'r':
		rev = EARGF(usage());
		break;
	} ARGEND;

	memset(&wd, 0, sizeof(wd));
	if(loadworkdir(&wd, *argv) < 0)
		sysfatal("loadworkdir: %r");

	if(memcmp(wd.p2hash, nullid, HASHSZ))
		sysfatal("outstanding merge");

	snprint(rpath, sizeof(rpath), "%s/%s", mtpt, rev);
	if(readhash(rpath, "rev", rhash) != 0)
		sysfatal("unable to get hash for %s", rev);

	if(memcmp(rhash, wd.p1hash, HASHSZ) == 0){
		fprint(2, "up to date\n");
		exits(0);
	}

	ancestor(mtpt, wd.p1hash, rhash, ahash);
	if(memcmp(ahash, nullid, HASHSZ) == 0)
		sysfatal("no common ancestor between %H and %H", wd.p1hash, rhash);

	if(memcmp(ahash, rhash, HASHSZ) == 0)
		memmove(ahash, wd.p1hash, HASHSZ);

	snprint(lpath, sizeof(lpath), "%s/%H/files", mtpt, wd.p1hash);
	snprint(rpath, sizeof(rpath), "%s/%H/files", mtpt, rhash);
	snprint(apath, sizeof(apath), "%s/%H/files", mtpt, ahash);
	
	changes(lpath, rpath, apath, &wd);

	exits(0);
}
