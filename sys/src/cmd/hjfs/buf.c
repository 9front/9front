#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Channel *getb, *putb, *syncb;
Buf bfree;
BufReq *freereq, *freereqlast;

static void
markbusy(Buf *b)
{
	b->busy = 1;
	if(b->fnext != nil){
		b->fnext->fprev = b->fprev;
		b->fprev->fnext = b->fnext;
		b->fnext = b->fprev = nil;
	}
}

static void
markfree(Buf *b)
{
	b->busy = 0;
	b->fnext = &bfree;
	b->fprev = bfree.fprev;
	b->fnext->fprev = b;
	b->fprev->fnext = b;
}

static void
changedev(Buf *b, Dev *d, uvlong off)
{
	if(b->dnext != nil){
		b->dnext->dprev = b->dprev;
		b->dprev->dnext = b->dnext;
		b->dprev = nil;
		b->dnext = nil;
	}
	b->off = off;
	b->d = d;
	if(d != nil){
		b->dnext = &d->buf[b->off & BUFHASH];
		b->dprev = b->dnext->dprev;
		b->dnext->dprev = b;
		b->dprev->dnext = b;
	}
}

static void
delayreq(BufReq req, BufReq **first, BufReq **last)
{
	BufReq *r;

	r = emalloc(sizeof(*r));
	memcpy(r, &req, sizeof(*r));
	r->next = nil;
	if(*first == nil)
		*first = *last = r;
	else{
		(*last)->next = r;
		*last = r;
	}
}

static void
work(Dev *d, Buf *b)
{
	qlock(&d->workl);
	b->wnext = &d->work;
	b->wprev = b->wnext->wprev;
	b->wnext->wprev = b;
	b->wprev->wnext = b;
	rwakeup(&d->workr);
	qunlock(&d->workl);
}

static void
givebuf(BufReq req, Buf *b)
{
	Buf *c, *l;

	markbusy(b);
	if(req.d == b->d && req.off == b->off){
		send(req.resp, &b);
		return;
	}
	if(b->op & BDELWRI){
		b->op &= ~BDELWRI;
		b->op |= BWRITE;
		delayreq(req, &b->next, &b->last);
		b->resp = putb;
		work(b->d, b);
		return;
	}
	l = &req.d->buf[req.off & BUFHASH];
	for(c = l->dnext; c != l; c = c->dnext)
		if(c->off == req.off)
			abort();
	changedev(b, req.d, req.off);
	b->op &= ~(BWRITE|BDELWRI|BWRIM);
	if(req.nodata)
		send(req.resp, &b);
	else{
		b->resp = req.resp;
		work(b->d, b);
	}
}

static void
undelayreq(Buf *b, BufReq **first, BufReq **last)
{
	BufReq *r;
	
	r = *first;
	*first = r->next;
	if(*last == r)
		*last = nil;
	givebuf(*r, b);
	free(r);
}

static void
handleget(BufReq req)
{
	Buf *b, *l;
	Dev *d;
	
	d = req.d;
	l = &d->buf[req.off & BUFHASH];
	for(b = l->dnext; b != l; b = b->dnext)
		if(b->off == req.off){
			if(b->busy){
				delayreq(req, &b->next, &b->last);
				return;
			}
			givebuf(req, b);
			return;
		}
	if(bfree.fnext == &bfree){
		delayreq(req, &freereq, &freereqlast);
		return;
	}
	b = bfree.fnext;
	givebuf(req, b);
}

static void
handleput(Buf *b)
{
	if(b->op & BWRIM){
		b->op &= ~(BWRIM | BDELWRI);
		b->op |= BWRITE;
		b->resp = putb;
		work(b->d, b);
		return;
	}
	if(b->error != nil){
		b->error = nil;
		b->op &= ~BDELWRI;
		changedev(b, nil, -1);
	}
	b->op &= ~BWRITE;
	markfree(b);
	if(b->next != nil)
		undelayreq(b, &b->next, &b->last);
	else if(freereq != nil)
		undelayreq(b, &freereq, &freereqlast);
}

static void
handlesync(Channel *resp)
{
	Buf *b, *c;

	for(b = bfree.fnext; b != &bfree; b = c){
		c = b->fnext;
		if(b->d != nil && b->op & BDELWRI){
			markbusy(b);
			b->resp = putb;
			b->op &= ~BDELWRI;
			b->op |= BWRITE;
			work(b->d, b);
		}
	}
	if(resp != nil)
		sendp(resp, nil);
}

static void
bufproc(void *)
{
	BufReq req;
	Buf *buf;
	Channel *r;
	Alt a[] = {{getb, &req, CHANRCV}, {putb, &buf, CHANRCV}, {syncb, &r, CHANRCV}, {nil, nil, CHANEND}};

	workerinit();
	for(;;)
		switch(alt(a)){
		case 0:
			handleget(req);
			break;
		case 1:
			handleput(buf);
			break;
		case 2:
			handlesync(r);
			break;
		case -1:
			sysfatal("alt: %r");
		}
}

static char *
typenames[] = {
	[TRAW] "raw",
	[TSUPERBLOCK] "superblock",
	[TDENTRY] "dentry",
	[TINDIR] "indir",
	[TREF] "ref",
	nil
};

static int
Tfmt(Fmt *f)
{
	int t;
	
	t = va_arg(f->args, uint);
	if(t >= nelem(typenames) || typenames[t] == nil)
		return fmtprint(f, "??? (%d)", t);
	return fmtstrcpy(f, typenames[t]);
}

void
bufinit(int nbuf)
{
	Buf *b;

	fmtinstall('T', Tfmt);
	b = emalloc(sizeof(*b) * nbuf);
	bfree.fnext = bfree.fprev = &bfree;
	while(nbuf--)
		markfree(b++);
	getb = chancreate(sizeof(BufReq), 0);
	putb = chancreate(sizeof(Buf *), 32);
	syncb = chancreate(sizeof(ulong), 0);
	proccreate(bufproc, nil, mainstacksize);
}

Buf *
getbuf(Dev *d, uvlong off, int type, int nodata)
{
	ThrData *th;
	BufReq req;
	Buf *b;

	if(off >= d->size)
		abort();
	th = getthrdata();
	req.d = d;
	req.off = off;
	req.resp = th->resp;
	req.nodata = nodata;
	send(getb, &req);
	recv(th->resp, &b);
	if(b->error != nil){
		werrstr("%s", b->error);
		putbuf(b);
		return nil;
	}
	if(nodata)
		b->type = type;
	if(b->type != type && type != -1){
		dprint("hjfs: type mismatch, dev %s, block %lld, got %T, want %T, caller %#p\n",
			d->name, off, b->type, type, getcallerpc(&d));
		werrstr("phase error -- type mismatch");
		putbuf(b);
		return nil;
	}
	b->callerpc = getcallerpc(&d);
	return b;
}

void
putbuf(Buf *b)
{
	send(putb, &b);
}

void
sync(int wait)
{
	Channel *r;
	Dev *d;
	Buf b;

	r = nil;
	if(wait)
		r = getthrdata()->resp;
	sendp(syncb, r);
	memset(&b, 0, sizeof(Buf));
	if(wait){
		recvp(r);
		for(d = devs; d != nil; d = d->next){
			b.d = nil;
			b.resp = r;
			b.busy = 1;
			work(d, &b);
			recvp(r);
		}
	}
}
