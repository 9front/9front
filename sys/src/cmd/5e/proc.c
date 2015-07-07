#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <mach.h>
#include <ctype.h>
#include "dat.h"
#include "fns.h"

#pragma pack on
typedef struct Tos Tos;
struct Tos {
	struct			/* Per process profiling */
	{
		ulong	pp;	/* known to be 0(ptr) */
		ulong	next;	/* known to be 4(ptr) */
		ulong	last;
		ulong	first;
		ulong	pid;
		ulong	what;
	} prof;
	uvlong	cyclefreq;	/* cycle clock frequency if there is one, 0 otherwise */
	vlong	kcycles;	/* cycles spent in kernel */
	vlong	pcycles;	/* cycles spent in process (kernel + user) */
	ulong	pid;		/* might as well put the pid here */
	ulong	clock;
	/* top of stack is here */
};
#pragma pack off

Process plist;
Lock plistlock;

void
initproc(void)
{
	P = emallocz(sizeof(Process));
	P->pid = getpid();
	P->fd = newfd();
	incref(&nproc);
	plist.prev = P;
	plist.next = P;
	P->prev = &plist;
	P->next = &plist;
	if(vfp)
		resetvfp();
	else
		resetfpa();
}

void
addproc(Process *p)
{
	lock(&plistlock);
	p->prev = plist.prev;
	p->next = &plist;
	plist.prev->next = p;
	plist.prev = p;
	unlock(&plistlock);
}

void
remproc(Process *p)
{
	lock(&plistlock);
	p->prev->next = p->next;
	p->next->prev = p->prev;
	unlock(&plistlock);
}

Process *
findproc(int pid)
{
	Process *p;

	lock(&plistlock);
	for(p = plist.next; p != &plist; p = p->next)
		if(p->pid == pid)
			break;
	unlock(&plistlock);
	if(p != &plist)
		return p;
	return nil;
}

static void
copyname(char *file)
{
	char *p;
	
	p = strrchr(file, '/');
	if(p == nil)
		p = file;
	else
		p++;
	strncpy(P->name, p, NAMEMAX);

	if(P->path != nil && decref(P->path) == 0)
		free(P->path);
	P->path = emallocz(sizeof(Ref) + strlen(file)+1);
	incref(P->path);
	strcpy((char*)(P->path + 1), file);
}

static void
initstack(int argc, char **argv)
{
	ulong tos, sp, ap, size, i, len;
	
	tos = (mach->utop & ~7) - sizeof(Tos) * 2;
	sp = tos;
	
	size = 8;
	for(i = 0; i < argc; i++)
		size += strlen(argv[i]) + 5;
	
	sp -= size;
	sp &= ~7;
	P->R[0] = tos;
	P->R[1] = mach->utop - 4;
	P->R[13] = sp;
	
	*(ulong *) vaddrnol(sp, 4) = argc;
	sp += 4;
	ap = sp + (argc + 1) * 4;
	for(i = 0; i < argc; i++) {
		*(ulong *) vaddrnol(sp, 4) = ap;
		sp += 4;
		len = strlen(argv[i]) + 1;
		memcpy(vaddrnol(ap, len), argv[i], len);
		ap += len;
	}
	*(ulong *) vaddrnol(sp, 4) = 0;
	inittos();
}

void
inittos(void)
{
	ulong tos;

	tos = (mach->utop & ~7) - sizeof(Tos) * 2;
	((Tos *) vaddrnol(tos, sizeof(Tos)))->pid = P->pid;
}

static int
loadscript(int fd, char *file, int argc, char **argv)
{
	char buf[513], *p, **q, **nargv;
	int rc, nargc, i;
	
	seek(fd, 0, 0);
	rc = readn(fd, buf, 512);
	if(rc <= 0)
		goto invalid;
	close(fd);
	buf[rc] = 0;
	p = strchr(buf, '\n');
	if(p == nil)
		goto invalid;
	*p = 0;
	while(isspace(*--p))
		*p = 0;
	nargc = 0;
	p = buf + 2;
	while(*p) {
		while(*p && isspace(*p))
			p++;
		nargc++;
		while(*p && !isspace(*p))
			p++;
	}
	if(nargc == 0)
		goto invalid;
	nargv = emallocz(sizeof(char *) * (nargc + argc));
	q = nargv;
	p = buf + 2;
	while(*p) {
		while(*p && isspace(*p))
			p++;
		*(p-1) = 0;
		*q++ = p;
		while(*p && !isspace(*p))
			p++;
	}
	*q++ = file;
	for(i = 1; i < argc; i++)
		*q++ = argv[i];
	rc = loadtext(*nargv, argc + nargc, nargv);
	free(nargv);
	return rc;

invalid:
	werrstr("exec header invalid");
	return -1;
}

int
loadtext(char *file, int argc, char **argv)
{
	int fd;
	Fhdr fp;
	Segment *text, *data, *bss;
	char buf[2];
	
	fd = open(file, OREAD);
	if(fd < 0) return -1;
	if(pread(fd, buf, 2, 0) == 2 && buf[0] == '#' && buf[1] == '!')
		return loadscript(fd, file, argc, argv);
	seek(fd, 0, 0);
	if(crackhdr(fd, &fp) == 0 || fp.magic != E_MAGIC) {
		werrstr("exec header invalid");
		return -1;
	}
	copyname(file);
	P->notehandler = P->innote = P->notein = P->noteout = 0;
	freesegs();
	memset(P->R, 0, sizeof(P->R));
	P->CPSR = 0;
	text = newseg(fp.txtaddr - fp.hdrsz, fp.txtsz + fp.hdrsz, SEGTEXT);
	data = newseg(fp.dataddr, fp.datsz, SEGDATA);
	bss = newseg(fp.dataddr + fp.datsz, fp.bsssz, SEGBSS);
	newseg((mach->utop & ~7) - STACKSIZE, STACKSIZE, SEGSTACK);
	seek(fd, fp.txtoff - fp.hdrsz, 0);
	if(readn(fd, text->data, fp.txtsz + fp.hdrsz) < fp.txtsz + fp.hdrsz)
		sysfatal("%r");
	seek(fd, fp.datoff, 0);
	if(readn(fd, data->data, fp.datsz) < fp.datsz)
		sysfatal("%r");
	memset(bss->data, 0, bss->size);
	P->R[15] = fp.entry;
	if(havesymbols && syminit(fd, &fp) < 0)
		fprint(2, "initializing symbol table: %r\n");
	close(fd);
	fdclear(P->fd);
	initstack(argc, argv);
	if(vfp)
		resetvfp();
	else
		resetfpa();
	return 0;
}

void
cherrstr(char *str, ...)
{
	va_list va;
	
	va_start(va, str);
	vsnprint(P->errbuf, ERRMAX, str, va);
	va_end(va);
}

u32int
noteerr(u32int x, u32int y)
{
	if(((int)x) >= ((int)y))
		return x;
	rerrstr(P->errbuf, ERRMAX);
	return x;
}

Fd *
newfd(void)
{
	Fd *fd;
	
	fd = emallocz(sizeof(*fd));
	incref(fd);
	return fd;
}

Fd *
copyfd(Fd *old)
{
	Fd *new;
	
	rlock(old);
	new = newfd();
	if(old->nfds > 0) {
		new->nfds = old->nfds;
		new->fds = emalloc(old->nfds);
		memcpy(new->fds, old->fds, old->nfds);
	}
	runlock(old);
	return new;
}

void
fddecref(Fd *fd)
{
	if(decref(fd) == 0) {
		free(fd->fds);
		free(fd);
	}
}

int
iscexec(Fd *fd, int n)
{
	int r;

	r = 0;
	rlock(fd);
	if(n / 8 < fd->nfds)
		r = (fd->fds[n / 8] & (1 << (n % 8))) != 0;
	runlock(fd);
	return r;
}

void
setcexec(Fd *fd, int n, int status)
{
	int old;

	wlock(fd);
	if(n / 8 >= fd->nfds) {
		if(status == 0) {
			wunlock(fd);
			return;
		}
		old = fd->nfds;
		fd->nfds = (n / 8) + 1;
		fd->fds = erealloc(fd->fds, fd->nfds);
		memset(fd->fds + old, 0, fd->nfds - old);
	}
	if(status == 0)
		fd->fds[n / 8] &= ~(1 << (n % 8));
	else
		fd->fds[n / 8] |= (1 << (n % 8));
	wunlock(fd);
}

void
fdclear(Fd *fd)
{
	int i, j, k;

	wlock(fd);
	if(fd->nfds == 0) {
		wunlock(fd);
		return;
	}
	for(i = 0; i < fd->nfds; i++) {
		j = fd->fds[i];
		for(k = 0; k < 8; k++)
			if(j & (1<<k))
				close(8 * i + k);
	}
	free(fd->fds);
	fd->nfds = 0;
	fd->fds = nil;
	wunlock(fd);
}

/* call this from a notehandler if you don't want the front to fall off */
void
addnote(char *msg)
{
	int new;
	
	new = P->notein + 1;
	if((new - P->noteout) % NNOTE == 0)
		return;

	strncpy(P->notes[P->notein % NNOTE], msg, ERRMAX - 1);
	P->notein = new;
}

/* the following code is not for the weak of heart */
void
donote(char *msg, ulong type)
{
	int rc;
	u32int *ureg, *sp, uregp, msgp;
	char *msgb;

	if(P->notehandler == 0)
		exits(msg);

	clrex();
	uregp = P->R[13] - 18 * 4;
	ureg = vaddrnol(uregp, 18 * 4);
	memcpy(ureg, P->R, 15 * 4);
	ureg[15] = type;
	ureg[16] = P->CPSR;
	ureg[17] = P->R[15];
	P->R[13] = uregp;
	msgp = P->R[13] -= ERRMAX;
	msgb = vaddrnol(msgp, ERRMAX);
	strncpy(msgb, msg, ERRMAX);
	P->R[13] -= 3 * 4;
	sp = vaddrnol(P->R[13], 3 * 4);
	sp[0] = 0;
	sp[2] = msgp;
	P->R[0] = uregp;
	P->R[15] = P->notehandler;
	P->innote = 1;
	switch(rc = setjmp(P->notejmp) - 1) {
	case -1:
		for(;;) {
			if(ultraverbose)
				dump();
			step();
		}
	case NDFLT:
		exits(msg);
	case NCONT:
		break;
	default:
		sysfatal("unhandled noted argument %d", rc);
	}
	P->innote = 0;
	ureg = vaddrnol(uregp, 18 * 4); /* just to be sure */
	memcpy(P->R, ureg, 15 * 4);
	P->CPSR = ureg[16];
	P->R[15] = ureg[17];
}
