#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

enum {
	Qmainroot,
	Qadmroot,
	Qadmuser,
	Nreamqid,
};

static void
fillxdir(Xdir *d, vlong qid, char *name, int type, int mode)
{
	memset(d, 0, sizeof(Xdir));
	d->flag = 0;
	d->qid = (Qid){qid, 0, type};
	d->mode = mode;
	d->atime = 0;
	d->mtime = 0;
	d->length = 0;
	d->name = name;
	d->uid = -1;
	d->gid = -1;
	d->muid = 0;
}

static void
initadm(Blk *r, Blk *u, int nu)
{
	char *p, kbuf[Keymax], vbuf[Inlmax];
	Kvp kv;
	Xdir d;

	/* nb: values must be inserted in key order */
	kv.k = kbuf;
	kv.nk = Offksz;
	kv.v = vbuf;
	kv.nv = Ptrsz;
	kbuf[0] = Kdat;
	PACK64(kbuf+1, (uvlong)Qadmuser);
	PACK64(kbuf+9, 0ULL);
	packbp(kv.v, kv.nv, &u->bp);
	setval(r, &kv);

	fillxdir(&d, Qadmuser, "users", QTFILE, 0664);
	d.length = nu;
	dir2kv(Qadmroot, &d, &kv, vbuf, sizeof(vbuf));
	setval(r, &kv);
	fillxdir(&d, Qadmroot, "", QTDIR, DMDIR|0775);
	dir2kv(-1, &d, &kv, vbuf, sizeof(vbuf));
	setval(r, &kv);

	p = packsuper(kbuf, sizeof(kbuf), Qadmroot);
	kv.k = kbuf;
	kv.nk = p - kbuf;
	p = packdkey(vbuf, sizeof(vbuf), -1, "");
	kv.v = vbuf;
	kv.nv = p - vbuf;
	setval(r, &kv);
}

static void
initroot(Blk *r)
{
	char *p, kbuf[Keymax], vbuf[Inlmax];
	Kvp kv;
	Xdir d;

	/* nb: values must be inserted in key order */
	fillxdir(&d, Qmainroot, "", QTDIR, DMDIR|0775);
	dir2kv(-1, &d, &kv, vbuf, sizeof(vbuf));
	setval(r, &kv);

	p = packsuper(kbuf, sizeof(kbuf), Qmainroot);
	kv.k = kbuf;
	kv.nk = p - kbuf;
	p = packdkey(vbuf, sizeof(vbuf), -1, "");
	kv.v = vbuf;
	kv.nv = p - vbuf;
	setval(r, &kv);
}

static void
initsnap(Blk *s, Blk *r, Blk *a)
{
	char *p, *e, buf[Kvmax];
	Tree t;
	Kvp kv;

	lbl2kv("adm", 1, Lmut, &kv, buf, sizeof(buf));
	setval(s, &kv);
	lbl2kv("empty", 0, 0, &kv, buf, sizeof(buf));
	setval(s, &kv);
	lbl2kv("main", 2, Lmut, &kv, buf, sizeof(buf));
	setval(s, &kv);

	p = buf;
	e = p + sizeof(buf);

	/* empty */
	kv.k = p;
	p = packsnap(buf, e - p, 0);
	kv.nk = p - kv.k;
	kv.v = p;
	memset(&t, 0, sizeof(Tree));
	t.flag = 0;
	t.nref = 2;
	t.nlbl = 1;
	t.ht = 1;
	t.gen = fs->nextgen++;
	t.pred = 0;
	t.succ = 2;
	t.bp = r->bp;
	p = packtree(p, e - p, &t);
	kv.nv = p - kv.v;
	setval(s, &kv);

	p = buf;
	e = p + sizeof(buf);

	/* adm */
	kv.k = p;
	p = packsnap(p, e - p, 1);
	kv.nk = p - kv.k;
	kv.v = p;
	memset(&t, 0, sizeof(Tree));
	t.nref = 0;
	t.nlbl = 1;
	t.ht = 1;
	t.gen = fs->nextgen++;
	t.pred = 0;
	t.succ = -1;
	t.bp = a->bp;
	p = packtree(p, e - p, &t);
	kv.nv = p - kv.v;
	setval(s, &kv);

	p = buf;
	e = p + sizeof(buf);

	/* main */
	kv.k = p;
	p = packsnap(buf, e - p, 2);
	kv.nk = p - kv.k;
	kv.v = p;
	memset(&t, 0, sizeof(Tree));
	t.nref = 0;
	t.nlbl = 1;
	t.ht = 1;
	t.gen = fs->nextgen++;
	t.pred = 0;
	t.succ = -1;
	t.bp = r->bp;
	p = packtree(p, e - p, &t);
	kv.nv = p - kv.v;
	setval(s, &kv);
}

static void
initarena(Arena *a, uvlong hdaddr, vlong asz)
{
	Blk *b, *h0, *h1;
	uvlong addr;
	char *p;

	b = cachepluck();

	addr = hdaddr+2*Blksz;	/* leave room for arena hdr */

	a->loghd.addr = -1;
	a->loghd.hash = -1;
	a->loghd.gen = -1;

	memset(b->buf, 0, sizeof(b->buf));
	b->type = Tlog;
	b->bp.addr = addr;
	b->logsz = 0;
	b->logp = (Bptr){-1, -1, -1};
	b->data = b->buf + Loghdsz;
	setflag(b, Bdirty, 0);

	p = b->buf + Loghdsz;
	b->logp = (Bptr){-1, -1, -1};
	PACK64(p, addr|LogFree);	p += 8;	/* addr */
	PACK64(p, asz-2*Blksz);		p += 8;	/* len */
	PACK64(p, b->bp.addr|LogAlloc);	p += 8;	/* addr */
	PACK64(p, Blksz);		p += 8;	/* len */
	PACK64(p, (uvlong)LogSync);	p += 8;	/* barrier */
	b->logsz = p - b->data;
	finalize(b);
	syncblk(b);
	dropblk(b);

	a->loghd = b->bp;
	a->loghd.gen = -1;
	a->size = asz;
	a->used = Blksz;

	h0 = cachepluck();
	h1 = cachepluck();

	memset(h0->buf, 0, sizeof(h0->buf));
	h0->type = Tarena;
	h0->bp.addr = hdaddr;
	h0->data = h0->buf+2;
	packarena(h0->data, Arenasz, a);
	finalize(h0);
	syncblk(h0);
	a->h0 = h0;

	memset(h1->buf, 0, sizeof(h1->buf));
	h1->type = Tarena;
	h1->bp.addr = hdaddr+Blksz;
	h1->data = h1->buf+2;
	packarena(h1->data, Arenasz, a);
	finalize(h1);
	syncblk(h1);
	a->h1 = h1;
}

void
reamfs(char *dev)
{
	Blk *sb0, *sb1, *tb, *mb, *ab, *ub;
	vlong sz, asz, off;
	Mount *mnt, *adm;
	Arena *a;
	Tree *r;
	char *utab;
	Dir *d;
	int i;

	if(waserror())
		sysfatal("ream %s: %s", dev, errmsg());
	if((fs->fd = open(dev, ORDWR)) == -1)
		sysfatal("open %s: %r", dev);
	if((d = dirfstat(fs->fd)) == nil)
		sysfatal("ream: %r");
	sz = d->length;
	free(d);

	print("reaming %s\n", dev);
	if(sz < 128*MiB+Blksz)
		sysfatal("ream: disk too small");
	mnt = emalloc(sizeof(Mount), 1);
	aswapp(&mnt->root, mallocz(sizeof(Tree), 1));
	adm = mallocz(sizeof(Mount), 1);
	aswapp(&adm->root, mallocz(sizeof(Tree), 1));

	sz = sz - sz%Blksz - 2*Blksz;
	fs->narena = (sz + 4096ULL*GiB - 1) / (4096ULL*GiB);
	if(fs->narena < 8)
		fs->narena = 8;
	if(fs->narena >= 32)
		fs->narena = 32;
	fs->arenas = emalloc(fs->narena*sizeof(Arena), 1);


	off = Blksz;
	asz = sz/fs->narena;
	asz = asz - (asz % Blksz) - 2*Blksz;

	sb0 = cachepluck();
	sb1 = cachepluck();
	sb0->bp = (Bptr){0, -1, -1};
	sb1->bp = (Bptr){sz+Blksz, -1, -1};

	fs->arenabp = emalloc(fs->narena * sizeof(Bptr), 1);
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		print("\tarena %d: %lld blocks at %llx\n", i, asz/Blksz, off);
		initarena(a, off, asz);
		fs->arenabp[i] = a->h0->bp;
		off += asz+2*Blksz;

	}
	
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		loadarena(a, a->h0->bp);
		loadlog(a, a->loghd);
	}

	if((mb = newblk(agetp(&mnt->root), Tleaf)) == nil)
		sysfatal("ream: allocate root: %r");
	holdblk(mb);
	initroot(mb);
	finalize(mb);
	syncblk(mb);

	r = agetp(&mnt->root);
	r->ht = 1;
	r->bp = mb->bp;

	r = agetp(&adm->root);
	if((ab = newblk(r, Tleaf)) == nil)
		sysfatal("ream: allocate root: %r");
	if((ub = newdblk(r, 0, 1)) == nil)
		sysfatal("ream: allocate root: %r");
	holdblk(ab);
	holdblk(ub);
	utab = smprint(
		"-1:adm::%s\n"
		"0:none::\n"
		"1:%s:%s:\n",
		reamuser, reamuser, reamuser);
	memcpy(ub->data, utab, strlen(utab));
	finalize(ub);
	syncblk(ub);
	initadm(ab, ub, strlen(utab));
	finalize(ab);
	syncblk(ab);

	r->ht = 1;
	r->bp = ab->bp;

	/*
	 * Now that we have a completely empty fs, give it
	 * a single snap block that the tree will insert
	 * into, and take a snapshot as the initial state.
	 */
	if((tb = newblk(agetp(&mnt->root), Tleaf)) == nil)
		sysfatal("ream: allocate snaps: %r");
	holdblk(tb);
	initsnap(tb, mb, ab);
	finalize(tb);
	syncblk(tb);

	fs->snap.bp = tb->bp;
	fs->snap.ht = 1;
	fs->snapdl.hd.addr = -1;
	fs->snapdl.hd.hash = -1;
	fs->snapdl.tl.addr = -1;
	fs->snapdl.tl.hash = -1;

	dropblk(mb);
	dropblk(ab);
	dropblk(ub);
	dropblk(tb);
	fs->nextqid = Nreamqid;

	/*
	 * We need to write back all of the arenas
	 * with the updated free lists
	 */
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		finalize(a->logtl);
		syncblk(a->logtl);
		packarena(a->h0->data, Blksz, a);
		finalize(a->h0);
		syncblk(a->h0);
		packarena(a->h1->data, Blksz, a);
		finalize(a->h1);
		syncblk(a->h1);
		fs->arenabp[i] = a->h0->bp;
		dropblk(a->h0);
		dropblk(a->h1);
	}

	dropblk(mb);
	dropblk(ab);
	dropblk(ub);
	dropblk(tb);

	/*
	 * Finally, write back the superblock and backup
	 * superblock.
	 */
	packsb(sb0->buf, Blksz, fs);
	packsb(sb1->buf, Blksz, fs);
	finalize(sb0);
	finalize(sb1);
	syncblk(sb0);
	syncblk(sb1);
	dropblk(sb0);
	dropblk(sb1);
	free(mnt);
	poperror();
}

void
growfs(char *dev)
{
	vlong oldsz, newsz, asz, off, eb;
	int i, narena;
	Arena *a;
	Bptr bp;
	Dir *d;

	if(waserror())
		sysfatal("grow %s: %s", dev, errmsg());
	if((fs->fd = open(dev, ORDWR)) == -1)
		sysfatal("open %s: %r", dev);
	if((d = dirfstat(fs->fd)) == nil)
		sysfatal("ream: %r");

	bp = (Bptr){0, -1, -1};
	fs->sb0 = getblk(bp, GBnochk);
	unpacksb(fs, fs->sb0->buf, Blksz);
	if((fs->arenas = calloc(fs->narena, sizeof(Arena))) == nil)
		sysfatal("malloc: %r");
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		loadarena(a, fs->arenabp[i]);
		fs->arenabp[i] = a->h0->bp;
	}
	a = &fs->arenas[fs->narena-1];
	oldsz = a->h0->bp.addr + a->size + 2*Blksz;
	newsz = d->length - d->length%Blksz - 2*Blksz;
	if(newsz - oldsz < 64*MiB)
		sysfatal("new arenas too small (%lld < %lld), not growing", newsz - oldsz, 64*MiB);
	asz = (newsz - oldsz)/4;
	asz = asz - asz % Blksz - 2*Blksz;
	narena = fs->narena + 4;
	assert(oldsz % Blksz == 0);
	if((fs->arenas = realloc(fs->arenas, narena*sizeof(Arena))) == nil)
		error(Enomem);
	if((fs->arenabp = realloc(fs->arenabp, narena*sizeof(Bptr))) == nil)
		error(Enomem);

	off = oldsz;
	for(i = fs->narena; i < narena; i++){
		a = &fs->arenas[i];
		print("\tnew arena %d: adding %lld blocks at %llx\n", i, asz/Blksz, off);
		initarena(&fs->arenas[i], off, asz);
		loadarena(a, a->h0->bp);
		loadlog(a, a->loghd);
		a = &fs->arenas[i];
		packarena(a->h0->data, Blksz, a);
		packarena(a->h1->data, Blksz, a);
		finalize(a->h0);
		finalize(a->h1);
		syncblk(a->h0);
		syncblk(a->h1);

		fs->arenabp[i] = a->h0->bp;
		off += asz+2*Blksz;
	}
	fs->narena = narena;
	packsb(fs->sb0->buf, Blksz, fs);
	finalize(fs->sb0);
	syncblk(fs->sb0);
	/*
	 * We're being a bit tricksy here: because we're on a bigger
	 * partition, we don't know where the end is; just load the
	 * first block, and patch the address in to the right place
	 * when we write it back.
	 */
	eb = d->length;
	eb = eb - (eb%Blksz) - Blksz;
	fs->sb0->bp = (Bptr){eb, -1, -1};
	packsb(fs->sb0->buf, Blksz, fs);
	finalize(fs->sb0);
	syncblk(fs->sb0);
	free(d);
	poperror();
}
