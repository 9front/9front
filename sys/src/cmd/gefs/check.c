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
checktree(int fd, Blk *b, int h, Kvp *lo, Kvp *hi)
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
		if(h != 0 && b->nval < 2){
			fprint(fd, "warning: underfilled leaf %B\n", b->bp);
			fail++;
		}
	}
	if(b->type == Tpivot && b->nval < 2)
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
			if(checktree(fd, c, h - 1, &x, &y))
				fail++;
			dropblk(c);
		}
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
		if((c = getblk(bp, 0)) == nil){
			fprint(fd, "corrupt block: %B\n", bp);
			fail++;
		}
		if(c != nil && checktree(fd, c, h - 1, &y, nil))
			fail++;
		dropblk(c);
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
checklog(int fd, Bptr hd)
{
	Bptr bp, nb;
	Blk *b;

	bp = (Bptr){-1, -1, -1};
	for(bp = hd; bp.addr != -1; bp = nb){
		if(waserror()){
			fprint(fd, "error loading %B\n", bp);
			return 0;
		}
		b = getblk(bp, 0);
		nb = b->logp;
		dropblk(b);
		poperror();
	}
	return 1;
}

static int
checkfree(int fd)
{
	Arena *a;
	Arange *r, *n;
	int i, fail;

	fail = 0;
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		qlock(a);
		r = (Arange*)avlmin(a->free);
		for(n = (Arange*)avlnext(r); n != nil; n = (Arange*)avlnext(n)){
			if(r->off >= n->off){
				fprint(2, "misordered length %llx >= %llx\n", r->off, n->off);
				fail++;
			}
			if(r->off+r->len >= n->off){
				fprint(2, "overlaping range %llx+%llx >= %llx\n", r->off, r->len, n->off);
				fail++;
			}
			r = n;
		}
		if(!checklog(fd, a->loghd))
			fprint(fd, "arena %d: broken freelist\n", i);
		qunlock(a);
	}
	return fail;
}

static int
checkdlist(int fd)
{
	char pfx[1];
	Dlist dl;
	Scan s;

	checklog(fd, fs->snapdl.hd);
	pfx[0] = Kdlist;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		kv2dlist(&s.kv, &dl);
		if(!checklog(fd, dl.hd))
			fprint(fd, "bad dlist %P: %s\n", &s.kv, errmsg());
	}
	btexit(&s);
	return 0;
}

static int
checkdata(int, Tree *t)
{
	char pfx[1];
	Bptr bp;
	Scan s;
	Blk *b;

	pfx[0] = Klabel;
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
		if(isfree(bp.addr)){
			fprint(2, "free block in use: %B\n", bp);
			error("free block in use");
		}
		b = getblk(bp, GBraw);
		dropblk(b);
		poperror();
	}
	btexit(&s);
	return 0;
}

int
checkfs(int fd)
{
	int ok, height;
	char pfx[1], name[Keymax+1];
	Tree *t;
	Scan s;
	Blk *b;

	ok = 1;
	epochwait();
	qlock(&fs->mutlk);
	if(waserror()){
		fprint(fd, "error checking %s\n", errmsg());
		return 0;
	}
	fprint(fd, "checking freelist\n");
	if(checkfree(fd))
		ok = 0;
	fprint(fd, "checking deadlist\n");
	if(checkdlist(fd))
		ok = 0;
	fprint(fd, "checking snap tree: %B\n", fs->snap.bp);
	if((b = getroot(&fs->snap, &height)) != nil){
		if(checktree(fd, b, height-1, nil, 0))
			ok = 0;
		dropblk(b);
	}
	pfx[0] = Klabel;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		if(waserror()){
			fprint(fd, "moving on: %s\n", errmsg());
			continue;
		}
		memcpy(name, s.kv.k+1, s.kv.nk-1);
		name[s.kv.nk-1] = 0;
		if((t = opensnap(name, nil)) == nil){
			fprint(2, "invalid snap label %s\n", name);
			ok = 0;
			poperror();
			break;
		}
		if(waserror()){
			closesnap(t);
			nexterror();
		}
		fprint(fd, "checking snap %s: %B\n", name, t->bp);
		b = getroot(t, &height);
		if(waserror()){
			dropblk(b);
			nexterror();
		}
		if(checktree(fd, b, height-1, nil, 0))
			ok = 0;
		if(checkdata(fd, t))
			ok = 0;
		dropblk(b);
		poperror();
		closesnap(t);
		poperror();
		poperror();
	}
	btexit(&s);
	qunlock(&fs->mutlk);
	poperror();
	return ok;
}
