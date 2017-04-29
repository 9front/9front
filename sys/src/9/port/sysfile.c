#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

/*
 * The sys*() routines needn't poperror() as they return directly to syscall().
 */

static void
unlockfgrp(Fgrp *f)
{
	int ex;

	ex = f->exceed;
	f->exceed = 0;
	unlock(f);
	if(ex)
		pprint("warning: process exceeds %d file descriptors\n", ex);
}

int
growfd(Fgrp *f, int fd)	/* fd is always >= 0 */
{
	Chan **newfd, **oldfd;

	if(fd < f->nfd)
		return 0;
	if(fd >= f->nfd+DELTAFD)
		return -1;	/* out of range */
	/*
	 * Unbounded allocation is unwise; besides, there are only 16 bits
	 * of fid in 9P
	 */
	if(f->nfd >= 5000){
    Exhausted:
		print("no free file descriptors\n");
		return -1;
	}
	newfd = malloc((f->nfd+DELTAFD)*sizeof(Chan*));
	if(newfd == nil)
		goto Exhausted;
	oldfd = f->fd;
	memmove(newfd, oldfd, f->nfd*sizeof(Chan*));
	f->fd = newfd;
	free(oldfd);
	f->nfd += DELTAFD;
	if(fd > f->maxfd){
		if(fd/100 > f->maxfd/100)
			f->exceed = (fd/100)*100;
		f->maxfd = fd;
	}
	return 1;
}

/*
 *  this assumes that the fgrp is locked
 */
int
findfreefd(Fgrp *f, int start)
{
	int fd;

	for(fd=start; fd<f->nfd; fd++)
		if(f->fd[fd] == nil)
			break;
	if(fd >= f->nfd && growfd(f, fd) < 0)
		return -1;
	return fd;
}

int
newfd(Chan *c)
{
	int fd;
	Fgrp *f;

	f = up->fgrp;
	lock(f);
	fd = findfreefd(f, 0);
	if(fd < 0){
		unlockfgrp(f);
		return -1;
	}
	if(fd > f->maxfd)
		f->maxfd = fd;
	f->fd[fd] = c;
	unlockfgrp(f);
	return fd;
}

int
newfd2(int fd[2], Chan *c[2])
{
	Fgrp *f;

	f = up->fgrp;
	lock(f);
	fd[0] = findfreefd(f, 0);
	if(fd[0] < 0){
		unlockfgrp(f);
		return -1;
	}
	fd[1] = findfreefd(f, fd[0]+1);
	if(fd[1] < 0){
		unlockfgrp(f);
		return -1;
	}
	if(fd[1] > f->maxfd)
		f->maxfd = fd[1];
	f->fd[fd[0]] = c[0];
	f->fd[fd[1]] = c[1];
	unlockfgrp(f);
	return 0;
}

Chan*
fdtochan(int fd, int mode, int chkmnt, int iref)
{
	Chan *c;
	Fgrp *f;

	c = nil;
	f = up->fgrp;

	lock(f);
	if(fd<0 || f->nfd<=fd || (c = f->fd[fd])==nil) {
		unlock(f);
		error(Ebadfd);
	}
	if(iref)
		incref(c);
	unlock(f);

	if(chkmnt && (c->flag&CMSG)) {
		if(iref)
			cclose(c);
		error(Ebadusefd);
	}

	if(mode<0 || c->mode==ORDWR)
		return c;

	if((mode&OTRUNC) && c->mode==OREAD) {
		if(iref)
			cclose(c);
		error(Ebadusefd);
	}

	if((mode&~OTRUNC) != c->mode) {
		if(iref)
			cclose(c);
		error(Ebadusefd);
	}
	return c;
}

int
openmode(ulong o)
{
	o &= ~(OTRUNC|OCEXEC|ORCLOSE);
	if(o > OEXEC)
		error(Ebadarg);
	if(o == OEXEC)
		return OREAD;
	return o;
}

uintptr
sysfd2path(va_list list)
{
	Chan *c;
	char *buf;
	uint len;
	int fd;

	fd = va_arg(list, int);
	buf = va_arg(list, char*);
	len = va_arg(list, uint);
	validaddr((uintptr)buf, len, 1);
	c = fdtochan(fd, -1, 0, 1);
	snprint(buf, len, "%s", chanpath(c));
	cclose(c);
	return 0;
}

uintptr
syspipe(va_list list)
{
	static char *datastr[] = {"data", "data1"};
	int fd[2], *ufd;
	Chan *c[2];

	ufd = va_arg(list, int*);
	validaddr((uintptr)ufd, sizeof(fd), 1);
	evenaddr((uintptr)ufd);
	
	ufd[0] = ufd[1] = fd[0] = fd[1] = -1;
	c[0] = namec("#|", Atodir, 0, 0);
	c[1] = nil;
	if(waserror()){
		cclose(c[0]);
		if(c[1] != nil)
			cclose(c[1]);
		nexterror();
	}
	c[1] = cclone(c[0]);
	if(walk(&c[0], datastr+0, 1, 1, nil) < 0)
		error(Egreg);
	if(walk(&c[1], datastr+1, 1, 1, nil) < 0)
		error(Egreg);
	c[0] = devtab[c[0]->type]->open(c[0], ORDWR);
	c[1] = devtab[c[1]->type]->open(c[1], ORDWR);
	if(newfd2(fd, c) < 0)
		error(Enofd);
	ufd[0] = fd[0];
	ufd[1] = fd[1];
	poperror();
	return 0;
}

uintptr
sysdup(va_list list)
{
	int fd;
	Chan *c, *oc;
	Fgrp *f = up->fgrp;

	fd = va_arg(list, int);

	/*
	 * Close after dup'ing, so date > #d/1 works
	 */
	c = fdtochan(fd, -1, 0, 1);
	fd = va_arg(list, int);
	if(fd != -1){
		lock(f);
		if(fd<0 || growfd(f, fd)<0) {
			unlockfgrp(f);
			cclose(c);
			error(Ebadfd);
		}
		if(fd > f->maxfd)
			f->maxfd = fd;

		oc = f->fd[fd];
		f->fd[fd] = c;
		unlockfgrp(f);
		if(oc != nil)
			cclose(oc);
	}else{
		if(waserror()) {
			cclose(c);
			nexterror();
		}
		fd = newfd(c);
		if(fd < 0)
			error(Enofd);
		poperror();
	}
	return (uintptr)fd;
}

uintptr
sysopen(va_list list)
{
	int fd;
	Chan *c;
	char *name;
	ulong mode;

	name = va_arg(list, char*);
	mode = va_arg(list, ulong);
	openmode(mode);	/* error check only */
	validaddr((uintptr)name, 1, 0);
	c = namec(name, Aopen, mode, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	fd = newfd(c);
	if(fd < 0)
		error(Enofd);
	poperror();
	return (uintptr)fd;
}

void
fdclose(int fd, int flag)
{
	Chan *c;
	Fgrp *f = up->fgrp;

	lock(f);
	c = fd <= f->maxfd ? f->fd[fd] : nil;
	if(c == nil || (flag != 0 && (c->flag&flag) == 0)){
		unlock(f);
		return;
	}
	f->fd[fd] = nil;
	if(fd == f->maxfd){
		while(fd > 0 && f->fd[fd] == nil)
			f->maxfd = --fd;
	}
	unlock(f);
	cclose(c);
}

uintptr
sysclose(va_list list)
{
	int fd;

	fd = va_arg(list, int);
	fdtochan(fd, -1, 0, 0);
	fdclose(fd, 0);
	return 0;
}

long
unionread(Chan *c, void *va, long n)
{
	int i;
	long nr;
	Mhead *m;
	Mount *mount;

	eqlock(&c->umqlock);
	m = c->umh;
	rlock(&m->lock);
	mount = m->mount;
	/* bring mount in sync with c->uri and c->umc */
	for(i = 0; mount != nil && i < c->uri; i++)
		mount = mount->next;

	nr = 0;
	while(mount != nil){
		/* Error causes component of union to be skipped */
		if(mount->to != nil && !waserror()){
			if(c->umc == nil){
				c->umc = cclone(mount->to);
				c->umc = devtab[c->umc->type]->open(c->umc, OREAD);
			}
	
			nr = devtab[c->umc->type]->read(c->umc, va, n, c->umc->offset);
			c->umc->offset += nr;
			poperror();
		}
		if(nr > 0)
			break;

		/* Advance to next element */
		c->uri++;
		if(c->umc != nil){
			cclose(c->umc);
			c->umc = nil;
		}
		mount = mount->next;
	}
	runlock(&m->lock);
	qunlock(&c->umqlock);
	return nr;
}

static void
unionrewind(Chan *c)
{
	eqlock(&c->umqlock);
	c->uri = 0;
	if(c->umc != nil){
		cclose(c->umc);
		c->umc = nil;
	}
	qunlock(&c->umqlock);
}

static int
dirfixed(uchar *p, uchar *e, Dir *d)
{
	int len;

	len = GBIT16(p)+BIT16SZ;
	if(p + len > e)
		return -1;

	p += BIT16SZ;	/* ignore size */
	d->type = devno(GBIT16(p), 1);
	p += BIT16SZ;
	d->dev = GBIT32(p);
	p += BIT32SZ;
	d->qid.type = GBIT8(p);
	p += BIT8SZ;
	d->qid.vers = GBIT32(p);
	p += BIT32SZ;
	d->qid.path = GBIT64(p);
	p += BIT64SZ;
	d->mode = GBIT32(p);
	p += BIT32SZ;
	d->atime = GBIT32(p);
	p += BIT32SZ;
	d->mtime = GBIT32(p);
	p += BIT32SZ;
	d->length = GBIT64(p);

	return len;
}

static char*
dirname(uchar *p, int *n)
{
	p += BIT16SZ+BIT16SZ+BIT32SZ+BIT8SZ+BIT32SZ+BIT64SZ
		+ BIT32SZ+BIT32SZ+BIT32SZ+BIT64SZ;
	*n = GBIT16(p);
	return (char*)p+BIT16SZ;
}

static long
dirsetname(char *name, int len, uchar *p, long n, long maxn)
{
	char *oname;
	int olen;
	long nn;

	if(n == BIT16SZ)
		return BIT16SZ;

	oname = dirname(p, &olen);

	nn = n+len-olen;
	PBIT16(p, nn-BIT16SZ);
	if(nn > maxn)
		return BIT16SZ;

	if(len != olen)
		memmove(oname+len, oname+olen, p+n-(uchar*)(oname+olen));
	PBIT16((uchar*)(oname-2), len);
	memmove(oname, name, len);
	return nn;
}

/*
 * Mountfix might have caused the fixed results of the directory read
 * to overflow the buffer.  Catch the overflow in c->dirrock.
 */
static void
mountrock(Chan *c, uchar *p, uchar **pe)
{
	uchar *e, *r;
	int len, n;

	e = *pe;

	/* find last directory entry */
	for(;;){
		len = BIT16SZ+GBIT16(p);
		if(p+len >= e)
			break;
		p += len;
	}

	/* save it away */
	qlock(&c->rockqlock);
	if(c->nrock+len > c->mrock){
		n = ROUND(c->nrock+len, 1024);
		r = smalloc(n);
		memmove(r, c->dirrock, c->nrock);
		free(c->dirrock);
		c->dirrock = r;
		c->mrock = n;
	}
	memmove(c->dirrock+c->nrock, p, len);
	c->nrock += len;
	qunlock(&c->rockqlock);

	/* drop it */
	*pe = p;
}

/*
 * Satisfy a directory read with the results saved in c->dirrock.
 */
static int
mountrockread(Chan *c, uchar *op, long n, long *nn)
{
	long dirlen;
	uchar *rp, *erp, *ep, *p;

	/* common case */
	if(c->nrock == 0)
		return 0;

	/* copy out what we can */
	qlock(&c->rockqlock);
	rp = c->dirrock;
	erp = rp+c->nrock;
	p = op;
	ep = p+n;
	while(rp+BIT16SZ <= erp){
		dirlen = BIT16SZ+GBIT16(rp);
		if(p+dirlen > ep)
			break;
		memmove(p, rp, dirlen);
		p += dirlen;
		rp += dirlen;
	}

	if(p == op){
		qunlock(&c->rockqlock);
		return 0;
	}

	/* shift the rest */
	if(rp != erp)
		memmove(c->dirrock, rp, erp-rp);
	c->nrock = erp - rp;

	*nn = p - op;
	qunlock(&c->rockqlock);
	return 1;
}

static void
mountrewind(Chan *c)
{
	c->nrock = 0;
}

/*
 * Rewrite the results of a directory read to reflect current 
 * name space bindings and mounts.  Specifically, replace
 * directory entries for bind and mount points with the results
 * of statting what is mounted there.  Except leave the old names.
 */
static long
mountfix(Chan *c, uchar *op, long n, long maxn)
{
	char *name;
	int nbuf, nname;
	Chan *nc;
	Mhead *mh;
	Mount *m;
	uchar *p;
	int dirlen, rest;
	long l;
	uchar *buf, *e;
	Dir d;

	p = op;
	buf = nil;
	nbuf = 0;
	for(e=&p[n]; p+BIT16SZ<e; p+=dirlen){
		dirlen = dirfixed(p, e, &d);
		if(dirlen < 0)
			break;
		nc = nil;
		mh = nil;
		if(findmount(&nc, &mh, d.type, d.dev, d.qid)){
			/*
			 * If it's a union directory and the original is
			 * in the union, don't rewrite anything.
			 */
			for(m = mh->mount; m != nil; m = m->next)
				if(eqchantdqid(m->to, d.type, d.dev, d.qid, 1))
					goto Norewrite;

			name = dirname(p, &nname);
			/*
			 * Do the stat but fix the name.  If it fails, leave old entry.
			 * BUG: If it fails because there isn't room for the entry,
			 * what can we do?  Nothing, really.  Might as well skip it.
			 */
			if(buf == nil){
				buf = smalloc(4096);
				nbuf = 4096;
			}
			if(waserror())
				goto Norewrite;
			l = devtab[nc->type]->stat(nc, buf, nbuf);
			l = dirsetname(name, nname, buf, l, nbuf);
			if(l == BIT16SZ)
				error("dirsetname");
			poperror();

			/*
			 * Shift data in buffer to accomodate new entry,
			 * possibly overflowing into rock.
			 */
			rest = e - (p+dirlen);
			if(l > dirlen){
				while(p+l+rest > op+maxn){
					mountrock(c, p, &e);
					if(e == p){
						dirlen = 0;
						goto Norewrite;
					}
					rest = e - (p+dirlen);
				}
			}
			if(l != dirlen){
				memmove(p+l, p+dirlen, rest);
				dirlen = l;
				e = p+dirlen+rest;
			}

			/*
			 * Rewrite directory entry.
			 */
			memmove(p, buf, l);

		    Norewrite:
			cclose(nc);
			putmhead(mh);
		}
	}
	if(buf != nil)
		free(buf);

	if(p != e)
		error("oops in rockfix");

	return e-op;
}

static long
read(int fd, uchar *p, long n, vlong *offp)
{
	long nn, nnn;
	Chan *c;
	vlong off;

	validaddr((uintptr)p, n, 1);
	c = fdtochan(fd, OREAD, 1, 1);

	if(waserror()){
		cclose(c);
		nexterror();
	}

	/*
	 * The offset is passed through on directories, normally.
	 * Sysseek complains, but pread is used by servers like exportfs,
	 * that shouldn't need to worry about this issue.
	 *
	 * Notice that c->devoffset is the offset that c's dev is seeing.
	 * The number of bytes read on this fd (c->offset) may be different
	 * due to rewritings in rockfix.
	 */
	if(offp == nil)	/* use and maintain channel's offset */
		off = c->offset;
	else
		off = *offp;
	if(off < 0)
		error(Enegoff);

	if(off == 0){	/* rewind to the beginning of the directory */
		if(offp == nil || (c->qid.type & QTDIR)){
			c->offset = 0;
			c->devoffset = 0;
		}
		mountrewind(c);
		unionrewind(c);
	}

	if(c->qid.type & QTDIR){
		if(mountrockread(c, p, n, &nn)){
			/* do nothing: mountrockread filled buffer */
		}else if(c->umh != nil)
			nn = unionread(c, p, n);
		else{
			if(off != c->offset)
				error(Edirseek);
			nn = devtab[c->type]->read(c, p, n, c->devoffset);
		}
		nnn = mountfix(c, p, nn, n);
	}else
		nnn = nn = devtab[c->type]->read(c, p, n, off);

	if(offp == nil || (c->qid.type & QTDIR)){
		lock(c);
		c->devoffset += nn;
		c->offset += nnn;
		unlock(c);
	}

	poperror();
	cclose(c);
	return nnn;
}

uintptr
sys_read(va_list list)
{
	int fd;
	void *buf;
	long len;

	fd = va_arg(list, int);
	buf = va_arg(list, void*);
	len = va_arg(list, long);
	return (uintptr)read(fd, buf, len, nil);
}

uintptr
syspread(va_list list)
{
	int fd;
	void *buf;
	long len;
	vlong off, *offp;

	fd = va_arg(list, int);
	buf = va_arg(list, void*);
	len = va_arg(list, long);
	off = va_arg(list, vlong);
	if(off != ~0ULL)
		offp = &off;
	else
		offp = nil;
	return (uintptr)read(fd, buf, len, offp);
}

static long
write(int fd, void *buf, long len, vlong *offp)
{
	Chan *c;
	long m, n;
	vlong off;

	validaddr((uintptr)buf, len, 0);
	n = 0;
	c = fdtochan(fd, OWRITE, 1, 1);
	if(waserror()) {
		if(offp == nil){
			lock(c);
			c->offset -= n;
			unlock(c);
		}
		cclose(c);
		nexterror();
	}

	if(c->qid.type & QTDIR)
		error(Eisdir);

	n = len;

	if(offp == nil){	/* use and maintain channel's offset */
		lock(c);
		off = c->offset;
		c->offset += n;
		unlock(c);
	}else
		off = *offp;

	if(off < 0)
		error(Enegoff);

	m = devtab[c->type]->write(c, buf, n, off);
	if(offp == nil && m < n){
		lock(c);
		c->offset -= n - m;
		unlock(c);
	}

	poperror();
	cclose(c);
	return m;
}

uintptr
sys_write(va_list list)
{
	int fd;
	void *buf;
	long len;

	fd = va_arg(list, int);
	buf = va_arg(list, void*);
	len = va_arg(list, long);
	return (uintptr)write(fd, buf, len, nil);
}

uintptr
syspwrite(va_list list)
{
	int fd;
	void *buf;
	long len;
	vlong off, *offp;

	fd = va_arg(list, int);
	buf = va_arg(list, void*);
	len = va_arg(list, long);
	off = va_arg(list, vlong);
	if(off != ~0ULL)
		offp = &off;
	else
		offp = nil;
	return (uintptr)write(fd, buf, len, offp);
}

static vlong
sseek(int fd, vlong o, int type)
{
	Chan *c;
	uchar buf[sizeof(Dir)+100];
	Dir dir;
	int n;
	vlong off;

	c = fdtochan(fd, -1, 1, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	if(devtab[c->type]->dc == L'|')
		error(Eisstream);

	off = 0;
	switch(type){
	case 0:
		off = o;
		if((c->qid.type & QTDIR) && off != 0)
			error(Eisdir);
		if(off < 0)
			error(Enegoff);
		c->offset = off;
		break;

	case 1:
		if(c->qid.type & QTDIR)
			error(Eisdir);
		lock(c);	/* lock for read/write update */
		off = o + c->offset;
		if(off < 0){
			unlock(c);
			error(Enegoff);
		}
		c->offset = off;
		unlock(c);
		break;

	case 2:
		if(c->qid.type & QTDIR)
			error(Eisdir);
		n = devtab[c->type]->stat(c, buf, sizeof buf);
		if(convM2D(buf, n, &dir, nil) == 0)
			error("internal error: stat error in seek");
		off = dir.length + o;
		if(off < 0)
			error(Enegoff);
		c->offset = off;
		break;

	default:
		error(Ebadarg);
	}
	c->uri = 0;
	c->dri = 0;
	cclose(c);
	poperror();
	return off;
}

uintptr
sysseek(va_list list)
{
	int fd, t;
	vlong n, *v;

	v = va_arg(list, vlong*);
	evenaddr((uintptr)v);
	validaddr((uintptr)v, sizeof(vlong), 1);

	fd = va_arg(list, int);
	n = va_arg(list, vlong);
	t = va_arg(list, int);

	*v = sseek(fd, n, t);

	return 0;
}

uintptr
sysoseek(va_list list)
{
	int fd, t;
	long n;

	fd = va_arg(list, int);
	n = va_arg(list, long);
	t = va_arg(list, int);
	return (uintptr)sseek(fd, n, t);
}

void
validstat(uchar *s, int n)
{
	int m;
	char buf[64];

	if(statcheck(s, n) < 0)
		error(Ebadstat);
	/* verify that name entry is acceptable */
	s += STATFIXLEN - 4*BIT16SZ;	/* location of first string */
	/*
	 * s now points at count for first string.
	 * if it's too long, let the server decide; this is
	 * only for his protection anyway. otherwise
	 * we'd have to allocate and waserror.
	 */
	m = GBIT16(s);
	s += BIT16SZ;
	if(m+1 > sizeof buf)
		return;
	memmove(buf, s, m);
	buf[m] = '\0';
	/* name could be '/' */
	if(strcmp(buf, "/") != 0)
		validname(buf, 0);
}

static char*
pathlast(Path *p)
{
	char *s;

	if(p == nil)
		return nil;
	if(p->len == 0)
		return nil;
	s = strrchr(p->s, '/');
	if(s != nil)
		return s+1;
	return p->s;
}

uintptr
sysfstat(va_list list)
{
	Chan *c;
	int fd;
	uint l;
	uchar *s;

	fd = va_arg(list, int);
	s = va_arg(list, uchar*);
	l = va_arg(list, uint);
	validaddr((uintptr)s, l, 1);

	c = fdtochan(fd, -1, 0, 1);
	if(waserror()) {
		cclose(c);
		nexterror();
	}
	l = devtab[c->type]->stat(c, s, l);
	poperror();
	cclose(c);
	return l;
}

uintptr
sysstat(va_list list)
{
	char *name;
	Chan *c;
	uint l, r;
	uchar *s;

	name = va_arg(list, char*);
	s = va_arg(list, uchar*);
	l = va_arg(list, uint);
	validaddr((uintptr)s, l, 1);
	validaddr((uintptr)name, 1, 0);
	c = namec(name, Aaccess, 0, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	r = devtab[c->type]->stat(c, s, l);
	name = pathlast(c->path);
	if(name != nil)
		r = dirsetname(name, strlen(name), s, r, l);

	poperror();
	cclose(c);
	return r;
}

uintptr
syschdir(va_list list)
{
	Chan *c;
	char *name;

	name = va_arg(list, char*);
	validaddr((uintptr)name, 1, 0);
	c = namec(name, Atodir, 0, 0);
	cclose(up->dot);
	up->dot = c;
	return 0;
}

long
bindmount(int ismount, int fd, int afd, char* arg0, char* arg1, int flag, char* spec)
{
	int ret;
	Chan *c0, *c1, *ac, *bc;

	if((flag&~MMASK) || (flag&MORDER)==(MBEFORE|MAFTER))
		error(Ebadarg);

	if(ismount){
		validaddr((uintptr)spec, 1, 0);
		spec = validnamedup(spec, 1);
		if(waserror()){
			free(spec);
			nexterror();
		}

		if(up->pgrp->noattach)
			error(Enoattach);

		ac = nil;
		bc = fdtochan(fd, ORDWR, 0, 1);
		if(waserror()) {
			if(ac != nil)
				cclose(ac);
			cclose(bc);
			nexterror();
		}

		if(afd >= 0)
			ac = fdtochan(afd, ORDWR, 0, 1);

		c0 = mntattach(bc, ac, spec, flag&MCACHE);
		poperror();	/* ac bc */
		if(ac != nil)
			cclose(ac);
		cclose(bc);
	}else{
		spec = nil;
		validaddr((uintptr)arg0, 1, 0);
		c0 = namec(arg0, Abind, 0, 0);
	}

	if(waserror()){
		cclose(c0);
		nexterror();
	}

	validaddr((uintptr)arg1, 1, 0);
	c1 = namec(arg1, Amount, 0, 0);
	if(waserror()){
		cclose(c1);
		nexterror();
	}

	ret = cmount(&c0, c1, flag, spec);

	poperror();
	cclose(c1);
	poperror();
	cclose(c0);
	if(ismount){
		fdclose(fd, 0);
		poperror();
		free(spec);
	}
	return ret;
}

uintptr
sysbind(va_list list)
{
	char *arg0, *arg1;
	int flag;

	arg0 = va_arg(list, char*);
	arg1 = va_arg(list, char*);
	flag = va_arg(list, int);
	return (uintptr)bindmount(0, -1, -1, arg0, arg1, flag, nil);
}

uintptr
sysmount(va_list list)
{
	char *arg1, *spec;
	int flag;
	int fd, afd;

	fd = va_arg(list, int);
	afd = va_arg(list, int);
	arg1 = va_arg(list, char*);
	flag = va_arg(list, int);
	spec = va_arg(list, char*);
	return (uintptr)bindmount(1, fd, afd, nil, arg1, flag, spec);
}

uintptr
sys_mount(va_list list)
{
	char *arg1, *spec;
	int flag;
	int fd;

	fd = va_arg(list, int);
	arg1 = va_arg(list, char*);
	flag = va_arg(list, int);
	spec = va_arg(list, char*);
	return (uintptr)bindmount(1, fd, -1, nil, arg1, flag, spec);
}

uintptr
sysunmount(va_list list)
{
	Chan *cmount, *cmounted;
	char *name, *old;

	name = va_arg(list, char*);
	old = va_arg(list, char*);

	cmounted = nil;
	validaddr((uintptr)old, 1, 0);
	cmount = namec(old, Amount, 0, 0);
	if(waserror()) {
		cclose(cmount);
		if(cmounted != nil)
			cclose(cmounted);
		nexterror();
	}
	if(name != nil) {
		/*
		 * This has to be namec(..., Aopen, ...) because
		 * if arg[0] is something like /srv/cs or /fd/0,
		 * opening it is the only way to get at the real
		 * Chan underneath.
		 */
		validaddr((uintptr)name, 1, 0);
		cmounted = namec(name, Aopen, OREAD, 0);
	}
	cunmount(cmount, cmounted);
	poperror();
	cclose(cmount);
	if(cmounted != nil)
		cclose(cmounted);
	return 0;
}

uintptr
syscreate(va_list list)
{
	int fd, mode, perm;
	char *name;
	Chan *c;

	name = va_arg(list, char*);
	mode = va_arg(list, int);
	perm = va_arg(list, int);
	openmode(mode&~OEXCL);	/* error check only; OEXCL okay here */
	validaddr((uintptr)name, 1, 0);
	c = namec(name, Acreate, mode, perm);
	if(waserror()) {
		cclose(c);
		nexterror();
	}
	fd = newfd(c);
	if(fd < 0)
		error(Enofd);
	poperror();
	return (uintptr)fd;
}

uintptr
sysremove(va_list list)
{
	char *name;
	Chan *c;

	name = va_arg(list, char*);
	validaddr((uintptr)name, 1, 0);
	c = namec(name, Aremove, 0, 0);
	/*
	 * Removing mount points is disallowed to avoid surprises
	 * (which should be removed: the mount point or the mounted Chan?).
	 */
	if(c->ismtpt){
		cclose(c);
		error(Eismtpt);
	}
	if(waserror()){
		c->type = 0;	/* see below */
		cclose(c);
		nexterror();
	}
	devtab[c->type]->remove(c);
	/*
	 * Remove clunks the fid, but we need to recover the Chan
	 * so fake it up.  rootclose() is known to be a nop.
	 */
	c->type = 0;
	poperror();
	cclose(c);
	return 0;
}

static long
wstat(Chan *c, uchar *d, int nd)
{
	long l;
	int namelen;

	if(waserror()){
		cclose(c);
		nexterror();
	}
	if(c->ismtpt){
		/*
		 * Renaming mount points is disallowed to avoid surprises
		 * (which should be renamed? the mount point or the mounted Chan?).
		 */
		dirname(d, &namelen);
		if(namelen)
			nameerror(chanpath(c), Eismtpt);
	}
	l = devtab[c->type]->wstat(c, d, nd);
	poperror();
	cclose(c);
	return l;
}

uintptr
syswstat(va_list list)
{
	char *name;
	uchar *s;
	Chan *c;
	uint l;

	name = va_arg(list, char*);
	s = va_arg(list, uchar*);
	l = va_arg(list, uint);
	validaddr((uintptr)s, l, 0);
	validstat(s, l);
	validaddr((uintptr)name, 1, 0);
	c = namec(name, Aaccess, 0, 0);
	return (uintptr)wstat(c, s, l);
}

uintptr
sysfwstat(va_list list)
{
	uchar *s;
	Chan *c;
	uint l;
	int fd;

	fd = va_arg(list, int);
	s = va_arg(list, uchar*);
	l = va_arg(list, uint);
	validaddr((uintptr)s, l, 0);
	validstat(s, l);
	c = fdtochan(fd, -1, 1, 1);
	return (uintptr)wstat(c, s, l);
}

static void
packoldstat(uchar *buf, Dir *d)
{
	uchar *p;
	ulong q;

	/* lay down old stat buffer - grotty code but it's temporary */
	p = buf;
	strncpy((char*)p, d->name, 28);
	p += 28;
	strncpy((char*)p, d->uid, 28);
	p += 28;
	strncpy((char*)p, d->gid, 28);
	p += 28;
	q = (ulong)d->qid.path & ~DMDIR;	/* make sure doesn't accidentally look like directory */
	if(d->qid.type & QTDIR)	/* this is the real test of a new directory */
		q |= DMDIR;
	PBIT32(p, q);
	p += BIT32SZ;
	PBIT32(p, d->qid.vers);
	p += BIT32SZ;
	PBIT32(p, d->mode);
	p += BIT32SZ;
	PBIT32(p, d->atime);
	p += BIT32SZ;
	PBIT32(p, d->mtime);
	p += BIT32SZ;
	PBIT64(p, d->length);
	p += BIT64SZ;
	PBIT16(p, d->type);
	p += BIT16SZ;
	PBIT16(p, d->dev);
}

uintptr
sys_stat(va_list list)
{
	static char old[] = "old stat system call - recompile";
	Chan *c;
	uint l;
	uchar *s, buf[128];	/* old DIRLEN plus a little should be plenty */
	char strs[128], *name;
	Dir d;

	name = va_arg(list, char*);
	s = va_arg(list, uchar*);
	validaddr((uintptr)s, 116, 1);
	validaddr((uintptr)name, 1, 0);
	c = namec(name, Aaccess, 0, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	l = devtab[c->type]->stat(c, buf, sizeof buf);
	/* buf contains a new stat buf; convert to old. yuck. */
	if(l <= BIT16SZ)	/* buffer too small; time to face reality */
		error(old);
	name = pathlast(c->path);
	if(name != nil)
		l = dirsetname(name, strlen(name), buf, l, sizeof buf);
	l = convM2D(buf, l, &d, strs);
	if(l == 0)
		error(old);
	packoldstat(s, &d);
	
	poperror();
	cclose(c);
	return 0;
}

uintptr
sys_fstat(va_list list)
{
	static char old[] = "old fstat system call - recompile";
	Chan *c;
	char *name;
	uint l;
	uchar *s, buf[128];	/* old DIRLEN plus a little should be plenty */
	char strs[128];
	Dir d;
	int fd;

	fd = va_arg(list, int);
	s = va_arg(list, uchar*);
	validaddr((uintptr)s, 116, 1);
	c = fdtochan(fd, -1, 0, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	l = devtab[c->type]->stat(c, buf, sizeof buf);
	/* buf contains a new stat buf; convert to old. yuck. */
	if(l <= BIT16SZ)	/* buffer too small; time to face reality */
		error(old);
	name = pathlast(c->path);
	if(name != nil)
		l = dirsetname(name, strlen(name), buf, l, sizeof buf);
	l = convM2D(buf, l, &d, strs);
	if(l == 0)
		error(old);
	packoldstat(s, &d);
	
	poperror();
	cclose(c);
	return 0;
}

uintptr
sys_wstat(va_list)
{
	error("old wstat system call - recompile");
	return (uintptr)-1;
}

uintptr
sys_fwstat(va_list)
{
	error("old fwstat system call - recompile");
	return (uintptr)-1;
}
