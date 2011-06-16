#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <mach.h>
#include <ctype.h>
#include <tos.h>
#include "dat.h"
#include "fns.h"

void
initproc(void)
{
	P = emallocz(sizeof(Process));
	P->pid = getpid();
	P->fd = newfd();
}

static void
initstack(int argc, char **argv)
{
	ulong tos, sp, ap, size, i, len;
	
	tos = STACKTOP - sizeof(Tos) * 2;
	sp = tos;
	
	size = 8;
	for(i = 0; i < argc; i++)
		size += strlen(argv[i]) + 5;
	
	sp -= size;
	sp &= ~7;
	P->R[0] = tos;
	P->R[1] = STACKTOP - 4;
	P->R[13] = sp;
	
	*(ulong *) vaddrnol(sp) = argc;
	sp += 4;
	ap = sp + (argc + 1) * 4;
	for(i = 0; i < argc; i++) {
		*(ulong *) vaddrnol(sp) = ap;
		sp += 4;
		len = strlen(argv[i]) + 1;
		memcpy(vaddrnol(ap), argv[i], len);
		ap += len;
	}
	*(ulong *) vaddrnol(sp) = 0;

	((Tos *) vaddrnol(tos))->pid = getpid();
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
	int fd, i;
	Fhdr fp;
	Segment *text, *data, *bss, *stack;
	char buf[2];
	
	fd = open(file, OREAD);
	if(fd < 0) return -1;
	if(pread(fd, buf, 2, 0) == 2 && buf[0] == '#' && buf[1] == '!')
		return loadscript(fd, file, argc, argv);
	seek(fd, 0, 0);
	if(crackhdr(fd, &fp) == 0) {
		werrstr("exec header invalid");
		return -1;
	}
	if(fp.magic != E_MAGIC) {
		werrstr("exec header invalid");
		return -1;
	}
	freesegs();
	memset(P->R, 0, sizeof(P->R));
	P->CPSR = 0;
	text = newseg(fp.txtaddr - fp.hdrsz, fp.txtsz + fp.hdrsz, SEGTEXT);
	data = newseg(fp.dataddr, fp.datsz, SEGDATA);
	bss = newseg(fp.dataddr + fp.datsz, fp.bsssz, SEGBSS);
	stack = newseg(STACKTOP - STACKSIZE, STACKSIZE, SEGSTACK);
	seek(fd, fp.txtoff - fp.hdrsz, 0);
	if(readn(fd, text->data, fp.txtsz + fp.hdrsz) < fp.txtsz + fp.hdrsz)
		sysfatal("%r");
	seek(fd, fp.datoff, 0);
	if(readn(fd, data->data, fp.datsz) < fp.datsz)
		sysfatal("%r");
	memset(bss->data, 0, bss->size);
	memset(stack->data, 0, stack->size);
	P->R[15] = fp.entry;
	if(havesymbols && syminit(fd, &fp) < 0)
		fprint(2, "initializing symbol table: %r\n");
	close(fd);
	for(i = 0; i < P->fd->nfds * 8; i++)
		if(iscexec(P->fd, i))
			close(i);
	wlock(P->fd);
	free(P->fd->fds);
	P->fd->fds = nil;
	P->fd->nfds = 0;
	wunlock(P->fd);
	initstack(argc, argv);
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
	incref(&fd->ref);
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
	if(decref(&fd->ref) == 0) {
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
