#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"edf.h"

#include	<a.out.h>

uintptr
sysr1(va_list)
{
	if(!iseve())
		error(Eperm);
	return 0;
}

static void
abortion(void*)
{
	pexit("fork aborted", 1);
}

uintptr
sysrfork(va_list list)
{
	Proc *p;
	int n, i;
	Fgrp *ofg;
	Pgrp *opg;
	Rgrp *org;
	Egrp *oeg;
	ulong pid, flag;
	Mach *wm;

	flag = va_arg(list, ulong);
	/* Check flags before we commit */
	if((flag & (RFFDG|RFCFDG)) == (RFFDG|RFCFDG))
		error(Ebadarg);
	if((flag & (RFNAMEG|RFCNAMEG)) == (RFNAMEG|RFCNAMEG))
		error(Ebadarg);
	if((flag & (RFENVG|RFCENVG)) == (RFENVG|RFCENVG))
		error(Ebadarg);

	if((flag&RFPROC) == 0) {
		if(flag & (RFMEM|RFNOWAIT))
			error(Ebadarg);
		if(flag & (RFFDG|RFCFDG)) {
			ofg = up->fgrp;
			if(flag & RFFDG)
				up->fgrp = dupfgrp(ofg);
			else
				up->fgrp = dupfgrp(nil);
			closefgrp(ofg);
		}
		if(flag & (RFNAMEG|RFCNAMEG)) {
			opg = up->pgrp;
			up->pgrp = newpgrp();
			if(flag & RFNAMEG)
				pgrpcpy(up->pgrp, opg);
			/* inherit noattach */
			up->pgrp->noattach = opg->noattach;
			closepgrp(opg);
		}
		if(flag & RFNOMNT)
			up->pgrp->noattach = 1;
		if(flag & RFREND) {
			org = up->rgrp;
			up->rgrp = newrgrp();
			closergrp(org);
		}
		if(flag & (RFENVG|RFCENVG)) {
			oeg = up->egrp;
			up->egrp = smalloc(sizeof(Egrp));
			up->egrp->ref = 1;
			if(flag & RFENVG)
				envcpy(up->egrp, oeg);
			closeegrp(oeg);
		}
		if(flag & RFNOTEG)
			setnoteid(up, 0);
		return 0;
	}

	p = newproc();

	qlock(&p->debug);

	p->scallnr = up->scallnr;
	p->s = up->s;
	p->slash = up->slash;
	p->dot = up->dot;
	incref(p->dot);

	memmove(p->note, up->note, sizeof(p->note));
	p->nnote = up->nnote;
	p->notify = up->notify;
	p->notified = 0;
	p->notepending = 0;
	p->lastnote = up->lastnote;
	if((flag & RFNOTEG) == 0)
		p->noteid = up->noteid;

	p->procmode = up->procmode;
	p->privatemem = up->privatemem;
	p->noswap = up->noswap;
	p->hang = up->hang;
	if(up->procctl == Proc_tracesyscall)
		p->procctl = Proc_tracesyscall;
	p->kp = 0;

	/* Craft a return frame which will cause the child to pop out of
	 * the scheduler in user mode with the return register zero
	 */
	forkchild(p, up->dbgreg);

	kstrdup(&p->text, up->text);
	kstrdup(&p->user, up->user);
	kstrdup(&p->args, "");
	p->nargs = 0;
	p->setargs = 0;

	p->insyscall = 0;
	memset(p->time, 0, sizeof(p->time));
	p->time[TReal] = MACHP(0)->ticks;

	pid = pidalloc(p);

	qunlock(&p->debug);

	/* Abort the child process on error */
	if(waserror()){
		p->kp = 1;
		kprocchild(p, abortion, 0);
		ready(p);
		nexterror();
	}

	/* Make a new set of memory segments */
	n = flag & RFMEM;
	qlock(&p->seglock);
	if(waserror()){
		qunlock(&p->seglock);
		nexterror();
	}
	for(i = 0; i < NSEG; i++)
		if(up->seg[i] != nil)
			p->seg[i] = dupseg(up->seg, i, n);
	qunlock(&p->seglock);
	poperror();

	/* File descriptors */
	if(flag & (RFFDG|RFCFDG)) {
		if(flag & RFFDG)
			p->fgrp = dupfgrp(up->fgrp);
		else
			p->fgrp = dupfgrp(nil);
	}
	else {
		p->fgrp = up->fgrp;
		incref(p->fgrp);
	}

	/* Process groups */
	if(flag & (RFNAMEG|RFCNAMEG)) {
		p->pgrp = newpgrp();
		if(flag & RFNAMEG)
			pgrpcpy(p->pgrp, up->pgrp);
		/* inherit noattach */
		p->pgrp->noattach = up->pgrp->noattach;
	}
	else {
		p->pgrp = up->pgrp;
		incref(p->pgrp);
	}
	if(flag & RFNOMNT)
		p->pgrp->noattach = 1;

	if(flag & RFREND)
		p->rgrp = newrgrp();
	else {
		incref(up->rgrp);
		p->rgrp = up->rgrp;
	}

	/* Environment group */
	if(flag & (RFENVG|RFCENVG)) {
		p->egrp = smalloc(sizeof(Egrp));
		p->egrp->ref = 1;
		if(flag & RFENVG)
			envcpy(p->egrp, up->egrp);
	}
	else {
		p->egrp = up->egrp;
		incref(p->egrp);
	}

	procfork(p);

	poperror();	/* abortion */

	if((flag&RFNOWAIT) == 0){
		p->parent = up;
		lock(&up->exl);
		up->nchild++;
		unlock(&up->exl);
	}

	/*
	 *  since the bss/data segments are now shareable,
	 *  any mmu info about this process is now stale
	 *  (i.e. has bad properties) and has to be discarded.
	 */
	flushmmu();
	p->basepri = up->basepri;
	p->priority = up->basepri;
	p->fixedpri = up->fixedpri;
	p->mp = up->mp;
	wm = up->wired;
	if(wm != nil)
		procwired(p, wm->machno);
	p->psstate = nil;
	ready(p);
	sched();
	return pid;
}

static int
shargs(char *s, int n, char **ap)
{
	int i;

	s += 2;
	n -= 2;		/* skip #! */
	for(i=0;; i++){
		if(i >= n)
			return 0;
		if(s[i]=='\n')
			break;
	}
	s[i] = 0;

	i = 0;
	for(;;) {
		while(*s==' ' || *s=='\t')
			s++;
		if(*s == 0)
			break;
		ap[i++] = s++;
		while(*s && *s!=' ' && *s!='\t')
			s++;
		if(*s == 0)
			break;
		*s++ = 0;
	}
	ap[i] = nil;
	return i;
}

static ulong
l2be(long l)
{
	uchar *cp;

	cp = (uchar*)&l;
	return (cp[0]<<24) | (cp[1]<<16) | (cp[2]<<8) | cp[3];
}

uintptr
sysexec(va_list list)
{
	Exec exec;
	char line[sizeof(Exec)];
	char *progarg[sizeof(line)/2+1];
	volatile char *args, *elem, *file0;
	char **argv, **argp, **argp0;
	char *a, *e, *charp, *file;
	int i, n, indir;
	ulong magic, ssize, nargs, nbytes;
	uintptr t, d, b, entry, bssend, text, data, bss, tstk, align;
	Segment *s, *ts;
	Image *img;
	Tos *tos;
	Chan *tc;
	Fgrp *f;

	args = elem = nil;
	file0 = va_arg(list, char*);
	validaddr((uintptr)file0, 1, 0);
	argp0 = va_arg(list, char**);
	evenaddr((uintptr)argp0);
	validaddr((uintptr)argp0, 2*BY2WD, 0);
	if(*argp0 == nil)
		error(Ebadarg);
	file0 = validnamedup(file0, 1);
	if(waserror()){
		free(file0);
		free(elem);
		free(args);
		/* Disaster after commit */
		if(up->seg[SSEG] == nil)
			pexit(up->errstr, 1);
		s = up->seg[ESEG];
		if(s != nil){
			putseg(s);
			up->seg[ESEG] = nil;
		}
		nexterror();
	}
	align = BY2PG;
	indir = 0;
	file = file0;
	for(;;){
		tc = namec(file, Aopen, OEXEC, 0);
		if(waserror()){
			cclose(tc);
			nexterror();
		}
		if(!indir)
			kstrdup(&elem, up->genbuf);

		n = devtab[tc->type]->read(tc, &exec, sizeof(Exec), 0);
		if(n <= 2)
			error(Ebadexec);
		magic = l2be(exec.magic);
		if(n == sizeof(Exec) && (magic == AOUT_MAGIC)){
			entry = l2be(exec.entry);
			text = l2be(exec.text);
			if(magic & HDR_MAGIC)
				text += 8;
			switch(magic){
			case S_MAGIC:	/* 2MB segment alignment for amd64 */
				align = 0x200000;
				break;
			case V_MAGIC:	/* 16K segment alignment for mips */
				align = 0x4000;
				break;
			case R_MAGIC:	/* 64K segment alignment for arm64 */
				align = 0x10000;
				break;
			}
			if(text >= (USTKTOP-USTKSIZE)-(UTZERO+sizeof(Exec))
			|| entry < UTZERO+sizeof(Exec)
			|| entry >= UTZERO+sizeof(Exec)+text)
				error(Ebadexec);
			break; /* for binary */
		}

		/*
		 * Process #! /bin/sh args ...
		 */
		memmove(line, &exec, n);
		if(line[0]!='#' || line[1]!='!' || indir++)
			error(Ebadexec);
		n = shargs(line, n, progarg);
		if(n < 1)
			error(Ebadexec);
		/*
		 * First arg becomes complete file name
		 */
		progarg[n++] = file;
		progarg[n] = nil;
		argp0++;
		file = progarg[0];
		progarg[0] = elem;
		poperror();
		cclose(tc);
	}

	data = l2be(exec.data);
	bss = l2be(exec.bss);
	align--;
	t = (UTZERO+sizeof(Exec)+text+align) & ~align;
	align = BY2PG-1;
	d = (t + data + align) & ~align;
	bssend = t + data + bss;
	b = (bssend + align) & ~align;
	if(t >= (USTKTOP-USTKSIZE) || d >= (USTKTOP-USTKSIZE) || b >= (USTKTOP-USTKSIZE))
		error(Ebadexec);

	/*
	 * Args: pass 1: count
	 */
	nbytes = sizeof(Tos);		/* hole for profiling clock at top of stack (and more) */
	nargs = 0;
	if(indir){
		argp = progarg;
		while(*argp != nil){
			a = *argp++;
			nbytes += strlen(a) + 1;
			nargs++;
		}
	}
	argp = argp0;
	while(*argp != nil){
		a = *argp++;
		if(((uintptr)argp&(BY2PG-1)) < BY2WD)
			validaddr((uintptr)argp, BY2WD, 0);
		validaddr((uintptr)a, 1, 0);
		e = vmemchr(a, 0, USTKSIZE);
		if(e == nil)
			error(Ebadarg);
		nbytes += (e - a) + 1;
		if(nbytes >= USTKSIZE)
			error(Enovmem);
		nargs++;
	}
	ssize = BY2WD*(nargs+1) + ((nbytes+(BY2WD-1)) & ~(BY2WD-1));

	/*
	 * 8-byte align SP for those (e.g. sparc) that need it.
	 * execregs() will subtract another 4 bytes for argc.
	 */
	if(BY2WD == 4 && (ssize+4) & 7)
		ssize += 4;

	if(PGROUND(ssize) >= USTKSIZE)
		error(Enovmem);

	/*
	 * Build the stack segment, putting it in kernel virtual for the moment
	 */
	qlock(&up->seglock);
	if(waserror()){
		qunlock(&up->seglock);
		nexterror();
	}

	s = up->seg[SSEG];
	do {
		tstk = s->base;
		if(tstk <= USTKSIZE)
			error(Enovmem);
	} while((s = isoverlap(tstk-USTKSIZE, USTKSIZE)) != nil);
	up->seg[ESEG] = newseg(SG_STACK | SG_NOEXEC, tstk-USTKSIZE, USTKSIZE/BY2PG);

	/*
	 * Args: pass 2: assemble; the pages will be faulted in
	 */
	tos = (Tos*)(tstk - sizeof(Tos));
	tos->cyclefreq = m->cyclefreq;
	tos->kcycles = 0;
	tos->pcycles = 0;
	tos->clock = 0;

	argv = (char**)(tstk - ssize);
	charp = (char*)(tstk - nbytes);
	if(indir)
		argp = progarg;
	else
		argp = argp0;

	for(i=0; i<nargs; i++){
		if(indir && *argp==nil) {
			indir = 0;
			argp = argp0;
		}
		*argv++ = charp + (USTKTOP-tstk);
		a = *argp++;
		if(indir)
			e = strchr(a, 0);
		else {
			if(charp >= (char*)tos)
				error(Ebadarg);
			validaddr((uintptr)a, 1, 0);
			e = vmemchr(a, 0, (char*)tos - charp);
			if(e == nil)
				error(Ebadarg);
		}
		n = (e - a) + 1;
		memmove(charp, a, n);
		charp += n;
	}

	/* copy args; easiest from new process's stack */
	a = (char*)(tstk - nbytes);
	n = charp - a;
	if(n > 128)	/* don't waste too much space on huge arg lists */
		n = 128;
	args = smalloc(n);
	memmove(args, a, n);
	if(n>0 && args[n-1]!='\0'){
		/* make sure last arg is NUL-terminated */
		/* put NUL at UTF-8 character boundary */
		for(i=n-1; i>0; --i)
			if(fullrune(args+i, n-i))
				break;
		args[i] = 0;
		n = i+1;
	}

	/*
	 * Committed.
	 * Free old memory.
	 * Special segments are maintained across exec
	 */
	for(i = SSEG; i <= BSEG; i++) {
		putseg(up->seg[i]);
		/* prevent a second free if we have an error */
		up->seg[i] = nil;
	}
	for(i = ESEG+1; i < NSEG; i++) {
		s = up->seg[i];
		if(s != nil && (s->type&SG_CEXEC) != 0) {
			putseg(s);
			up->seg[i] = nil;
		}
	}

	/* Text.  Shared. Attaches to cache image if possible */
	/* attachimage returns a locked cache image */
	img = attachimage(SG_TEXT | SG_RONLY, tc, UTZERO, (t-UTZERO)>>PGSHIFT);
	ts = img->s;
	up->seg[TSEG] = ts;
	ts->flushme = 1;
	ts->fstart = 0;
	ts->flen = sizeof(Exec)+text;
	unlock(img);

	/* Data. Shared. */
	s = newseg(SG_DATA, t, (d-t)>>PGSHIFT);
	up->seg[DSEG] = s;

	/* Attached by hand */
	incref(img);
	s->image = img;
	s->fstart = ts->fstart+ts->flen;
	s->flen = data;

	/* BSS. Zero fill on demand */
	up->seg[BSEG] = newseg(SG_BSS, d, (b-d)>>PGSHIFT);

	/*
	 * Move the stack
	 */
	s = up->seg[ESEG];
	up->seg[ESEG] = nil;
	s->base = USTKTOP-USTKSIZE;
	s->top = USTKTOP;
	relocateseg(s, USTKTOP-tstk);
	up->seg[SSEG] = s;
	qunlock(&up->seglock);
	poperror();	/* seglock */

	/*
	 * Close on exec
	 */
	if((f = up->fgrp) != nil) {
		for(i=0; i<=f->maxfd; i++)
			fdclose(i, CCEXEC);
	}

	/*
	 *  '/' processes are higher priority (hack to make /ip more responsive).
	 */
	if(devtab[tc->type]->dc == L'/')
		up->basepri = PriRoot;
	up->priority = up->basepri;

	poperror();	/* tc */
	cclose(tc);
	poperror();	/* file0 */
	free(file0);

	qlock(&up->debug);
	free(up->text);
	up->text = elem;
	free(up->args);
	up->args = args;
	up->nargs = n;
	up->setargs = 0;

	up->nnote = 0;
	up->notify = nil;
	up->notified = 0;
	up->privatemem = 0;
	up->noswap = 0;
	procsetup(up);
	qunlock(&up->debug);

	up->errbuf0[0] = '\0';
	up->errbuf1[0] = '\0';

	/*
	 *  At this point, the mmu contains info about the old address
	 *  space and needs to be flushed
	 */
	flushmmu();

	if(up->hang)
		up->procctl = Proc_stopme;
	return execregs(entry, ssize, nargs);
}

int
return0(void*)
{
	return 0;
}

uintptr
syssleep(va_list list)
{
	long ms;

	ms = va_arg(list, long);
	if(ms <= 0) {
		if (up->edf != nil && (up->edf->flags & Admitted))
			edfyield();
		else
			yield();
	} else {
		tsleep(&up->sleep, return0, 0, ms);
	}
	return 0;
}

uintptr
sysalarm(va_list list)
{
	return procalarm(va_arg(list, ulong));
}


uintptr
sysexits(va_list list)
{
	char *status;
	char *inval = "invalid exit string";
	char buf[ERRMAX];

	status = va_arg(list, char*);
	if(status != nil){
		if(waserror())
			status = inval;
		else{
			validaddr((uintptr)status, 1, 0);
			if(vmemchr(status, 0, ERRMAX) == nil){
				memmove(buf, status, ERRMAX);
				buf[ERRMAX-1] = 0;
				status = buf;
			}
			poperror();
		}

	}
	pexit(status, 1);
	return 0;	/* not reached */
}

uintptr
sys_wait(va_list list)
{
	ulong pid;
	Waitmsg w;
	OWaitmsg *ow;

	ow = va_arg(list, OWaitmsg*);
	if(ow == nil)
		pid = pwait(nil);
	else {
		validaddr((uintptr)ow, sizeof(OWaitmsg), 1);
		evenaddr((uintptr)ow);
		pid = pwait(&w);
	}
	if(ow != nil){
		readnum(0, ow->pid, NUMSIZE, w.pid, NUMSIZE);
		readnum(0, ow->time+TUser*NUMSIZE, NUMSIZE, w.time[TUser], NUMSIZE);
		readnum(0, ow->time+TSys*NUMSIZE, NUMSIZE, w.time[TSys], NUMSIZE);
		readnum(0, ow->time+TReal*NUMSIZE, NUMSIZE, w.time[TReal], NUMSIZE);
		strncpy(ow->msg, w.msg, sizeof(ow->msg)-1);
		ow->msg[sizeof(ow->msg)-1] = '\0';
	}
	return pid;
}

uintptr
sysawait(va_list list)
{
	char *p;
	Waitmsg w;
	uint n;

	p = va_arg(list, char*);
	n = va_arg(list, uint);
	validaddr((uintptr)p, n, 1);
	pwait(&w);
	return (uintptr)snprint(p, n, "%d %lud %lud %lud %q",
		w.pid,
		w.time[TUser], w.time[TSys], w.time[TReal],
		w.msg);
}

void
werrstr(char *fmt, ...)
{
	va_list va;

	if(up == nil)
		return;

	va_start(va, fmt);
	vseprint(up->syserrstr, up->syserrstr+ERRMAX, fmt, va);
	va_end(va);
}

static int
generrstr(char *buf, uint nbuf)
{
	char *err;

	if(nbuf == 0)
		error(Ebadarg);
	if(nbuf > ERRMAX)
		nbuf = ERRMAX;
	validaddr((uintptr)buf, nbuf, 1);

	err = up->errstr;
	utfecpy(err, err+nbuf, buf);
	utfecpy(buf, buf+nbuf, up->syserrstr);

	up->errstr = up->syserrstr;
	up->syserrstr = err;
	
	return 0;
}

uintptr
syserrstr(va_list list)
{
	char *buf;
	uint len;

	buf = va_arg(list, char*);
	len = va_arg(list, uint);
	return (uintptr)generrstr(buf, len);
}

/* compatibility for old binaries */
uintptr
sys_errstr(va_list list)
{
	return (uintptr)generrstr(va_arg(list, char*), 64);
}

uintptr
sysnotify(va_list list)
{
	int (*f)(void*, char*);
	f = va_arg(list, void*);
	if(f != nil)
		validaddr((uintptr)f, sizeof(void*), 0);
	up->notify = f;
	return 0;
}

uintptr
sysnoted(va_list list)
{
	if(va_arg(list, int) != NRSTR && !up->notified)
		error(Egreg);
	return 0;
}

uintptr
syssegbrk(va_list list)
{
	int i;
	uintptr addr;
	Segment *s;

	addr = va_arg(list, uintptr);
	for(i = 0; i < NSEG; i++) {
		s = up->seg[i];
		if(s == nil || addr < s->base || addr >= s->top)
			continue;
		switch(s->type&SG_TYPE) {
		case SG_TEXT:
		case SG_DATA:
		case SG_STACK:
		case SG_PHYSICAL:
		case SG_FIXED:
		case SG_STICKY:
			error(Ebadarg);
		default:
			return ibrk(va_arg(list, uintptr), i);
		}
	}
	error(Ebadarg);
	return 0;	/* not reached */
}

uintptr
syssegattach(va_list list)
{
	int attr;
	char *name;
	uintptr va;
	ulong len;

	attr = va_arg(list, int);
	name = va_arg(list, char*);
	va = va_arg(list, uintptr);
	len = va_arg(list, ulong);
	validaddr((uintptr)name, 1, 0);
	name = validnamedup(name, 1);
	if(waserror()){
		free(name);
		nexterror();
	}
	va = segattach(attr, name, va, len);
	free(name);
	poperror();
	return va;
}

uintptr
syssegdetach(va_list list)
{
	int i;
	uintptr addr;
	Segment *s;

	addr = va_arg(list, uintptr);

	qlock(&up->seglock);
	if(waserror()){
		qunlock(&up->seglock);
		nexterror();
	}

	s = nil;
	for(i = 0; i < NSEG; i++)
		if((s = up->seg[i]) != nil) {
			qlock(s);
			if((addr >= s->base && addr < s->top) ||
			   (s->top == s->base && addr == s->base))
				goto found;
			qunlock(s);
		}

	error(Ebadarg);

found:
	/*
	 * Check we are not detaching the initial stack segment.
	 */
	if(s == up->seg[SSEG]){
		qunlock(s);
		error(Ebadarg);
	}
	up->seg[i] = nil;
	qunlock(s);
	putseg(s);
	qunlock(&up->seglock);
	poperror();

	/* Ensure we flush any entries from the lost segment */
	flushmmu();
	return 0;
}

uintptr
syssegfree(va_list list)
{
	Segment *s;
	uintptr from, to;

	from = va_arg(list, uintptr);
	to = va_arg(list, ulong);
	to += from;
	if(to < from)
		error(Ebadarg);
	s = seg(up, from, 1);
	if(s == nil)
		error(Ebadarg);
	to &= ~(BY2PG-1);
	from = PGROUND(from);
	if(from >= to) {
		qunlock(s);
		return 0;
	}
	if(to > s->top) {
		qunlock(s);
		error(Ebadarg);
	}
	mfreeseg(s, from, (to - from) / BY2PG);
	qunlock(s);
	flushmmu();
	return 0;
}

/* For binary compatibility */
uintptr
sysbrk_(va_list list)
{
	return ibrk(va_arg(list, uintptr), BSEG);
}

uintptr
sysrendezvous(va_list list)
{
	uintptr tag, val, new;
	Proc *p, **l;

	tag = va_arg(list, uintptr);
	new = va_arg(list, uintptr);
	l = &REND(up->rgrp, tag);

	lock(up->rgrp);
	for(p = *l; p != nil; p = p->rendhash) {
		if(p->rendtag == tag) {
			*l = p->rendhash;
			val = p->rendval;
			p->rendval = new;
			unlock(up->rgrp);

			ready(p);

			return val;
		}
		l = &p->rendhash;
	}

	/* Going to sleep here */
	up->rendtag = tag;
	up->rendval = new;
	up->rendhash = *l;
	*l = up;
	up->state = Rendezvous;
	unlock(up->rgrp);

	sched();

	return up->rendval;
}

/*
 * The implementation of semaphores is complicated by needing
 * to avoid rescheduling in syssemrelease, so that it is safe
 * to call from real-time processes.  This means syssemrelease
 * cannot acquire any qlocks, only spin locks.
 * 
 * Semacquire and semrelease must both manipulate the semaphore
 * wait list.  Lock-free linked lists only exist in theory, not
 * in practice, so the wait list is protected by a spin lock.
 * 
 * The semaphore value *addr is stored in user memory, so it
 * cannot be read or written while holding spin locks.
 * 
 * Thus, we can access the list only when holding the lock, and
 * we can access the semaphore only when not holding the lock.
 * This makes things interesting.  Note that sleep's condition function
 * is called while holding two locks - r and up->rlock - so it cannot
 * access the semaphore value either.
 * 
 * An acquirer announces its intention to try for the semaphore
 * by putting a Sema structure onto the wait list and then
 * setting Sema.waiting.  After one last check of semaphore,
 * the acquirer sleeps until Sema.waiting==0.  A releaser of n
 * must wake up n acquirers who have Sema.waiting set.  It does
 * this by clearing Sema.waiting and then calling wakeup.
 * 
 * There are three interesting races here.  
 
 * The first is that in this particular sleep/wakeup usage, a single
 * wakeup can rouse a process from two consecutive sleeps!  
 * The ordering is:
 * 
 * 	(a) set Sema.waiting = 1
 * 	(a) call sleep
 * 	(b) set Sema.waiting = 0
 * 	(a) check Sema.waiting inside sleep, return w/o sleeping
 * 	(a) try for semaphore, fail
 * 	(a) set Sema.waiting = 1
 * 	(a) call sleep
 * 	(b) call wakeup(a)
 * 	(a) wake up again
 * 
 * This is okay - semacquire will just go around the loop
 * again.  It does mean that at the top of the for(;;) loop in
 * semacquire, phore.waiting might already be set to 1.
 * 
 * The second is that a releaser might wake an acquirer who is
 * interrupted before he can acquire the lock.  Since
 * release(n) issues only n wakeup calls -- only n can be used
 * anyway -- if the interrupted process is not going to use his
 * wakeup call he must pass it on to another acquirer.
 * 
 * The third race is similar to the second but more subtle.  An
 * acquirer sets waiting=1 and then does a final canacquire()
 * before going to sleep.  The opposite order would result in
 * missing wakeups that happen between canacquire and
 * waiting=1.  (In fact, the whole point of Sema.waiting is to
 * avoid missing wakeups between canacquire() and sleep().) But
 * there can be spurious wakeups between a successful
 * canacquire() and the following semdequeue().  This wakeup is
 * not useful to the acquirer, since he has already acquired
 * the semaphore.  Like in the previous case, though, the
 * acquirer must pass the wakeup call along.
 * 
 * This is all rather subtle.  The code below has been verified
 * with the spin model /sys/src/9/port/semaphore.p.  The
 * original code anticipated the second race but not the first
 * or third, which were caught only with spin.  The first race
 * is mentioned in /sys/doc/sleep.ps, but I'd forgotten about it.
 * It was lucky that my abstract model of sleep/wakeup still managed
 * to preserve that behavior.
 *
 * I remain slightly concerned about memory coherence
 * outside of locks.  The spin model does not take 
 * queued processor writes into account so we have to
 * think hard.  The only variables accessed outside locks
 * are the semaphore value itself and the boolean flag
 * Sema.waiting.  The value is only accessed with cmpswap,
 * whose job description includes doing the right thing as
 * far as memory coherence across processors.  That leaves
 * Sema.waiting.  To handle it, we call coherence() before each
 * read and after each write.		- rsc
 */

/* Add semaphore p with addr a to list in seg. */
static void
semqueue(Segment *s, long *a, Sema *p)
{
	memset(p, 0, sizeof *p);
	p->addr = a;
	lock(&s->sema);	/* uses s->sema.Rendez.Lock, but no one else is */
	p->next = &s->sema;
	p->prev = s->sema.prev;
	p->next->prev = p;
	p->prev->next = p;
	unlock(&s->sema);
}

/* Remove semaphore p from list in seg. */
static void
semdequeue(Segment *s, Sema *p)
{
	lock(&s->sema);
	p->next->prev = p->prev;
	p->prev->next = p->next;
	unlock(&s->sema);
}

/* Wake up n waiters with addr a on list in seg. */
static void
semwakeup(Segment *s, long *a, long n)
{
	Sema *p;
	
	lock(&s->sema);
	for(p=s->sema.next; p!=&s->sema && n>0; p=p->next){
		if(p->addr == a && p->waiting){
			p->waiting = 0;
			coherence();
			wakeup(p);
			n--;
		}
	}
	unlock(&s->sema);
}

/* Add delta to semaphore and wake up waiters as appropriate. */
static long
semrelease(Segment *s, long *addr, long delta)
{
	long value;

	do
		value = *addr;
	while(!cmpswap(addr, value, value+delta));
	semwakeup(s, addr, delta);
	return value+delta;
}

/* Try to acquire semaphore using compare-and-swap */
static int
canacquire(long *addr)
{
	long value;
	
	while((value=*addr) > 0)
		if(cmpswap(addr, value, value-1))
			return 1;
	return 0;
}		

/* Should we wake up? */
static int
semawoke(void *p)
{
	coherence();
	return !((Sema*)p)->waiting;
}

/* Acquire semaphore (subtract 1). */
static int
semacquire(Segment *s, long *addr, int block)
{
	int acquired;
	Sema phore;

	if(canacquire(addr))
		return 1;
	if(!block)
		return 0;

	acquired = 0;
	semqueue(s, addr, &phore);
	for(;;){
		phore.waiting = 1;
		coherence();
		if(canacquire(addr)){
			acquired = 1;
			break;
		}
		if(waserror())
			break;
		sleep(&phore, semawoke, &phore);
		poperror();
	}
	semdequeue(s, &phore);
	coherence();	/* not strictly necessary due to lock in semdequeue */
	if(!phore.waiting)
		semwakeup(s, addr, 1);
	if(!acquired)
		nexterror();
	return 1;
}

/* Acquire semaphore or time-out */
static int
tsemacquire(Segment *s, long *addr, ulong ms)
{
	int acquired, timedout;
	ulong t;
	Sema phore;

	if(canacquire(addr))
		return 1;
	if(ms == 0)
		return 0;
	acquired = timedout = 0;
	semqueue(s, addr, &phore);
	for(;;){
		phore.waiting = 1;
		coherence();
		if(canacquire(addr)){
			acquired = 1;
			break;
		}
		if(waserror())
			break;
		t = MACHP(0)->ticks;
		tsleep(&phore, semawoke, &phore, ms);
		t = TK2MS(MACHP(0)->ticks - t);
		poperror();
		if(t >= ms){
			timedout = 1;
			break;
		}
		ms -= t;
	}
	semdequeue(s, &phore);
	coherence();	/* not strictly necessary due to lock in semdequeue */
	if(!phore.waiting)
		semwakeup(s, addr, 1);
	if(timedout)
		return 0;
	if(!acquired)
		nexterror();
	return 1;
}

uintptr
syssemacquire(va_list list)
{
	int block;
	long *addr;
	Segment *s;

	addr = va_arg(list, long*);
	block = va_arg(list, int);
	evenaddr((uintptr)addr);
	s = seg(up, (uintptr)addr, 0);
	if(s == nil || (s->type&SG_RONLY) != 0 || (uintptr)addr+sizeof(long) > s->top){
		validaddr((uintptr)addr, sizeof(long), 1);
		error(Ebadarg);
	}
	if(*addr < 0)
		error(Ebadarg);
	return (uintptr)semacquire(s, addr, block);
}

uintptr
systsemacquire(va_list list)
{
	long *addr;
	ulong ms;
	Segment *s;

	addr = va_arg(list, long*);
	ms = va_arg(list, ulong);
	evenaddr((uintptr)addr);
	s = seg(up, (uintptr)addr, 0);
	if(s == nil || (s->type&SG_RONLY) != 0 || (uintptr)addr+sizeof(long) > s->top){
		validaddr((uintptr)addr, sizeof(long), 1);
		error(Ebadarg);
	}
	if(*addr < 0)
		error(Ebadarg);
	return (uintptr)tsemacquire(s, addr, ms);
}

uintptr
syssemrelease(va_list list)
{
	long *addr, delta;
	Segment *s;

	addr = va_arg(list, long*);
	delta = va_arg(list, long);
	evenaddr((uintptr)addr);
	s = seg(up, (uintptr)addr, 0);
	if(s == nil || (s->type&SG_RONLY) != 0 || (uintptr)addr+sizeof(long) > s->top){
		validaddr((uintptr)addr, sizeof(long), 1);
		error(Ebadarg);
	}
	/* delta == 0 is a no-op, not a release */
	if(delta < 0 || *addr < 0)
		error(Ebadarg);
	return (uintptr)semrelease(s, addr, delta);
}

/* For binary compatibility */
uintptr
sys_nsec(va_list list)
{
	vlong *v;

	/* return in register on 64bit machine */
	if(sizeof(uintptr) == sizeof(vlong)){
		USED(list);
		return (uintptr)todget(nil);
	}

	v = va_arg(list, vlong*);
	evenaddr((uintptr)v);
	validaddr((uintptr)v, sizeof(vlong), 1);
	*v = todget(nil);
	return 0;
}
