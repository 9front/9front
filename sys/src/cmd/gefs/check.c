#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static int
isfree(vlong bp)
{
	Arange *r, q;
	Arena *a;

	q.off = bp;
	q.len = Blksz;

	a = getarena(bp);
	r = (Arange*)avllookup(a->free, &q, -1);
	if(r == nil)
		return 0;
	return bp < (r->off + r->len);
}

static int
checktree(int fd, Blk *b, int h, vlong pred, Kvp *lo, Kvp *hi)
{
	Kvp x, y;
	Msg mx, my;
	int i, r, fill;
	Blk *c;
	int fail;
	Bptr bp;

	fail = 0;
	if(h < 0){
		fprint(fd, "node too deep (loop?\n");
		fail++;
		return fail;
	} 
	if(b->type == Tleaf){
		if(h != 0){
			fprint(fd, "unbalanced leaf\n");
			fail++;
		}
		if(h != 0 && b->nval < 2 && debug){
			fprint(fd, "warning: underfilled leaf %B\n", b->bp);
			fail++;
		}
	}
	if(b->type == Tpivot && b->nval < 2 && debug)
		fprint(fd, "warning: underfilled pivot %B\n", b->bp);
	getval(b, 0, &x);
	if(lo && keycmp(lo, &x) > 0){
		fprint(fd, "out of range keys %P != %P\n", lo, &x);
		showblk(fd, b, "out of range", 1);
		fail++;
	}
	for(i = 1; i < b->nval; i++){
		getval(b, i, &y);
		if(hi && keycmp(&y, hi) >= 0){
			fprint(fd, "out of range keys %P >= %P\n", &y, hi);
			fail++;
		}
		if(b->type == Tpivot){
			bp = getptr(&x, &fill);
			if(bp.gen <= pred)
				goto Skip;
			if(isfree(bp.addr)){
				fprint(fd, "freed block in use: %llx\n", bp.addr);
				fail++;
			}
			if((c = getblk(bp, 0)) == nil){
				fprint(fd, "corrupt block: %B\n", bp);
				fail++;
				continue;
			}
			if(blkfill(c) != fill){
				fprint(fd, "mismatched block fill\n");
				fail++;
			}
			if(checktree(fd, c, h - 1, pred, &x, &y))
				fail++;
			dropblk(c);
		}
Skip:
		r = keycmp(&x, &y);
		switch(r){
		case -1:
			break;
		case 0:
			fprint(fd, "duplicate keys %P, %P\n", &x, &y);
			fail++;
			break;
		case 1:
			fprint(fd, "misordered keys %P, %P\n", &x, &y);
			fail++;
			break;
		}
		x = y;
	}
	if(b->type == Tpivot){
		getval(b, b->nval-1, &y);
		bp = getptr(&x, &fill);
		if(bp.gen > pred){
			if((c = getblk(bp, 0)) == nil){
				fprint(fd, "corrupt block: %B\n", bp);
				fail++;
			}
			if(c != nil && checktree(fd, c, h - 1, pred, &y, nil))
				fail++;
			dropblk(c);
		}
		if(b->nbuf > 0){
			getmsg(b, 0, &mx);
			if(hi && keycmp(&mx, hi) >= 0){
				fprint(fd, "out of range messages %P != %M\n", hi, &mx);
				fail++;
			}
		}
		for(i = 1; i < b->nbuf; i++){
			getmsg(b, i, &my);
			switch(my.op){
			case Owstat:		/* kvp dirent */
				if((my.v[0] & ~(Owsize|Owmode|Owmtime|Owatime|Owuid|Owgid|Owmuid)) != 0){
					fprint(fd, "invalid stat op %x\n", my.v[0]);
					fail++;
				}
				break;
			default:
				if(my.op <= 0 || my.op >= Nmsgtype){
					fprint(fd, "invalid message op %d\n", my.op);
					fail++;
				}
				break;
			}
			if(hi && keycmp(&y, hi) > 0){
				fprint(fd, "out of range keys %P >= %P\n", &y, hi);
				fail++;
			}
			if(keycmp(&mx, &my) == 1){
				fprint(fd, "misordered keys %P, %P\n", &x, &y);
				fail++;
				break;
			}
			mx = my;
		}

	}
	return fail;
}

static int
checklog(int fd, Bptr hd, Bptr tl)
{
	Bptr pb, bp, nb;
	Blk *b;
	
	pb = Zb;
	for(bp = hd; bp.addr != -1; bp = nb){
		if(waserror()){
			fprint(fd, "error loading %B\n", bp);
			return 0;
		}
		pb = bp;
		b = getblk(bp, 0);
		nb = b->logp;
		dropblk(b);
		poperror();
		if(bp.addr == tl.addr)
			break;
	}
	if(tl.addr != -1 && pb.addr != tl.addr){
		fprint(fd, "truncated chain %B\n", hd);
		return 0;
	}
	return 1;
}

static int
checkfree(int fd)
{
	Arena *a;
	Arange *r, *n;
	int i, ok;

	ok = 1;
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		qlock(a);
		r = (Arange*)avlmin(a->free);
		for(n = (Arange*)avlnext(r); n != nil; n = (Arange*)avlnext(n)){
			if(r->off >= n->off){
				fprint(fd, "misordered length %llx >= %llx\n", r->off, n->off);
				ok = 0;
			}
			if(r->off+r->len >= n->off){
				fprint(fd, "overlaping range %llx+%llx >= %llx\n", r->off, r->len, n->off);
				ok = 0;
			}
			r = n;
		}
		if(!checklog(fd, a->loghd, Zb)){
			fprint(fd, "arena %d: broken freelist\n", i);
			ok = 0;
		}
		qunlock(a);
	}
	return ok;
}

static int
checkdlist(int fd)
{
	char pfx[1];
	Dlist dl;
	Scan s;
	int ok;

	ok = 1;
	if(!checklog(fd, fs->snapdl.hd, fs->snapdl.tl)){
		fprint(fd, "bad snap dlist (%B, %B): %s\n", fs->snapdl.hd, fs->snapdl.tl, errmsg());
		ok = 0;
	}
	pfx[0] = Kdlist;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		kv2dlist(&s.kv, &dl);
		if(!checklog(fd, dl.hd, dl.tl)){
			fprint(fd, "bad dlist %P: %s\n", &s.kv, errmsg());
			ok = 0;
		}
	}
	btexit(&s);
	return ok;
}

static int
checkdata(int fd, Tree *t, vlong pred)
{
	char pfx[1];
	Bptr bp;
	Scan s;
	Blk *b;

	pfx[0] = Kdat;
	btnewscan(&s, pfx, 1);
	btenter(t, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		if(waserror()){
			btexit(&s);
			nexterror();
		}
		bp = unpackbp(s.kv.v, s.kv.nv);
		if(bp.gen > pred){
			if(isfree(bp.addr)){
				fprint(fd, "free block in use: %B\n", bp);
				error("free block in use");
			}
			b = getblk(bp, GBraw);
			dropblk(b);
		}
		poperror();
	}
	btexit(&s);
	return 0;
}

static vlong
countlbl(int fd, vlong id)
{
	char pfx[1];
	int nref;
	Scan s;

	nref = 0;
	pfx[0] = Klabel;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		if(s.kv.nv >= 9 && UNPACK64(s.kv.v+1) == id){
			fprint(fd, "\tlabel %.*s => %lld\n", (int)(s.kv.nk-1), s.kv.k+1, UNPACK64(s.kv.v+1));
			nref++;
		}
	}
	btexit(&s);
	return nref;
}

static vlong
countref(vlong id)
{
	char pfx[1];
	Tree t;
	int nref;
	Scan s;

	nref = 0;
	pfx[0] = Ksnap;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		unpacktree(&t, s.kv.v, s.kv.nv);
		if(t.pred == -1 && t.base == id)
			nref++;
	}
	btexit(&s);
	return nref;
}

int
checkfs(int fd)
{
	int ok, height, nref, nlbl;
	vlong gen;
	char pfx[1];
	Tree *t;
	Scan s;
	Blk *b;

	ok = 1;
	assert(!canqlock(&fs->mutlk));
	if(waserror()){
		fprint(fd, "error checking %s\n", errmsg());
		return 0;
	}
	fprint(fd, "checking freelist\n");
	if(!checkfree(fd))
		ok = 0;
	fprint(fd, "checking deadlist\n");
	if(!checkdlist(fd))
		ok = 0;
	fprint(fd, "checking snap tree: %B\n", fs->snap.bp);
	if((b = getroot(&fs->snap, &height)) != nil){
		if(checktree(fd, b, height-1, -1, nil, 0))
			ok = 0;
		dropblk(b);
	}
	pfx[0] = Ksnap;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		gen = UNPACK64(s.kv.k+1);
		if(waserror()){
			fprint(fd, "moving on: %s\n", errmsg());
			ok = 0;
			continue;
		}
		if((t = opentree(gen)) == nil){
			fprint(fd, "invalid snap id %lld\n", gen);
			ok = 0;
			poperror();
			continue;
		}
		fprint(fd, "checking snap %lld: %B\n", gen, t->bp);
		if(waserror()){
			closesnap(t);
			nexterror();
		}
		nref = countref(gen);
		nlbl = countlbl(fd, gen);
		fprint(fd, "\tnref %d nlbl %d\n", nref, nlbl);
		if(t->nref < nref || t->nlbl < nlbl){
			fprint(fd, "mismatched refs: (%d, %d) != (%d, %d)\n", t->nref, nref, t->nlbl, nlbl);
			ok = 0;
		}
		b = getroot(t, &height);
		if(waserror()){
			dropblk(b);
			nexterror();
		}
		if(checktree(fd, b, height-1, t->pred, nil, 0))
			ok = 0;
		if(checkdata(fd, t, t->pred))
			ok = 0;
		dropblk(b);
		poperror();
		closesnap(t);
		poperror();
		poperror();
	}
	btexit(&s);
	poperror();
	if(ok)
		fprint(fd, "ok\n");
	else
		fprint(fd, "not ok\n");
	return ok;
}
