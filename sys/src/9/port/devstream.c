#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

typedef struct Stream Stream;
typedef struct Iocom Iocom;

struct Stream
{
	Ref;
	Lock;

	int	iounit;
	int	noseek;

	Ref	nrp;
	Ref	nwp;
	Ref	nwq;

	Proc	*rp[4];
	Proc	*wp[2];

	Block	*rlist;

	vlong	soff;
	vlong	roff;
	vlong	woff;
	
	QLock	rcl;
	QLock	wcl;
	QLock	rql;
	QLock	wql;

	Rendez	wz;

	Queue	*rq;
	Queue	*wq;
	Chan	*f;
};

struct Iocom
{
	Proc	*p;
	QLock	*q;
	Stream	*s;
	Block	*b;
};

static void
putstream(Stream *s)
{
	if(decref(s))
		return;
	freeblist(s->rlist);
	qfree(s->rq);
	qfree(s->wq);
	if(s->f != nil)
		cclose(s->f);
	free(s);
}

#define BOFF(b)		(*(vlong*)((b)->rp - sizeof(vlong)))
#define BDONE		(1<<15)
#define BERROR		(1<<14)

static Block*
sblock(Stream *s)
{
	Block *b;

	b = allocb(sizeof(vlong)+s->iounit);
	b->flag &= ~(BDONE|BERROR);
	b->wp += sizeof(vlong);
	b->rp = b->wp;
	return b;
}

static void
iocom(void *arg, int complete)
{
	Iocom *io = arg;
	Stream *s;
	QLock *q;
	Proc *p;

	p = io->p;
	if(complete && p == up){
		up->iocomfun = nil;
		up->iocomarg = nil;
	}

	q = io->q;
	if(q != nil && p == up){
		io->q = nil;
		qunlock(q);
	}

	s = io->s;
	if(complete && s != nil && s->noseek){
		io->s = nil;
		lock(s);
		BOFF(io->b) = s->soff;
		s->soff += s->iounit;
		unlock(s);
	}
}

static void
ioq(Iocom *io, QLock *q)
{
	eqlock(q);	/* unlocked in iocom() above */

	io->p = up;
	io->q = q;
	io->s = nil;
	io->b = nil;

	up->iocomarg = io;
	up->iocomfun = iocom;
}

static void
streamreader(void *arg)
{
	Stream *s = arg;
	Iocom io;
	Chan *f;
	Block *b, *l, **ll;
	vlong o;
	int id, n;

	id = incref(&s->nrp) % nelem(s->rp);
	s->rp[id] = up;

	f = s->f;
	b = sblock(s);
	qlock(&s->rql);
	if(waserror()){
		qhangup(s->rq, up->errstr);
		goto Done;
	}
	if(s->noseek == -1){
		BOFF(b) = 0;
		n = devtab[f->type]->read(f, b->wp, s->iounit, 0x7fffffffffffffLL);

		if(n > 0){
			b->wp += n;
			b->flag |= BDONE;
			b->next = nil;
			s->rlist = b;
			s->soff = s->iounit;
			s->roff = 0;
			s->noseek = 1;

			b = sblock(s);
		} else {
			s->noseek = 0;
		}
	}
	while(!qisclosed(s->rq)) {
		ll = &s->rlist;
		while((l = *ll) != nil){
			if((l->flag & BDONE) == 0 || BOFF(l) != s->roff){
				if(s->noseek){
					ll = &l->next;
					continue;
				}
				break;
			}
			if((l->flag & BERROR) != 0)
				error((char*)l->rp);
			if(BLEN(l) == 0){
				qhangup(s->rq, nil);
				poperror();
				goto Done;
			}
			s->roff += s->noseek ? s->iounit : BLEN(l);
			*ll = l->next;
			l->next = nil;
			qbwrite(s->rq, l);
		}

		n = s->iounit;
		o = s->roff;
		l = s->rlist;
		if(s->noseek) {
			o = 0;
			b->next = l;
			s->rlist = b;
		} else if(l == nil) {
			b->next = nil;
			s->rlist = b;
		} else {
			if(o < BOFF(l)){
				n = BOFF(l) - o;
				b->next = l;
				s->rlist = b;
			} else {
				for(;; l = l->next){
					if((l->flag & BDONE) != 0 && BLEN(l) == 0)
						goto Done;
					o = BOFF(l) + ((l->flag & BDONE) == 0 ? s->iounit : BLEN(l));
					if(l->next == nil)
						break;
					if(o < BOFF(l->next)){
						n = BOFF(l->next) - o;
						break;
					}
				}
				b->next = l->next;
				l->next = b;
			}
		}
		BOFF(b) = o;
		qunlock(&s->rql);

		if(waserror()){
			poperror();
			goto Exit;
		}
		ioq(&io, &s->rcl);
		io.b = b;
		io.s = s;
		if(waserror()){
			strncpy((char*)b->wp, up->errstr, s->iounit-1);
			b->wp[s->iounit-1] = 0;
			n = -1;
		} else {
			n = devtab[f->type]->read(f, b->wp, n, o);
			if(n < 0)
				error(Eio);
			poperror();
		}
		iocom(&io, 1);
		poperror();

		l = b;
		b = sblock(s);
		qlock(&s->rql);
		if(n >= 0)
			l->wp += n;
		else
			l->flag |= BERROR;
		l->flag |= BDONE;
	}
	poperror();
Done:
	qunlock(&s->rql);
	freeb(b);
Exit:
	s->rp[id] = nil;
	putstream(s);
	pexit("closed", 1);
}

static void
streamwriter(void *arg)
{
	Stream *s = arg;
	Iocom io;
	Block *b;
	Chan *f;
	vlong o;
	int id, n;

	id = incref(&s->nwp) % nelem(s->wp);
	s->wp[id] = up;

	f = s->f;
	while(!qisclosed(s->wq)) {
		if(incref(&s->nwq) == s->nwp.ref && qlen(s->wq) == 0)
			wakeup(&s->wz);	/* queue drained */
		if(waserror()){
			decref(&s->nwq);
			break;
		}
		ioq(&io, &s->wcl);
		b = qbread(s->wq, s->iounit);
		decref(&s->nwq);
		if(b == nil){
			iocom(&io, 1);
			break;
		}
		poperror();

		if(waserror()){
			qhangup(s->wq, up->errstr);
			iocom(&io, 1);
			freeb(b);
			break;
		}
		n = BLEN(b);
		o = s->woff;
		s->woff += n;
		if(devtab[f->type]->write(f, b->rp, n, o) != n)
			error(Eio);
		iocom(&io, 1);
		freeb(b);
		poperror();
	}

	s->wp[id] = nil;
	wakeup(&s->wz);

	putstream(s);
	pexit("closed", 1);
}

static int
streamgen(Chan *c, char *, Dirtab*, int, int s, Dir *dp)
{
	static int perm[] = { 0400, 0200, 0600, 0 };
	Fgrp *fgrp = up->fgrp;
	Chan *f;
	Qid q;

	if(s == DEVDOTDOT){
		devdir(c, c->qid, ".", 0, eve, DMDIR|0555, dp);
		return 1;
	}
	if(s == 0)
		return 0;
	s--;
	if(s > fgrp->maxfd)
		return -1;
	if((f=fgrp->fd[s]) == nil)
		return 0;
	sprint(up->genbuf, "%dstream", s);
	mkqid(&q, s+1, 0, QTFILE);
	devdir(c, q, up->genbuf, 0, eve, perm[f->mode&3], dp);
	return 1;
}

static Chan*
streamattach(char *spec)
{
	return devattach(L'¶', spec);
}

static Walkqid*
streamwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, (Dirtab *)0, 0, streamgen);
}

static int
streamstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, (Dirtab *)0, 0L, streamgen);
}

static Chan*
streamopen(Chan *c, int omode)
{
	Stream *s;

	c->aux = nil;
	if(c->qid.type & QTDIR){
		if(omode != 0)
			error(Eisdir);
		c->mode = 0;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}
	s = mallocz(sizeof(*s), 1);
	if(s == nil)
		error(Enomem);
	incref(s);
	if(waserror()){
		putstream(s);
		nexterror();
	}
	omode = openmode(omode);
	s->f = fdtochan(c->qid.path - 1, omode, 0, 1);
	if(s->f == nil || s->f->qid.type != QTFILE)
		error(Eperm);
	s->noseek = -1;
	s->roff = s->f->offset;
	s->woff = s->f->offset;
	s->iounit = s->f->iounit;
	if(s->iounit <= 0 || s->iounit > qiomaxatomic)
		s->iounit = qiomaxatomic;
	c->iounit = s->iounit;
	c->aux = s;
	c->mode = omode;
	c->flag |= COPEN;
	c->offset = 0;
	poperror();
	return c;
}

static int
isdrained(void *a)
{
	Stream *s;
	int i;

	s = a;
	if(s->wq == nil)
		return 1;

	if(qisclosed(s->wq) == 0)
		return qlen(s->wq) == 0 && s->nwq.ref == s->nwp.ref;

	for(i=0; i<nelem(s->wp); i++)
		if(s->wp[i] != nil)
			return 0;

	return 1;
}

static void
streamdrain(Chan *c)
{
	Stream *s;

	if((s = c->aux) == nil)
		return;
	eqlock(&s->wql);
	if(waserror()){
		qunlock(&s->wql);
		nexterror();
	}
	while(!streamdrained(s))
		sleep(&s->wz, isdrained, s);
	qunlock(&s->wql);
	poperror();
}

static void
streamclose(Chan *c)
{
	Stream *s;
	int i;

	if((c->flag & COPEN) == 0 || (s = c->aux) == nil)
		return;
	if(s->rq != nil){
		qclose(s->rq);
		for(i=0; i<nelem(s->rp); i++)
			postnote(s->rp[i], 1, "streamclose", 0);
	}
	if(s->wq != nil){
		qhangup(s->wq, nil);
		if(!waserror()){
			streamdrain(c);
			poperror();
		}
		qclose(s->wq);	/* discard the data */
		for(i=0; i<nelem(s->wp); i++)
			postnote(s->wp[i], 1, "streamclose", 0);
	}
	c->aux = nil;
	putstream(s);
}

static int
canpipeline(Chan *f, int mode)
{
	USED(mode);

	return devtab[f->type]->dc == 'M';
}

static Queue*
streamqueue(Chan *c, int mode)
{
	Stream *s;
	int i, n;

	s = c->aux;
	if(s == nil || c->qid.type != QTFILE)
		error(Eperm);

	switch(mode){
	case OREAD:
		while(s->rq == nil){
			qlock(&s->rql);
			if(s->rq != nil){
				qunlock(&s->rql);
				break;
			}
			s->rq = qopen(conf.pipeqsize, 0, 0, 0);
			if(s->rq == nil){
				qunlock(&s->rql);
				error(Enomem);
			}
			n = canpipeline(s->f, mode) ? nelem(s->rp) : 1;
			for(i=0; i<n; i++){
				incref(s);
				kproc("streamreader", streamreader, s);
			}
			while(s->nrp.ref != n)
				sched();
			qunlock(&s->rql);
			break;
		}
		return s->rq;
	case OWRITE:
		while(s->wq == nil){
			qlock(&s->wql);
			if(s->wq != nil){
				qunlock(&s->wql);
				break;
			}
			s->wq = qopen(conf.pipeqsize, 0, 0, 0);
			if(s->wq == nil){
				qunlock(&s->wql);
				error(Enomem);
			}
			n = canpipeline(s->f, mode) ? nelem(s->wp) : 1;
			for(i=0; i<n; i++){
				incref(s);
				kproc("streamwriter", streamwriter, s);
			}
			while(s->nwp.ref != n)
				sched();
			qunlock(&s->wql);
			break;
		}
		return s->wq;
	}
	error(Egreg);
	return nil;
}

static long
streamread(Chan *c, void *va, long n, vlong)
{
	if(c->qid.type == QTDIR)
		return devdirread(c, va, n, (Dirtab *)0, 0L, streamgen);
	return qread(streamqueue(c, OREAD), va, n);
}

static Block*
streambread(Chan *c, long n, ulong)
{
	return qbread(streamqueue(c, OREAD), n);
}

static long
streamwrite(Chan *c, void *va, long n, vlong)
{
	if(n == 0)
		streamdrain(c);
	return qwrite(streamqueue(c, OWRITE), va, n);
}

static long
streambwrite(Chan *c, Block *b, ulong)
{
	if(BLEN(b) == 0)
		streamdrain(c);
	return qbwrite(streamqueue(c, OWRITE), b);
}

Dev streamdevtab = {
	L'¶',
	"stream",

	devreset,
	devinit,
	devshutdown,
	streamattach,
	streamwalk,
	streamstat,
	streamopen,
	devcreate,
	streamclose,
	streamread,
	streambread,
	streamwrite,
	streambwrite,
	devremove,
	devwstat,
};
