#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

#include <ctype.h>
#include <flate.h>
#include <auth.h>
#include <fcall.h>
#include <9p.h>

enum {
	Qroot,
		Qrev,
			Qrev1,
			Qrev2,
			Qlog,
			Qwho,
			Qwhy,
			Qfiles,
			Qchanges,
				Qtree,
};

static char *nametab[Qtree+1] = {
	"/",
		nil,
			"rev1",
			"rev2",
			"log",
			"who",
			"why",
			"files",
			"changes",
				nil,
};

static Revlog changelog;
static Revlog manifest;

static Revinfo*
getrevinfo(int rev)
{
	Revinfo *ri;

	if(rev < 0 || rev >= changelog.nmap)
		return nil;
	if(ri = changelog.map[rev].aux)
		return ri;
	if(ri = loadrevinfo(&changelog, rev))
		changelog.map[rev].aux = ri;
	return ri;
}

static char*
fsmkuid(char *s)
{
	if(s){
		char *x;

		while(x = strchr(s, '<'))
			s = x+1;
		s = estrdup9p(s);
		if(x = strchr(s, '>'))
			*x = 0;
		if(x = strchr(s, '@'))
			*x = 0;
		if(x = strchr(s, '\n'))
			*x = 0;
	}
	if(s == nil || *s == 0){
		free(s);
		s = estrdup9p("hgfs");
	}
	return s;
}

static void
fsmkqid(Qid *q, int level, void *aux)
{
	Revnode *nd;
	Revinfo *ri;

	switch(level){
	case Qroot:
		q->type = QTDIR;
		q->path = Qroot;
		q->vers = 0;
		break;
	case Qrev:
	case Qfiles:
	case Qchanges:
		q->type = QTDIR;
		if(0){
	case Qrev1:
	case Qrev2:
	case Qlog:
	case Qwho:
	case Qwhy:
		q->type = 0;
		}
		ri = aux;
		q->path = *((uvlong*)ri->chash) + (level - Qrev);
		q->vers = 0;
		break;
	case Qtree:
		nd = aux;
		if(nd->hash){
			q->type = 0;
		} else {
			q->type = QTDIR;
		}
		q->path = nd->path;
		q->vers = 0;
		break;
	}
}

static void
fsmkdir(Dir *d, int level, void *aux)
{
	char buf[64], *s;
	Revnode *nd;
	Revinfo *ri;
	int rev;

	memset(d, 0, sizeof(*d));

	fsmkqid(&d->qid, level, aux);

	d->mode = 0444;
	if(d->qid.type == QTDIR)
		d->mode |= DMDIR | 0111;

	s = nil;
	ri = nil;
	switch(level){
	case Qroot:
		goto Namegen;
	case Qrev:
	case Qrev1:
	case Qrev2:
		ri = aux;
		rev = hashrev(&changelog, ri->chash);
		if(level == Qrev1)
			rev = changelog.map[rev].p1rev;
		else if(level == Qrev2)
			rev = changelog.map[rev].p2rev;
		if(rev >= 0)
			snprint(s = buf, sizeof(buf), "%d.%H", rev, changelog.map[rev].hash);
		if(level == Qrev){
			d->name = estrdup9p(buf);
			break;
		}
		goto Strgen;
	case Qlog:
		ri = aux;
		if((rev = hashrev(&changelog, ri->chash)) >= 0)
			d->length = changelog.map[rev].flen;
		goto Namegen;
	case Qwho:
		ri = aux;
		s = ri->who;
		goto Strgen;
	case Qwhy:
		ri = aux;
		s = ri->why;
	Strgen:
		d->length = s ? strlen(s)+1 : 0;
	case Qfiles:
	case Qchanges:
		ri = aux;
		/* no break */
	Namegen:
		d->name = estrdup9p(nametab[level]);
		break;
	case Qtree:
		nd = aux;
		d->name = estrdup9p(nd->name);
		if(nd->hash){
			char path[MAXPATH];
			Revlog rl;

			nodepath(seprint(path, path+MAXPATH, ".hg/store/data"), path+MAXPATH, nd);
			if(revlogopen(&rl, path, OREAD) < 0)
				break;
			if((rev = hashrev(&rl, nd->hash)) >= 0){
				d->length = rl.map[rev].flen;
				ri = getrevinfo(rl.map[rev].linkrev);
			}
			revlogclose(&rl);
		}
		break;
	}

	if(ri){
		d->atime = d->mtime = ri->when;
		d->muid = fsmkuid(ri->who);
		d->uid = fsmkuid(ri->who);
	} else
		d->atime = d->mtime = time(0);

	if(d->uid == nil)
		d->uid = fsmkuid(nil);
	if(d->gid == nil)
		d->gid = fsmkuid(nil);
	if(d->muid == nil)
		d->muid = fsmkuid(nil);
}

static void
fsattach(Req *r)
{
	Revfile *rf;

	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, "invalid attach specifier");
		return;
	}
	r->fid->qid.path = Qroot;
	r->fid->qid.type = QTDIR;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;

	rf = emalloc9p(sizeof(*rf));
	rf->level = Qroot;
	rf->info = nil;
	rf->tree = nil;
	rf->node = nil;

	rf->fd = -1;
	rf->buf = nil;

	r->fid->aux = rf;

	respond(r, nil);
}

static void
fsstat(Req *r)
{
	Revfile *rf;

	rf = r->fid->aux;
	if(rf->level < Qtree)
		fsmkdir(&r->d, rf->level,  rf->info);
	else
		fsmkdir(&r->d, rf->level,  rf->node);
	respond(r, nil);
}

static int
findrev(Revlog *rl, char *name)
{
	uchar hash[HASHSZ];
	int n, i, rev;
	char *s;

	rev = strtol(name, &s, 10);
	if(s > name && (*s == 0 || ispunct(*s)))
		return rev;
	rev = -1;
	if(s = strchr(name, '.'))
		name = s+1;
	if((n = strhash(name, hash)) > 0){
		for(i=0; i<rl->nmap; i++){
			if(memcmp(rl->map[i].hash, hash, n) == 0){
				if(rev < 0)
					rev = i;
				else {
					rev = -1;
					break;
				}
			}
		}
	}
	return rev;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	Revfile *rf;
	Revnode *nd;
	int i;

	if(!(fid->qid.type&QTDIR))
		return "walk in non-directory";

	rf = fid->aux;
	if(strcmp(name, "..") == 0){
		switch(rf->level){
		case Qroot:
			break;
		case Qrev:
			rf->info = nil;
			rf->level = Qroot;
			break;
		case Qfiles:
		case Qchanges:
			closerevtree(rf->tree);
			rf->tree = nil;
			rf->level = Qrev;
			break;
		case Qtree:
			if((rf->node = rf->node->up) == rf->tree->root)
				rf->level = rf->tree->level;
			break;
		}
	} else {
		switch(rf->level){
		case Qroot:
			i = findrev(&changelog, name);
			if(rf->info = getrevinfo(i)){
				rf->level = Qrev;
				break;
			}
		Notfound:
			return "directory entry not found";
			break;
		case Qrev:
			for(i = Qrev+1; i < Qtree; i++){
				if(nametab[i] == nil)
					continue;
				if(strcmp(name, nametab[i]) == 0)
					break;
			}
			switch(i){
			case Qtree:
				goto Notfound;
			case Qfiles:
				if((rf->tree = loadfilestree(&changelog, &manifest, rf->info)) == nil)
					goto Notfound;
				break;
			case Qchanges:
				if((rf->tree = loadchangestree(&changelog, &manifest, rf->info)) == nil)
					goto Notfound;
				break;
			}
			if(rf->tree){
				rf->node = rf->tree->root;
				rf->tree->level = i;
			}
			rf->level = i;
			break;
		case Qtree:
		case Qfiles:
		case Qchanges:
			for(nd = rf->node->down; nd; nd = nd->next)
				if(strcmp(nd->name, name) == 0)
					break;
			if(nd == nil)
				goto Notfound;
			rf->node = nd;
			rf->level = Qtree;
			break;
		}
	}

	if(rf->level < Qtree)
		fsmkqid(qid, rf->level, rf->info);
	else
		fsmkqid(qid, rf->level, rf->node);
	fid->qid = *qid;

	return nil;
}

static char*
fsclone(Fid *oldfid, Fid *newfid)
{
	Revfile *orf, *rf;

	rf = nil;
	if(orf = oldfid->aux){
		rf = emalloc9p(sizeof(*rf));
		*rf = *orf;
		if(rf->tree)
			incref(rf->tree);
		if(rf->fd >= 0)
			rf->fd = dup(rf->fd, -1);
		if(rf->buf)
			rf->buf = estrdup9p(rf->buf);
	}
	newfid->aux = rf;
	return nil;
}

static void
fsdestroyfid(Fid *fid)
{
	Revfile *rf;

	if(rf = fid->aux){
		closerevtree(rf->tree);
		if(rf->fd >= 0)
			close(rf->fd);
		free(rf->buf);
		free(rf);
	}
}

static void
fsopen(Req *r)
{
	respond(r, nil);
}

static int
rootgen(int i, Dir *d, void *)
{
	Revinfo *ri;

	if((ri = getrevinfo(i)) == nil)
		return -1;
	fsmkdir(d, Qrev, ri);
	return 0;
}

static int
revgen(int i, Dir *d, void *aux)
{
	i += Qrev+1;
	if(i >= Qtree)
		return -1;
	fsmkdir(d, i, aux);
	return 0;
}

static int
treegen(int i, Dir *d, void *aux)
{
	Revnode *nd = aux;

	while(i > 0 && nd){
		nd = nd->next;
		i--;
	}
	if(i || nd == nil)
		return -1;
	fsmkdir(d, Qtree, nd);
	return 0;
}

static void
fsread(Req *r)
{
	Revfile *rf;

	rf = r->fid->aux;
	if(r->fid->qid.type == QTDIR){
		switch(rf->level){
		default:
			respond(r, "bug in fsread");
			return;
		case Qroot:
			dirread9p(r, rootgen, nil);
			respond(r, nil);
			return;
		case Qrev:
			dirread9p(r, revgen, rf->info);
			respond(r, nil);
			return;
		case Qtree:
		case Qfiles:
		case Qchanges:
			dirread9p(r, treegen, rf->node->down);
			respond(r, nil);
			return;
		}
	} else {
		char buf[MAXPATH];
		Revlog rl;
		char *s;
		int i, n;

		switch(rf->level){
		default:
			respond(r, "bug in fsread");
			return;
		case Qlog:
			if(rf->fd >= 0)
				break;
			if((rf->fd = revlogopentemp(&changelog, hashrev(&changelog, rf->info->chash))) < 0){
				responderror(r);
				return;
			}
			break;
		case Qrev1:
		case Qrev2:
			s = nil;
			if((i = hashrev(&changelog, rf->info->chash)) >= 0){
				if(rf->level == Qrev1)
					i = changelog.map[i].p1rev;
				else
					i = changelog.map[i].p2rev;
				if(i >= 0)
					snprint(s = buf, sizeof(buf), "%d.%H", i, changelog.map[i].hash);
			}
			goto Strgen;
		case Qwho:
			s = rf->info->who;
			goto Strgen;
		case Qwhy:
			s = rf->info->why;
		Strgen:
			if(rf->buf == nil)
				rf->buf = s ? smprint("%s\n", s) : estrdup9p("");
			readstr(r, rf->buf);
			respond(r, nil);
			return;
		case Qtree:
			if(rf->fd >= 0)
				break;
			nodepath(seprint(buf, buf+sizeof(buf), ".hg/store/data"), buf+sizeof(buf), rf->node);
			if(revlogopen(&rl, buf, OREAD) < 0){
				responderror(r);
				return;
			}
			if((rf->fd = revlogopentemp(&rl, hashrev(&rl, rf->node->hash))) < 0){
				responderror(r);
				revlogclose(&rl);
				return;
			}
			revlogclose(&rl);
			break;
		}
		if((n = pread(rf->fd, r->ofcall.data, r->ifcall.count, r->ifcall.offset)) < 0){
			responderror(r);
			return;
		}
		r->ofcall.count = n;
		respond(r, nil);
	}
}

Srv fs = 
{
	.attach=fsattach,
	.stat=fsstat,
	.walk1=fswalk1,
	.clone=fsclone,
	.destroyfid=fsdestroyfid,
	.open=fsopen,
	.read=fsread,
};

void
usage(void)
{
	fprint(2, "usage: %s [-D] [-m mtpt] [-s srv]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char *srv, *mtpt;

	inflateinit();
	fmtinstall('H', Hfmt);

	srv = nil;
	mtpt = "/n/hg";

	ARGBEGIN {
	case 'D':
		chatty9p++;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 's':
		srv = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	if(revlogopen(&changelog, ".hg/store/00changelog", OREAD) < 0)
		sysfatal("can't open changelog: %r\n");
	if(revlogopen(&manifest, ".hg/store/00manifest", OREAD) < 0)
		sysfatal("can't open menifest: %r\n");

	postmountsrv(&fs, srv, mtpt, MREPL);
}
