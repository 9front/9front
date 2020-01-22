#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

Chan chans[NCHANS];

int systrace=1;
#define dprint print

static u32int
arg(int n)
{
	return memread32(r[12] + (n + 1) * 4);
}

static char *
strget(u32int addr)
{
	int n;
	char *s;
	
	for(n = 0; memread8(addr + n) != 0; n++)
		;
	s = emalloc(n + 1);
	for(n = 0; n == 0 || s[n-1] != 0; n++)
		s[n] = memread8(addr + n);
	return s;
}

static char **
vecget(u32int addr)
{
	int n;
	u32int u;
	char **s;
	
	for(n = 0; readm(addr + n, 2) != 0; n += 4)
		;
	s = emalloc((n + 1) * sizeof(char *));
	for(n = 0; u = readm(addr + n * 4, 2), u != 0; n++)
		s[n] = strget(u);
	return s;
}

static Chan *
getfd(int n)
{
	if((unsigned)n >= NCHANS || chans[n].fd < 0)
		return nil;
	return &chans[n];
}

static Chan *
newfd(void)
{
	Chan *c;

	for(c = chans; c < chans + nelem(chans); c++)
		if(c->fd < 0)
			return c;
	return nil;
}

static int
toerrno(void)
{
	char buf[ERRMAX];
	
	rerrstr(buf, sizeof(buf));
	if(strstr(buf, "not found")) return -ENOENT;
	print("couldn't translate %s\n", buf);
	return -EIO;
}

static int
toerrnoi(int rc)
{
	if(rc >= 0)
		return rc;
	return toerrno();
}

static int
dostat(u32int buf, Dir *d)
{
	int m;

	if(d == nil) return toerrno();
	writem(buf, 0, 1); /* dev */
	writem(buf + 2, d->qid.path, 1); /* ino */
	m = d->mode & 0777;
	if((d->mode & DMDIR) != 0) m |= 040000;
	else m |= 010000;
	writem(buf + 4, m, 1); /* mode */
	writem(buf + 6, 1, 1); /* nlink */
	writem(buf + 8, 0, 1); /* uid */
	writem(buf + 10, 0, 1); /* gid */
	writem(buf + 12, d->dev, 1); /* dev */
	writem(buf + 16, d->length, 2); /* size */
	writem(buf + 20, d->atime, 2); /* atime */
	writem(buf + 24, d->mtime, 2); /* mtime */
	writem(buf + 28, d->mtime, 2); /* ctime */
	free(d);
	return 0;
}

static int
sysexit(void)
{
	int no;
	
	no = arg(0);
	if(no == 0) exits(nil);
	exits(smprint("%d", no));
	return 0;
}

static int
dodirread(Chan *c)
{
	Dir *d, *dp;
	int rc;
	
	free(c->buf);
	c->buf = c->bufp = c->bufe = nil;	
	rc = dirread(c->fd, &d);
	if(rc <= 0) return rc;
	c->bufp = c->bufe = c->buf = emalloc(16 * rc);
	for(dp = d; --rc >= 0; dp++){
		*c->bufe++ = dp->qid.path;
		*c->bufe++ = dp->qid.path >> 8;
		strncpy(c->bufe, dp->name, 14);
		c->bufe += 14;
	}
	free(d);
	return 0;
}

static int
sysread(void)
{
	int fd, sz, rc, i;
	u32int addr;
	char *buf;
	Chan *c;
	
	fd = arg(0);
	addr = arg(1);
	sz = arg(2);
	if(systrace) dprint("read(%d, %#ux, %d)\n", fd, addr, sz);
	c = getfd(fd);
	if(sz < 0) return -EINVAL;
	if(c == nil) return -EBADF;
	if((c->flags & DIR) != 0){
		if(c->bufp >= c->bufe)
			if(dodirread(c) < 0)
				return toerrno();
		for(rc = 0; sz > 0 && c->bufp < c->bufe; rc++, sz--)
			writem(addr++, *c->bufp++, 0);
		return rc;
	}else{
		buf = emalloc(sz);
		rc = read(c->fd, buf, sz);
		for(i = 0; i < rc; i++)
			writem(addr + i, buf[i], 0);
		free(buf);
		if(rc < 0) return toerrno();
	}
	return rc;
}

static int
syswrite(void)
{
	int fd, sz, rc, i;
	u32int addr;
	Chan *c;
	char *buf;
	
	fd = arg(0);
	addr = arg(1);
	sz = arg(2);
	if(systrace) dprint("write(%d, %#ux, %d)\n", fd, addr, sz);
	c = getfd(fd);
	if(sz < 0) return -EINVAL;
	if(c == nil) return -EBADF;
	buf = emalloc(sz);
	for(i = 0; i < sz; i++)
		buf[i] = memread8(addr + i);
	rc = write(c->fd, buf, sz);
	free(buf);
	return toerrnoi(rc);
}

static int
sysopen(void)
{
	char *s;
	Chan *c;
	int m;
	Dir *d;
	
	s = strget(arg(0));
	m = arg(1);
	if(systrace) dprint("open(\"%s\", %#uo)\n", s, m);
	switch(m){
	case 0: m = OREAD; break;
	case 1: m = OWRITE; break;
	case 2: m = ORDWR; break;
	default: free(s); return -EINVAL;
	}
	c = newfd();
	if(c == nil){
		free(s);
		return -EMFILE;
	}
	c->fd = open(s, m);
	free(s);
	if(c->fd < 0) return toerrno();
	d = dirfstat(c->fd);
	if(d == nil){
		close(c->fd);
		return toerrno();
	}
	if((d->mode & DMDIR) != 0)
		c->flags |= DIR;
	free(d);
	return c - chans;
}

static int
sysclose(void)
{
	int fd;
	Chan *c;
	
	fd = arg(0);
	if(systrace) dprint("close(%d)\n", fd);
	c = getfd(fd);
	if(c == nil) return -EBADF;
	if((c->flags & DONTCLOSE) == 0)
		close(c->fd);
	c->fd = -1;
	return 0;
}

static int
sysfstat(void)
{
	int fd, buf;
	Chan *c;
	Dir *d;
	
	fd = arg(0);
	buf = arg(1);
	if(systrace) dprint("fstat(%d, %#ux)\n", fd, buf);
	c = getfd(fd);
	if(c == nil) return -EBADF;
	d = dirfstat(c->fd);
	return dostat(buf, d);
}

static int
syslstat(void)
{
	char *s;
	int buf;
	Dir *d;
	
	s = strget(arg(0));
	buf = arg(1);
	if(systrace) dprint("lstat(\"%s\", %#ux)\n", s, buf);
	d = dirstat(s);
	free(s);
	return dostat(buf, d);
}

static int
sysioctl(void)
{
	int fd, ctl;
	u32int addr;
	Chan *c;
	
	fd = arg(0);
	ctl = arg(1);
	addr = arg(2);
	if(systrace) dprint("lstat(%d, %#ux, %#ux)\n", fd, ctl, addr);
	c = getfd(fd);
	if(c == nil) return -EBADF;
	switch(ctl){
	case 't'<<8|8:
		if((c->flags & FAKETTY) != 0){
			writem(addr, 13 | 13<<8 | '#'<<16 | '@'<<24, 2);
			writem(addr + 4, 06010, 2);
			return 0;
		}
		return -ENOTTY;
	case 'j'<<8|3: return -EINVAL;
	default:
		fprint(2, "unknown ioctl %c%d\n", ctl >> 8, (u8int)ctl);
		return -EINVAL;
	}
}

static int
systime(void)
{
	u32int addr;
	int t;
	
	addr = arg(0);
	if(systrace) dprint("time(%#ux)\n", addr);
	t = time(0);
	if(addr != 0) writem(addr, t, 2);
	return t;
}

static int
sysbreak(void)
{
	u32int a;
	int ns;
	
	a = arg(0);
	if(systrace) dprint("break(%#ux)\n", a);
	a = -(-a & -1024);
	ns = a - segs[1].start;
	if(ns > segs[1].size){
		segs[1].data = realloc(segs[1].data, ns);
		memset((uchar *) segs[1].data + segs[1].size, 0, ns - segs[1].size);
		segs[1].size = ns;
	}
	return 0;
}

static int
sysftime(void)
{
	u32int p;
	vlong n;
	Tm *t;
	
	p = arg(0);
	if(systrace) dprint("ftime(%#ux)\n", p);
	n = nsec();
	n /= 1000000;
	writem(p + 4, n % 1000, 1);
	n /= 1000;
	writem(p, n, 2);
	t = localtime(n);
	writem(p + 6, -t->tzoff / 60, 1);
	writem(p + 8, 0, 1);
	return 0;
}

static int
syssignal(void)
{
	return 0;
}

static int
sysgetpid(void)
{
	if(systrace) dprint("getpid()\n");
	return getpid() & 0xffff;
}

static int
sysaccess(void)
{
	char *s;
	int m, rc;
	
	s = strget(arg(0));
	m = arg(1);
	if(systrace) dprint("access(\"%s\", %#ux)\n", s, m);
	rc = access(s, m & 7);
	free(s);
	return toerrnoi(rc);	
}

static int
syscreat(void)
{
	char *s;
	Chan *c;
	int m;
	
	s = strget(arg(0));
	m = arg(1);
	if(systrace) dprint("creat(\"%s\", %#uo)\n", s, m);
	c = newfd();
	if(c == nil){
		free(s);
		return -EMFILE;
	}
	c->fd = create(s, OWRITE, m & 0777);
	free(s);
	if(c->fd < 0) return toerrno();
	return c - chans;
}

static int
sysseek(void)
{
	int fd, off, wh;
	Chan *c;
	
	fd = arg(0);
	off = arg(1);
	wh = arg(2);
	if(systrace) dprint("seek(%d, %d, %d)\n", fd, off, wh);
	c = getfd(fd);
	if(c == nil || off < 0 || (uint)wh > 2) return -EBADF;
	return toerrnoi(seek(c->fd, off, wh));
}

static int
sysunlink(void)
{
	char *s;
	int rc;
	
	s = strget(arg(0));
	if(systrace) dprint("unlink(\"%s\")\n", s);
	rc = remove(s);
	free(s);
	return toerrnoi(rc);
}

static int
syschdir(void)
{
	char *s;
	int rc;
	
	s = strget(arg(0));
	if(systrace) dprint("chdir(\"%s\")\n", s);
	rc = chdir(s);
	free(s);
	return toerrnoi(rc);
}

static int
sysgetuid(void)
{
	return 0;
}

static int
sysfork(void)
{
	int rc;
	
	if(systrace) dprint("fork()\n");
	rc = fork();
	if(rc < 0) return toerrno();
	if(rc == 0){
		r[1] = 1;
		return getppid();
	}
	r[1] = 0;
	return rc;
}

static int
sysexece(void)
{
	char *file, **argv, **env, **p;
	int rc;
	
	file = strget(arg(0));
	argv = vecget(arg(1));
	env = vecget(arg(2));
	if(systrace) dprint("exece(\"%s\", ..., ...)\n", file);
	rc = load(file, argv, env);
	for(p = argv; *p != nil; p++)
		free(*p);
	for(p = env; *p != nil; p++)
		free(*p);
	free(file);
	free(argv);
	free(env);
	return toerrnoi(rc);
}

static int
syswait(void)
{
	Waitmsg *w;
	int rc, st;
	u32int addr;
	char *p;
	
	addr = arg(0);
	if(systrace) dprint("wait(%#ux)\n", addr);
	w = wait();
	if(w == nil) return toerrno();
	rc = w->pid;
	if(addr != 0){
		st = strtol(w->msg, &p, 10) & 255 << 8;
		if(*p == 0) st = 127 << 8;
		writem(addr, st, 2);
	}
	free(w);
	return rc;
}

int mask = 022;
static int
sysumask(void)
{
	int rc;
	
	rc = mask;
	mask = arg(0);
	if(systrace) dprint("umask(%#uo)\n", mask);
	return rc;
}

static int
syslink(void)
{
	char *a, *b;
	int f0, f1, rc, n;
	char buf[8192];
	
	a = strget(arg(0));
	b = strget(arg(1));
	if(systrace) dprint("link(\"%s\", \"%s\")\n", a, b);
	f0 = open(a, OREAD);
	f1 = create(b, OWRITE | OEXCL, 0777 ^ mask);
	if(f0 < 0 || f1 < 0) {
	err:
		rc = toerrno();
		goto out;
	}
	for(;;){
		n = read(f0, buf, sizeof(buf));
		if(n < 0) goto err;
		if(n == 0) break;
		if(write(f1, buf, n) < n) goto err;
	}
	rc = 0;
out:
	if(f0 >= 0) close(f0);
	if(f1 >= 0) close(f1);
	free(a);
	free(b);
	return rc;
}

static int
syschmod(void)
{
	char *a;
	int mode;
	Dir d;
	Dir *e;
	
	a = strget(arg(0));
	mode = arg(1);
	if(systrace) dprint("chmod(\"%s\", %#uo)\n", a, mode);
	e = dirstat(a);
	if(e == nil){
		free(a);
		return toerrno();
	}
	nulldir(&d);
	d.mode = e->mode & ~0777 | mode & 0777;
	free(e);
	if(dirwstat(a, &d) < 0){
		free(a);
		return toerrno();
	}
	free(a);
	return 0;
}

static int
sysdup(void)
{
	int fd;
	Chan *c, *d;
	
	fd = arg(0);
	if(systrace) dprint("dup(%d)\n", fd);
	c = getfd(fd);
	if(c == nil) return -EBADF;
	d = newfd();
	if(d == nil) return -EMFILE;
	d->fd = c->fd;
	d->flags = c->flags;
	return d - chans;
}

void
syscall(u16int c)
{
	int rc;

	static int (*calls[])(void) = {
		[1] sysexit,
		[2] sysfork,
		[3] sysread,
		[4] syswrite,
		[5] sysopen,
		[6] sysclose,
		[7] syswait,
		[8] syscreat,
		[9] syslink,
		[10] sysunlink,
		[12] syschdir,
		[13] systime,
		[15] syschmod,
		[17] sysbreak,
		[18] syslstat,
		[19] sysseek,
		[20] sysgetpid,
		[24] sysgetuid,
		[28] sysfstat,
		[33] sysaccess,
		[35] sysftime,
		[40] syslstat,
		[41] sysdup,
		[48] syssignal,
		[54] sysioctl,
		[59] sysexece,
		[60] sysumask,
		[66] sysfork,
	};
	
	if(c >= nelem(calls) || calls[c] == nil) sysfatal("unknown syscall %d", c);
	rc = calls[c]();
	if(rc < 0){
		r[0] = -rc;
		ps |= 1;
	}else{
		r[0] = rc;
		ps &= ~1;
	}
}

void
sysinit(void)
{
	int i;
	
	for(i = 0; i < NCHANS; i++)
		chans[i].fd = -1;
	chans[0].fd = 0;
	chans[0].flags = DONTCLOSE|FAKETTY;
	chans[1].fd = 1;
	chans[1].flags = DONTCLOSE|FAKETTY;
	chans[2].fd = 2;
	chans[2].flags = DONTCLOSE|FAKETTY;
}
