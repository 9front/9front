#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"
#include </sys/src/libc/9syscall/sys.h>

static u32int
arg(int n)
{
	/* no locking necessary, since we're on the stack */
	return *(u32int*) vaddrnol(P->R[13] + 4 + 4 * n, 4);
}

static u64int
argv(int n)
{
	return arg(n) | ((u64int)arg(n+1) << 32);
}

static void
sysopen(void)
{
	u32int name, flags;
	char *namet;
	int fd, copied;
	
	name = arg(0);
	flags = arg(1);
	namet = copyifnec(name, -1, &copied);
	if(systrace)
		fprint(2, "open(%#ux=\"%s\", %#o)\n", name, namet, flags);
	fd = open(namet, flags);
	if(copied)
		free(namet);
	if(fd < 0) {
		noteerr(0, 1);
		P->R[0] = fd;
		return;
	}
	setcexec(P->fd, fd, flags & OCEXEC);
	P->R[0] = fd;
}

static void
syscreate(void)
{
	u32int name, flags, perm;
	char *namet;
	int fd, copied;
	
	name = arg(0);
	flags = arg(1);
	perm = arg(2);
	namet = copyifnec(name, -1, &copied);
	if(systrace)
		fprint(2, "create(%#ux=\"%s\", %#o, %o)\n", name, namet, flags, perm);
	fd = create(namet, flags, perm);
	if(copied)
		free(namet);
	if(fd < 0) {
		noteerr(0, 1);
		P->R[0] = fd;
		return;
	}
	setcexec(P->fd, fd, flags & OCEXEC);
	P->R[0] = fd;
}

static void
sysclose(void)
{
	u32int fd;
	
	fd = arg(0);
	if(systrace)
		fprint(2, "close(%d)\n", fd);
	P->R[0] = noteerr(close(fd), 0);
	if((fd & (1<<31)) == 0)
		setcexec(P->fd, fd, 0);
}

static void
syspread(void)
{
	int buffered;
	u32int fd, size, buf;
	u64int off;
	void *targ;
	
	fd = arg(0);
	buf = arg(1);
	size = arg(2);
	off = argv(3);
	if(systrace)
		fprint(2, "pread(%d, %#ux, %ud, %#ullx)\n", fd, buf, size, off);
	targ = bufifnec(buf, size, &buffered);
	P->R[0] = noteerr(pread(fd, targ, size, off), size);
	if(buffered)
		copyback(buf, P->R[0], targ);
}

static void
syspwrite(void)
{
	u32int fd, size, buf;
	u64int off;
	int copied;
	void *buft;
	
	fd = arg(0);
	buf = arg(1);
	size = arg(2);
	off = argv(3);
	buft = copyifnec(buf, size, &copied);
	if(systrace)
		fprint(2, "pwrite(%d, %#ux, %ud, %#ullx)\n", fd, buf, size, off);
	P->R[0] = noteerr(pwrite(fd, buft, size, off), size);
	if(copied)
		free(buft);
}

static void
sysseek(void)
{
	u32int fd, type;
	vlong n, *ret;
	Segment *seg;
	
	ret = vaddr(arg(0), 8, &seg);
	fd = arg(1);
	n = argv(2);
	type = arg(4);
	if(systrace)
		fprint(2, "seek(%d, %lld, %d)\n", fd, n, type);
	*ret = seek(fd, n, type);
	if(*ret < 0) noteerr(0, 1);
	segunlock(seg);
}

static void
sysfd2path(void)
{
	u32int fd, buf, nbuf;
	void *buft;
	int buffered;
	
	fd = arg(0);
	buf = arg(1);
	nbuf = arg(2);
	buft = bufifnec(buf, nbuf, &buffered);
	if(systrace)
		fprint(2, "fd2path(%d, %#ux, %d)\n", fd, buf, nbuf);
	P->R[0] = noteerr(fd2path(fd, buft, nbuf), 0);
	if(buffered)
		copyback(buf, nbuf, buft);
}

static void
sysstat(void)
{
	u32int name, edir, nedir;
	char *namet;
	void *edirt;
	int copied, buffered;
	
	name = arg(0);
	namet = copyifnec(name, -1, &copied);
	edir = arg(1);
	nedir = arg(2);
	edirt = bufifnec(edir, nedir, &buffered);
	if(systrace)
		fprint(2, "stat(%#ux=\"%s\", %#ux, %ud)\n", name, namet, edir, nedir);
	P->R[0] = noteerr(stat(namet, edirt, nedir), nedir);
	if(copied)
		free(namet);
	if(buffered)
		copyback(edir, P->R[0], edirt);
}

static void
sysfstat(void)
{
	u32int fd, edir, nedir;
	void *edirt;
	int buffered;
	
	fd = arg(0);
	edir = arg(1);
	nedir = arg(2);
	edirt = bufifnec(edir, nedir, &buffered);
	if(systrace)
		fprint(2, "fstat(%d, %#ux, %d)\n", fd, edir, nedir);
	P->R[0] = noteerr(fstat(fd, edirt, nedir), nedir);
	if(buffered)
		copyback(edir, P->R[0], edirt);
}

static void
syswstat(void)
{
	u32int name, edir, nedir;
	char *namet;
	void *edirt;
	int copied, copied2;
	
	name = arg(0);
	namet = copyifnec(name, -1, &copied);
	edir = arg(1);
	nedir = arg(2);
	edirt = copyifnec(edir, nedir, &copied2);
	if(systrace)
		fprint(2, "wstat(%#ux=\"%s\", %#ux, %ud)\n", name, namet, edir, nedir);
	P->R[0] = noteerr(wstat(namet, edirt, nedir), nedir);
	if(copied)
		free(namet);
	if(copied2)
		free(edirt);
}

static void
sysfwstat(void)
{
	u32int fd, edir, nedir;
	void *edirt;
	int copied;
	
	fd = arg(0);
	edir = arg(1);
	nedir = arg(2);
	edirt = copyifnec(edir, nedir, &copied);
	if(systrace)
		fprint(2, "fwstat(%d, %#ux, %d)\n", fd, edir, nedir);
	P->R[0] = noteerr(fwstat(fd, edirt, nedir), nedir);
	if(copied)
		free(edirt);
}

static void
sysexits(void)
{
	if(arg(0) == 0)
		exits(nil);
	else
		exits(vaddrnol(arg(0), 0));
}

static void
sysbrk(void)
{
	ulong v;
	Segment *s;
	
	v = arg(0);
	if(systrace)
		fprint(2, "brk(%#lux)\n", v);
	v = v + 7 & -8;
	if(v >= P->S[SEGSTACK]->start)
		sysfatal("bss > stack, wtf?");
	if(v < P->S[SEGBSS]->start)
		sysfatal("bss length < 0, wtf?");
	s = P->S[SEGBSS];
	wlock(&s->rw);
	s->dref = realloc(s->dref, v - s->start + sizeof(Ref));
	if(s->dref == nil)
		sysfatal("error reallocating");
	s->data = s->dref + 1;
	if(s->size < v - s->start)
		memset((char*)s->data + s->size, 0, v - s->start - s->size);
	s->size = v - s->start;
	P->R[0] = 0;
	wunlock(&s->rw);
}

static void
syserrstr(void)
{
	char buf[ERRMAX], *srct;
	u32int src, len;
	int copied;
	
	src = arg(0);
	len = arg(1);
	srct = copyifnec(src, len, &copied);
	strcpy(buf, P->errbuf);
	utfecpy(P->errbuf, P->errbuf + ERRMAX, srct);
	utfecpy(srct, srct + len, buf);
	if(copied)
		copyback(src, len, srct);
	P->R[0] = 0;
}

static void
syschdir(void)
{
	u32int dir;
	char *dirt;
	int copied;
	
	dir = arg(0);
	dirt = copyifnec(dir, -1, &copied);
	if(systrace)
		fprint(2, "chdir(%#ux=\"%s\")\n", dir, dirt);
	P->R[0] = noteerr(chdir(dirt), 0);
	if(copied)
		free(dirt);
}

static void
sysnotify(void)
{
	u32int handler;
	
	handler = arg(0);
	if(systrace)
		fprint(2, "notify(%#ux)\n", handler);
	P->notehandler = handler;
	P->R[0] = 0;
}

static void
sysnoted(void)
{
	u32int v;
	
	v = arg(0);
	if(systrace)
		fprint(2, "noted(%d)\n", v);
	if(P->innote)
		longjmp(P->notejmp, v + 1);
	cherrstr("the front fell off");
	P->R[0] = -1;
}

static void
sysrfork(void)
{
	u32int flags;
	int rc, i;
	Process *p;
	Segment *s, *t;
	Fd *old;
	
	flags = arg(0);
	if(systrace)
		fprint(2, "rfork(%#o)\n", flags);
	if((flags & (RFFDG | RFCFDG)) == (RFFDG | RFCFDG) ||
	   (flags & (RFNAMEG | RFCNAMEG)) == (RFNAMEG | RFCNAMEG) ||
	   (flags & (RFENVG | RFCENVG)) == (RFENVG | RFCENVG)) {
		P->R[0] = -1;
		cherrstr("bad arg in syscall");
		return;
	}
	if((flags & RFPROC) == 0) {
		if(flags & RFFDG) {
			old = P->fd;
			P->fd = copyfd(P->fd);
			fddecref(old);
		}
		if(flags & RFCFDG) {
			old = P->fd;
			P->fd = newfd();
			fddecref(old);
		}
		P->R[0] = noteerr(rfork(flags), 0);
		return;
	}
	incref(&nproc);
	p = emallocz(sizeof(Process));
	memcpy(p, P, sizeof(Process));
	for(i = 0; i < SEGNUM; i++) {
		s = p->S[i];
		if(s == nil)
			continue;
		if((flags & RFMEM) == 0 && i != SEGTEXT || i == SEGSTACK) {
			t = emallocz(sizeof(Segment));
			incref(t);
			t->size = s->size;
			t->start = s->start;
			t->dref = emalloc(sizeof(Ref) + s->size);
			memset(t->dref, 0, sizeof(Ref));
			incref(t->dref);
			t->data = t->dref + 1;
			memcpy(t->data, s->data, s->size);
			p->S[i] = t;
		} else {
			incref(s->dref);
			incref(s);
		}
	}
	
	if(flags & RFFDG)
		p->fd = copyfd(P->fd);
	else if(flags & RFCFDG)
		p->fd = newfd();
	else
		incref(P->fd);

	incref(P->path);
	rc = rfork(RFMEM | flags);
	if(rc < 0) /* this should NEVER happen */
		sysfatal("rfork failed wtf: %r");
	if(rc == 0) {
		P = p;
		atexit(cleanup);
		P->pid = getpid();
		inittos();
		addproc(P);
	}
	P->R[0] = rc;
}

static void
sysexec(void)
{
	u32int name, argv, *argvt;
	char *namet, **argvv;
	int i, argc, rc;
	Segment *seg1, *seg2;
	
	name = arg(0);
	argv = arg(1);
	namet = strdup(vaddr(name, 0, &seg1));
	segunlock(seg1);
	argvt = vaddr(argv, 0, &seg1);
	if(systrace)
		fprint(2, "exec(%#ux=\"%s\", %#ux)\n", name, namet, argv);
	for(argc = 0; argvt[argc]; argc++)
		;
	argvv = emalloc(sizeof(char *) * argc);
	for(i = 0; i < argc; i++) {
		argvv[i] = strdup(vaddr(argvt[i], 0, &seg2));
		segunlock(seg2);
	}
	segunlock(seg1);
	rc = loadtext(namet, argc, argvv);
	for(i = 0; i < argc; i++)
		free(argvv[i]);
	free(argvv);
	if(rc < 0)
		P->R[0] = noteerr(rc, 0);
	free(namet);
}

static void
sysawait(void)
{
	u32int s, n;
	void *st;
	int buffered;
	
	s = arg(0);
	n = arg(1);
	st = bufifnec(s, n, &buffered);
	if(systrace)
		fprint(2, "await(%#ux, %d)\n", s, n);
	P->R[0] = noteerr(await(st, n), 0);
	if(buffered)
		copyback(s, P->R[0], st);
}

static void
syspipe(void)
{
	u32int fd, *fdt;
	int buffered;
	
	fd = arg(0);
	if(systrace)
		fprint(2, "pipe(%#ux)\n", fd);
	fdt = bufifnec(fd, 8, &buffered);
	P->R[0] = noteerr(pipe((int *) fdt), 0);
	if(buffered)
		copyback(fd, 8, fdt);
}

static void
sysdup(void)
{
	u32int oldfd, newfd;
	
	oldfd = arg(0);
	newfd = arg(1);
	if(systrace)
		fprint(2, "dup(%d, %d)\n", oldfd, newfd);
	P->R[0] = noteerr(dup(oldfd, newfd), 0);
}

static void
syssleep(void)
{
	u32int n;
	
	n = arg(0);
	if(systrace)
		fprint(2, "sleep(%d)\n", n);
	P->R[0] = noteerr(sleep(n), 0);
}

static void
sysrendezvous(void)
{
	u32int tag, value;
	
	tag = arg(0);
	value = arg(1);
	if(systrace)
		fprint(2, "rendezvous(%#ux, %#ux)\n", tag, value);
	P->R[0] = (u32int) (uintptr)rendezvous((void *) tag, (void *) value);
	if(P->R[0] == ~0)
		noteerr(0, 1);
}

static void
sysmount(void)
{
	u32int fd, afd, old, flag, aname;
	char *oldt, *anamet;
	int copiedold, copiedaname;
	
	fd = arg(0);
	afd = arg(1);
	old = arg(2);
	flag = arg(3);
	aname = arg(4);
	oldt = copyifnec(old, -1, &copiedold);
	if(aname) {
		anamet = copyifnec(aname, -1, &copiedaname);
		if(systrace)
			fprint(2, "mount(%d, %d, %#x=\"%s\", %#o, %#x=\"%s\")\n", fd, afd, old, oldt, flag, aname, anamet);
	} else {
		anamet = nil;
		copiedaname = 0;
		if(systrace)
			fprint(2, "mount(%d, %d, %#x=\"%s\", %#o, nil)\n", fd, afd, old, oldt, flag);
	}
	P->R[0] = noteerr(mount(fd, afd, oldt, flag, anamet), 0);
	if(copiedold)
		free(oldt);
	if(copiedaname)
		free(anamet);
}

static void
sysbind(void)
{
	u32int name, old, flags;
	char *namet, *oldt;
	int copiedname, copiedold;
	
	name = arg(0);
	old = arg(1);
	flags = arg(2);
	namet = copyifnec(name, -1, &copiedname);
	oldt = copyifnec(old, -1, &copiedold);
	if(systrace)
		fprint(2, "bind(%#ux=\"%s\", %#ux=\"%s\", %#o)\n", name, namet, old, oldt, flags);
	P->R[0] = noteerr(bind(namet, oldt, flags), 0);
	if(copiedname)
		free(namet);
	if(copiedold)
		free(oldt);
}

static void
sysunmount(void)
{
	u32int name, old;
	char *namet, *oldt;
	int copiedname, copiedold;
	
	name = arg(0);
	old = arg(1);
	oldt = copyifnec(old, -1, &copiedold);
	if(name == 0) {
		namet = nil;
		copiedname = 0;
		if(systrace)
			fprint(2, "unmount(nil, %#ux=\"%s\")\n", old, oldt);
		P->R[0] = noteerr(unmount(nil, oldt), 0);
	} else {
		namet = copyifnec(name, -1, &copiedname);
		if(systrace)
			fprint(2, "unmount(%#ux=\"%s\", %#ux=\"%s\")\n", name, namet, old, oldt);
		P->R[0] = noteerr(unmount(namet, oldt), 0);
	}
	if(copiedold)
		free(oldt);
	if(copiedname)
		free(namet);
}

static void
sysremove(void)
{
	u32int file;
	char *filet;
	int copied;
	
	file = arg(0);
	filet = copyifnec(file, -1, &copied);
	if(systrace)
		fprint(2, "remove(%#ux=\"%s\")\n", file, filet);
	P->R[0] = noteerr(remove(filet), 0);
	if(copied)
		free(filet);
}

static void
sysalarm(void)
{
	u32int msec;
	
	msec = arg(0);
	if(systrace)
		fprint(2, "alarm(%d)\n", msec);
	P->R[0] = alarm(msec);
}

static void
syssemacquire(void)
{
	u32int addr, block;
	long *addrt;

	addr = arg(0);
	block = arg(1);
	if(systrace)
		fprint(2, "semacquire(%#ux, %ud)\n", addr, block);
	addrt = vaddrnol(addr, 4);
	P->R[0] = noteerr(semacquire(addrt, block), 0);
}

static void
syssemrelease(void)
{
	u32int addr, count;
	long *addrt;
	Segment *seg;

	addr = arg(0);
	count = arg(1);
	if(systrace)
		fprint(2, "semrelease(%#ux, %ud)\n", addr, count);
	addrt = vaddr(addr, 4, &seg);
	P->R[0] = noteerr(semrelease(addrt, count), 0);
	segunlock(seg);
}

void
syscall(void)
{
	u32int n;
	static void (*calls[])(void) = {
		[EXITS] sysexits,
		[CLOSE] sysclose,
		[OPEN] sysopen,
		[CREATE] syscreate,
		[PREAD] syspread,
		[PWRITE] syspwrite,
		[BRK_] sysbrk,
		[ERRSTR] syserrstr,
		[STAT] sysstat,
		[FSTAT] sysfstat,
		[WSTAT] syswstat,
		[FWSTAT] sysfwstat,
		[SEEK] sysseek,
		[CHDIR] syschdir,
		[FD2PATH] sysfd2path,
		[NOTIFY] sysnotify,
		[NOTED] sysnoted,
		[RFORK] sysrfork,
		[EXEC] sysexec,
		[AWAIT] sysawait,
		[PIPE] syspipe,
		[SLEEP] syssleep,
		[RENDEZVOUS] sysrendezvous,
		[BIND] sysbind,
		[UNMOUNT] sysunmount,
		[DUP] sysdup,
		[MOUNT] sysmount,
		[REMOVE] sysremove,
		[ALARM] sysalarm,
		[SEMACQUIRE] syssemacquire,
		[SEMRELEASE] syssemrelease,
	};
	
	n = P->R[0];
	if(n >= nelem(calls) || calls[n] == nil)
		sysfatal("no such syscall %d @ %#ux", n, P->R[15] - 4);
	calls[n]();
}
