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
			Qrev0,
			Qrev1,
			Qrev2,
			Qlog,
			Qwho,
			Qwhy,
			Qfiles,
			Qchanges,
				Qtree,
				Qtreerev,
};

static char *nametab[] = {
	"/",
		nil,
			"rev",
			"rev1",
			"rev2",
			"log",
			"who",
			"why",
			"files",
			"changes",
				nil,
				nil,
};

static Revlog changelog;
static Revlog manifest;
static Revlog *revlogs;
static int nfreerevlogs = 0;

static char workdir[MAXPATH];
static int mangle = 0;

static Revlog*
getrevlog(Revnode *nd)
{
	char buf[MAXPATH];
	Revlog *rl, **link;
	int mang;

	mang = mangle;
Again:
	nodepath(seprint(buf, buf+sizeof(buf), "%s/.hg/store/data", workdir),
		buf+sizeof(buf), nd, mang);
	link = &revlogs;
	while(rl = *link){
		if(strcmp(buf, rl->path) == 0){
			if(rl->ref == 0) nfreerevlogs--;
			break;
		}
		if(nfreerevlogs > 8 && rl->ref == 0){
			*link = rl->next;
			nfreerevlogs--;
			revlogclose(rl);
			free(rl);
			continue;
		}
		link = &rl->next;
	}
	if(rl == nil){
		rl = emalloc9p(sizeof(*rl));
		memset(rl, 0, sizeof(*rl));
		if(revlogopen(rl, buf, OREAD) < 0){
			free(rl);
			if(mang++ == 0)
				goto Again;
			return nil;
		}
		rl->next = revlogs;
		revlogs = rl;
		if(mang)
			mangle = 1;
	} else
		revlogupdate(rl);
	incref(rl);
	return rl;
}

static void
closerevlog(Revlog *rl)
{
	if(rl != nil && decref(rl) == 0)
		nfreerevlogs++;
}

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

static Revtree*
getrevtree(Revtree *(*fn)(Revlog *, Revlog *, Revinfo *), Revinfo *ri)
{
	static ulong gen;
	static struct {
		ulong g;
		void *f;
		Revinfo *i;
		Revtree *t;
	} cache[4];
	Revtree *rt;
	int i, j;

	for(i=j=0; i<nelem(cache); i++){
		if(cache[i].t == nil){
			j = i;
			continue;
		}
		if(cache[i].f == fn && cache[i].i == ri){
			cache[i].g = ++gen;
			rt = cache[i].t;
			goto found;
		}
		if(cache[j].t && cache[i].g < cache[j].g)
			j = i;
	}
	if((rt = (*fn)(&changelog, &manifest, ri)) == nil)
		return nil;

	closerevtree(cache[j].t);

	cache[j].g = ++gen;
	cache[j].f = fn;
	cache[j].i = ri;
	cache[j].t = rt;

found:
	incref(rt);
	return rt;
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
	case Qrev0:
	case Qrev1:
	case Qrev2:
	case Qlog:
	case Qwho:
	case Qwhy:
		q->type = 0;
		}
		ri = aux;
		q->path = hash2qid(ri->chash) + (level - Qrev);
		q->vers = 0;
		break;
	case Qtree:
	case Qtreerev:
		nd = aux;
		if(level == Qtree && nd->down){
			q->type = QTDIR;
		} else {
			q->type = 0;
		}
		q->path = nd->path + (level - Qtree);
		q->vers = 0;
		break;
	}
}

static char*
fsmkrevname(char *buf, int nbuf, int rev)
{
	if(rev < 0 || rev >= changelog.nmap)
		return nil;
	snprint(buf, nbuf, "%d.%H", rev, changelog.map[rev].hash);
	return buf;
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
	if(d->qid.type & QTDIR)
		d->mode |= DMDIR | 0111;

	ri = nil;
	switch(level){
	case Qroot:
		goto Namegen;
	case Qrev:
	case Qrev0:
	case Qrev1:
	case Qrev2:
		ri = aux;
	Revgen:
		rev = hashrev(&changelog, ri->chash);
		if(level == Qrev1)
			rev = changelog.map[rev].p1rev;
		else if(level == Qrev2)
			rev = changelog.map[rev].p2rev;
		s = fsmkrevname(buf, sizeof(buf), rev);
		if(level == Qrev){
			d->name = estrdup9p(s);
			break;
		}
		goto Strgen;
	case Qlog:
		ri = aux;
		d->length = ri->loglen;
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
		if(level == Qtreerev)
			break;
	case Qfiles:
	case Qchanges:
		ri = aux;
		/* no break */
	Namegen:
		d->name = estrdup9p(nametab[level]);
		break;
	case Qtree:
	case Qtreerev:
		nd = aux;
		if(level == Qtree && nd->mode == 'x')
			d->mode |= 0111;
		d->name = estrdup9p(nd->name);
		if(nd->hash){
			Revlog *rl;

			if((rl = getrevlog(nd)) == nil)
				break;
			if((rev = hashrev(rl, nd->hash)) >= 0){
				if(level == Qtree){
					/*
					 * BUG: this is not correct. mercurial might
					 * prefix the data log with random \1\n escaped
					 * metadata strings (see fmetaheader()) and the flen
					 * *includes* the metadata part. we try to compensate
					 * for this once the revision got extracted and
					 * subtract the metadata header in fsstat().
					 */
					d->length = rl->map[rev].flen;
				}
				ri = getrevinfo(rl->map[rev].linkrev);
			}
			closerevlog(rl);
			if(level == Qtreerev && ri)
				goto Revgen;
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
	rf->rlog = nil;

	rf->fd = -1;
	rf->doff = 0;
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
	else {
		fsmkdir(&r->d, rf->level,  rf->node);
		if(rf->level == Qtree)
			r->d.length -= rf->doff;
	}
	respond(r, nil);
}

static int
findrev(Revlog *rl, char *name)
{
	uchar hash[HASHSZ];
	int n, i, rev;
	char *s;

	if(strcmp(name, "tip") == 0)
		return rl->nmap-1;
	rev = strtol(name, &s, 10);
	if(s > name && (*s == 0 || ispunct(*s)))
		return rev;
	rev = -1;
	if(s = strchr(name, '.'))
		name = s+1;
	if((n = hex2hash(name, hash)) > 0){
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
	Revtree* (*loadfn)(Revlog *, Revlog *, Revinfo *);
	char buf[MAXPATH], *sname;
	Revnode *nd;
	Revfile *rf;
	int i, level;

	if((fid->qid.type & QTDIR) == 0)
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
			closerevlog(rf->rlog);
			rf->rlog = nil;
			if((rf->node = rf->node->up) == rf->tree->root)
				rf->level = rf->tree->level;
			break;
		}
	} else {
		switch(rf->level){
		case Qroot:
			revlogupdate(&changelog);
			revlogupdate(&manifest);

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
			loadfn = nil;
			switch(i){
			case Qtree:
				goto Notfound;
			case Qfiles:
				loadfn = loadfilestree;
				break;
			case Qchanges:
				loadfn = loadchangestree;
				break;
			}
			if(loadfn){
				if((rf->tree = getrevtree(loadfn, rf->info)) == nil)
					goto Notfound;
				rf->node = rf->tree->root;
				rf->tree->level = i;
			}
			rf->level = i;
			break;
		case Qtree:
		case Qfiles:
		case Qchanges:
			i = 0;
			level = Qtree;
			sname = name;
		Searchtree:
			for(nd = rf->node->down; nd; nd = nd->next)
				if(strcmp(nd->name, sname) == 0)
					break;
			if(nd == nil){
				if(sname == name){
					sname = strrchr(name, '.');
					if(sname && (i = utfnlen(name, sname - name)) > 0){
						snprint(buf, sizeof(buf), "%.*s", i, name);
						sname++;
						if(strncmp(sname, "rev", 3) == 0){
							level = Qtreerev;
							sname += 3;
						}
						if(*sname == 0)
							i = 0;
						else {
							i = strtol(sname, &sname, 10);
							if(i < 0 || *sname != '\0')
								goto Notfound;
						}
						sname = buf;
						goto Searchtree;
					}
				}
				goto Notfound;
			}
			if(nd->hash){
				Revnode *nb;
				int j;

				if((rf->rlog = getrevlog(nd)) == nil)
					goto Notfound;
				j = hashrev(rf->rlog, nd->hash) - i;
				if(i < 0 || j < 0 || j >= rf->rlog->nmap){
					closerevlog(rf->rlog);
					rf->rlog = nil;
					goto Notfound;
				}
				for(nb = nd; nb; nb = nb->before)
					if(hashrev(rf->rlog, nb->hash) == j)
						break;
				if(nb == nil){
					nb = mknode(nd->name, revhash(rf->rlog, j), nd->mode);
					nb->up = nd->up;
					nb->before = nd->before;
					nd->before = nb;
				}
				nd = nb;
			} else if(name != sname)
				goto Notfound;
			rf->node = nd;
			rf->level = level;
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
		if(rf->rlog)
			incref(rf->rlog);
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
		closerevlog(rf->rlog);
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
	Revfile *rf;

	rf = r->fid->aux;
	switch(r->ifcall.mode & 3){
	case OEXEC:
		if(rf->node == nil || rf->node->mode != 'x')
			break;
	case OREAD:
		if(rf->level == Qlog){
			if((rf->fd = revlogopentemp(&changelog, hashrev(&changelog, rf->info->chash))) < 0){
				responderror(r);
				return;
			}
			rf->doff = rf->info->logoff;
		} else if(rf->level == Qtree && rf->node->down == nil){
			if((rf->fd = revlogopentemp(rf->rlog, hashrev(rf->rlog, rf->node->hash))) < 0){
				responderror(r);
				return;
			}
			rf->doff = fmetaheader(rf->fd);
		}
		respond(r, nil);
		return;
	}
	respond(r, "permission denied");
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
	char buf[MAXPATH], *s;
	Revfile *rf;
	int i, n;
	vlong off;
	int len;

	off = r->ifcall.offset;
	len = r->ifcall.count;

	rf = r->fid->aux;
	switch(rf->level){
	case Qroot:
		if(off == 0){
			revlogupdate(&changelog);
			revlogupdate(&manifest);
		}
		dirread9p(r, rootgen, nil);
		respond(r, nil);
		return;
	case Qrev:
		dirread9p(r, revgen, rf->info);
		respond(r, nil);
		return;
	case Qrev0:
	case Qrev1:
	case Qrev2:
		s = nil;
		if(rf->buf)
			goto Strgen;
		i = hashrev(&changelog, rf->info->chash);
		if(rf->level == Qrev1)
			i = changelog.map[i].p1rev;
		else if(rf->level == Qrev2)
			i = changelog.map[i].p2rev;
	Revgen:
		s = fsmkrevname(buf, sizeof(buf), i);
		goto Strgen;
	case Qtreerev:
		if((i = hashrev(rf->rlog, rf->node->hash)) >= 0)
			i = rf->rlog->map[i].linkrev;
		goto Revgen;
	case Qlog:
		if(off >= rf->info->loglen)
			len = 0;
		else if((off + len) >= rf->info->loglen)
			len = rf->info->loglen - off;
		goto Fdgen;
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
		if(rf->node->down){
	case Qfiles:
	case Qchanges:
			dirread9p(r, treegen, rf->node->down);
			respond(r, nil);
			return;
		}
	Fdgen:
		if(rf->fd < 0)
			break;
		if((n = pread(rf->fd, r->ofcall.data, len, off + rf->doff)) < 0){
			responderror(r);
			return;
		}
		r->ofcall.count = n;
		respond(r, nil);
		return;
	}
	respond(r, "bug in fsread");
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
	fprint(2, "usage: %s [-D] [-m mtpt] [-s srv] [root]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char *srv, *mtpt;
	char buf[MAXPATH];

	inflateinit();
	fmtinstall('H', Hfmt);

	srv = nil;
	mtpt = "/mnt/hg";

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

	if(getworkdir(workdir, *argv) < 0)
		sysfatal("can't find workdir: %r");

	snprint(buf, sizeof(buf), "%s/.hg/store/00changelog", workdir);
	if(revlogopen(&changelog, buf, OREAD) < 0)
		sysfatal("can't open changelog: %r\n");
	snprint(buf, sizeof(buf), "%s/.hg/store/00manifest", workdir);
	if(revlogopen(&manifest, buf, OREAD) < 0)
		sysfatal("can't open menifest: %r\n");

	postmountsrv(&fs, srv, mtpt, MREPL);

	exits(0);
}
