#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

extern Fs *fsmain;

static void
checkdir(FLoc *l, Buf *b)
{
	Buf *c;
	Dentry *d;
	uvlong i, r;

	d = getdent(l, b);
	for(i = 0; i < d->size; i++){
		if(getblk(fsmain, l, b, i, &r, GBREAD) <= 0) {
			dprint("hjfs: directory in block %ulld at index %d has a bad block %ulld at directory index %ulld\n", l->blk, l->deind, r, i);
			continue;
		}
		c = getbuf(fsmain->d, r, TDENTRY, 0);
		if(c == nil) {
			dprint("hjfs: directory in block %ulld at index %d has a block %ulld at directory index %ulld that is not a directory entry\n", l->blk, l->deind, r, i);
			continue;
		}
		if(chref(fsmain, r, 0) == 0)
			dprint("hjfs: directory in block %ulld at index %d has a block %ulld at index %ulld whose reference count is 0");
	}
}

static void
checkfile(FLoc*, Buf*)
{}

int
checkblk(uvlong blk)
{
	Dentry *d;
	Buf *b;
	FLoc l;
	int i, type;

	b = getbuf(fsmain->d, blk, TDONTCARE, 0);
	if(b == nil)
		return -1;
	switch(type = b->type){
	case TRAW:
		break;
	case TSUPERBLOCK:
		dprint("hjfs: checkblk: should not have found superblock at %ulld\n", blk);
		break;
	case TDENTRY:
		l.blk = blk;
		for(i = 0; i < DEPERBLK; i++){
			d = &b->de[i];
			l.deind = i;
			l.Qid = d->Qid;
			if((d->type & QTDIR) != 0)
				checkdir(&l, b);
			else
				checkfile(&l, b);
		}
		break;
	case TINDIR:
		break;
	case TREF:
		break;
	}
	putbuf(b);
	return type;
}
