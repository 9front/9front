#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static int
rangecmp(Avl *a, Avl *b)
{
	if(((Arange*)a)->off < ((Arange*)b)->off)
		return -1;
	if(((Arange*)a)->off > ((Arange*)b)->off)
		return 1;
	return 0;
}

void
loadarena(Arena *a, Bptr hd)
{
	Blk *h0, *h1, *b;
	Bptr bp;

	/* try to load block pointers, we only need one to succeed */
	bp = hd;
	h0 = getblk(bp, GBtry);
	bp.addr += Blksz;
	h1 = getblk(bp, GBtry);
	/* if neither head nor tail is consistent, we're hosed */
	b = (h0 != nil) ? h0 : h1;
	if(b == nil)
		error(Efs);
	if(h0 == nil || h1 == nil)
		fprint(2, "using fallback arena header %B\n", b->bp);


	/* otherwise, we could have crashed mid-pass, just load the blocks */
	bp = hd;
	if(h0 == nil)
		h0 = getblk(bp, GBnochk);
	bp.addr += Blksz;
	if(h1 == nil)
		h1 = getblk(bp, GBnochk);

	unpackarena(a, b->data, Arenasz);
	if((a->free = avlcreate(rangecmp)) == nil)
		error(Enomem);
	a->logbuf[0] = cachepluck();
	a->logbuf[1] = cachepluck();
	a->logbuf[0]->bp = (Bptr){-1, -1, -1};
	a->logbuf[1]->bp = (Bptr){-1, -1, -1};
	setflag(a->logbuf[0], Bstatic, 0);
	setflag(a->logbuf[1], Bstatic, 0);
	a->h0 = h0;
	a->h1 = h1;
	a->used = a->size;
}

void
loadfs(char *dev)
{
	Bptr bhd, btl;
	Mount *dump;
	Arena *a;
	Tree *t;
	Dir *d;
	int i;
	vlong eb;

	if((dump = mallocz(sizeof(*dump), 1)) == nil)
		sysfatal("malloc: %r");
	if(waserror())
		sysfatal("load fs: %s", errmsg());
	snprint(dump->name, sizeof(dump->name), "dump");
	aswapl(&dump->ref, 1);
	dump->gen = -1;
	aswapp(&dump->root, &fs->snap);

	fs->snapmnt = dump;
	fs->narena = 1;
	if((fs->fd = open(dev, ORDWR)) == -1)
		sysfatal("open %s: %r", dev);
	if((d = dirfstat(fs->fd)) == nil)
		sysfatal("stat %s: %r", dev);
	eb = d->length;
	eb = eb - (eb%Blksz) - Blksz;
	bhd = (Bptr){0, -1, -1};
	btl = (Bptr){eb, -1, -1};
	fs->sb0 = getblk(bhd, GBnochk);
	fs->sb1 = getblk(btl, GBnochk);
	if(!waserror()){
		unpacksb(fs, fs->sb0->buf, Blksz);
		poperror();
	}else{
		fprint(2, "unable to load primary superblock: %s\n", errmsg());
		if(waserror()){
			fprint(2, "unable to load primary superblock: %s\n", errmsg());
			exits("corrupt");
		}
		unpacksb(fs, fs->sb1->buf, Blksz);
		poperror();
	}

	if((fs->arenas = calloc(fs->narena, sizeof(Arena))) == nil)
		sysfatal("malloc: %r");
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		loadarena(a, fs->arenabp[i]);
		a->reserve = a->size / 1024;
		if(a->reserve < 512*KiB)
			a->reserve = 512*KiB;
		if(a->reserve > 8*MiB)
			a->reserve = 8*MiB;
	}
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		loadlog(a, a->loghd);
	}

	if((t = opensnap("adm", nil)) == nil)
		sysfatal("load users: no adm label");
	loadusers(2, t);
	poperror();

	fprint(2, "load %s:\n", dev);
	fprint(2, "\tsnaptree:\t%B\n", fs->snap.bp);
	fprint(2, "\tnarenas:\t%d\n", fs->narena);
	fprint(2, "\tfeatures:\t%lld\n", fs->flag);
	fprint(2, "\tnextqid:\t%lld\n", fs->nextqid);
	fprint(2, "\tlastqgen:\t%lld\n", agetv(&fs->qgen));
	fprint(2, "\tnextgen:\t%lld\n", fs->nextgen);
	fprint(2, "\tblocksize:\t%lld\n", Blksz);
	fprint(2, "\tcachesz:\t%lld MiB\n", fs->cmax*Blksz/MiB);
	closesnap(t);
}
