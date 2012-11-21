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

int clean = 0;

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

char*
pjoin(char *path, char *name)
{
	if(path[0] == '\0')
		path = "/";
	if(name[0] == '\0')
		return strdup(path);
	if(path[strlen(path)-1] == '/' || name[0] == '/')
		return smprint("%s%s", path, name);
	return smprint("%s/%s", path, name);
}

void
changes1(int fd, char *lpath, char *rpath, char *apath,
	void (*apply)(char *, char *, char *, char *, char *, void *), void *aux)
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
		apply(state, name, lpath, rpath, apath, aux);
	}
	Bterm(&bin);
}

int
changes(char *opt, char *lp, char *rp, char *ap,
	void (*apply)(char *, char *, char *, char *, char *, void *), void *aux)
{
	int pfd[2], pid;
	Waitmsg *w;

	if(pipe(pfd) < 0)
		return -1;
	pid = rfork(RFPROC|RFMEM|RFFDG);
	switch(pid){
	case -1:
		close(pfd[0]);
		close(pfd[1]);
		return -1;
	case 0:
		close(pfd[0]);
		dup(pfd[1], 1);
		close(pfd[1]);
		execl("/bin/derp", "derp", opt, "-p", "0111", lp, ap, rp, nil);
		exits("exec");
	}
	close(pfd[1]);
	changes1(pfd[0], lp, rp, ap, apply, aux);
	close(pfd[0]);
	while(w = wait()){
		if(w->pid == pid){
			if(w->msg[0] != '\0'){
				werrstr("%s", w->msg);
				free(w);
				return 1;
			}
			free(w);
			return 0;
		}
		free(w);
	}
	return -1;
}

void
apply(char *state, char *name, char *lp, char *rp, char *ap, Workdir *)
{
	Dir *rd, *ld;

	ld = rd = nil;
	// fprint(2, "### %s %s ->\t", state, name);
	if(strcmp(state, "na") == 0){
		rd = dirstat(rp);
		if(rd != nil){
			if(rd->qid.type & QTDIR)
				print("mkdir %s\n", lp);
			else
				print("cp %s %s\n", rp, lp);
		}
	}
	else if(strcmp(state, "an") == 0){
	}
	else if(strcmp(state, "nm") == 0){
		print("cp %s %s\n", rp, lp);
	}
	else if(strcmp(state, "mn") == 0){
	}
	else if(strcmp(state, "nd") == 0){
		print("rm %s\n", lp);
	}
	else if(strcmp(state, "dn") == 0){
	}
	else if(strcmp(state, "aa!") == 0 || strcmp(state, "mm!") == 0){
		ld = dirstat(lp);
		rd = dirstat(rp);
		if(ld != nil && rd != nil){
			if(rd->qid.type & QTDIR)
				print("# conflict # mkdir %s\n", lp);
			else if(ld->qid.type & QTDIR)
				print("# conflict # rm -r %s\n", lp);
			else
				print("# conflict # ape/diff3 %s %s %s >%s\n", lp, ap, rp, lp);
		}
	}
	else if(strcmp(state, "md!") == 0){
		print("# delete conflict # rm %s\n", lp);
	}
	else if(strcmp(state, "dm!") == 0){
		print("# delete conflict # cp %s %s\n", rp, lp);
	}
	else {
		print("# unknown status %s %s\n", state, name);
	}
	free(rd);
	free(ld);
}

void
apply1(char *state, char *name, char *lp, char *rp, char *ap, void *aux)
{
	Workdir *wd = aux;

	lp = pjoin(lp, name);
	rp = pjoin(rp, name);
	ap = pjoin(ap, name);
	apply(state, lp + strlen(wd->path)+1, lp, rp, ap, wd);
	free(ap);
	free(rp);
	free(lp);
}

void
apply0(char *state, char *name, char *lp, char *rp, char *ap, void *aux)
{
	Workdir *wd = aux;
	Dir *ld;

	if(clean){
		/* working dir clean */
		apply1(state, name, wd->path, rp, ap, wd);
		return;
	}
	lp = pjoin(wd->path, name);
	ld = dirstat(lp);
	if(clean == 0 && ld != nil && (ld->qid.type & QTDIR) == 0){
		/* check for changes in working directory */
		rp = pjoin(rp, name);
		ap = pjoin(ap, name);
		changes("-Lcq", lp, rp, ap, apply1, wd);
		free(ap);
		free(rp);
	} else {
		/* working dir clean */
		apply1(state, name, wd->path, rp, ap, wd);
	}
	free(lp);
	free(ld);
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
	char lp[MAXPATH], rp[MAXPATH], ap[MAXPATH];
	uchar rh[HASHSZ], ah[HASHSZ];
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
	case 'c':
		clean = 1;
		break;
	} ARGEND;

	memset(&wd, 0, sizeof(wd));
	if(loadworkdir(&wd, *argv) < 0)
		sysfatal("loadworkdir: %r");

	if(memcmp(wd.p2hash, nullid, HASHSZ))
		sysfatal("outstanding merge");

	snprint(rp, sizeof(rp), "%s/%s", mtpt, rev);
	if(readhash(rp, "rev", rh) != 0)
		sysfatal("unable to get hash for %s", rev);

	if(memcmp(rh, wd.p1hash, HASHSZ) == 0){
		fprint(2, "up to date\n");
		exits(0);
	}

	ancestor(mtpt, wd.p1hash, rh, ah);
	if(memcmp(ah, nullid, HASHSZ) == 0)
		sysfatal("no common ancestor between %H and %H", wd.p1hash, rh);

	if(memcmp(ah, rh, HASHSZ) == 0)
		memmove(ah, wd.p1hash, HASHSZ);

	snprint(lp, sizeof(lp), "%s/%H/files", mtpt, wd.p1hash);
	snprint(rp, sizeof(rp), "%s/%H/files", mtpt, rh);
	snprint(ap, sizeof(ap), "%s/%H/files", mtpt, ah);
	
	changes("-L", lp, rp, ap, apply0, &wd);

	exits(0);
}
