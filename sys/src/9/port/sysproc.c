#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"ureg.h"
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
abortion(void)
{
	pexit("fork aborted", 1);
}

uintptr
sysrfork(va_list list)
{
	/*
	 * Code using RFNOMNT expects to block all but
	 * the following devices.
	 */
	static char nomntdevs[] = "|decp";

	ulong pid, flag;
	int n, i;
	Proc *p;

	flag = va_arg(list, ulong);
	/* Check flags before we commit */
	if((flag & (RFFDG|RFCFDG)) == (RFFDG|RFCFDG))
		error(Ebadarg);
	if((flag & (RFNAMEG|RFCNAMEG)) == (RFNAMEG|RFCNAMEG))
		error(Ebadarg);
	if((flag & (RFENVG|RFCENVG)) == (RFENVG|RFCENVG))
		error(Ebadarg);

	if((flag&RFPROC) == 0) {
		Fgrp *ofg;
		Pgrp *opg;
		Rgrp *org;
		Egrp *oeg;

		if(flag & (RFMEM|RFNOWAIT))
			error(Ebadarg);

		ofg = up->fgrp;
		opg = up->pgrp;
		org = up->rgrp;
		oeg = up->egrp;

		if(waserror()){
			if(up->fgrp != ofg){
				closefgrp(up->fgrp);
				up->fgrp = ofg;
			}
			if(up->pgrp != opg){
				closepgrp(up->pgrp);
				up->pgrp = opg;
			}
			if(up->rgrp != org){
				closergrp(up->rgrp);
				up->rgrp = org;
			}
			if(up->egrp != oeg){
				closeegrp(up->egrp);
				up->egrp = oeg;
			}
			nexterror();
		}

		/* File descriptors */
		if(flag & (RFFDG|RFCFDG)) {
			if(flag & RFFDG)
				up->fgrp = dupfgrp(ofg);
			else
				up->fgrp = dupfgrp(nil);
		}

		/* Process group */
		if(flag & (RFNAMEG|RFCNAMEG)) {
			up->pgrp = newpgrp();
			pgrpcpy(up->pgrp, opg, flag);
		}

		/* Rendezvous group */
		if(flag & RFREND)
			up->rgrp = newrgrp();

		/* Environment group */
		if(flag & (RFENVG|RFCENVG)) {
			up->egrp = newegrp();
			if(flag & RFENVG)
				envcpy(up->egrp, oeg);
		}

		if(ofg != up->fgrp)
			closefgrp(ofg);
		if(opg != up->pgrp)
			closepgrp(opg);
		if(org != up->rgrp)
			closergrp(org);
		if(oeg != up->egrp)
			closeegrp(oeg);

		poperror();

		if(flag & RFNOMNT)
			devmask(up->pgrp, 1, nomntdevs);

		if(flag & RFNOTEG){
			qlock(&up->debug);
			setnoteid(up, 0);	/* can't error() with 0 argument */
			qunlock(&up->debug);
		}
		return 0;
	}

	if((p = newproc()) == nil)
		error("no procs");

	qlock(&up->debug);
	qlock(&p->debug);

	p->scallnr = up->scallnr;
	p->s = up->s;
	p->slash = up->slash;
	p->dot = up->dot;
	incref(p->dot);

	p->nnote = 0;
 	p->notify = up->notify;
	p->notified = 0;
	p->notepending = 0;
	p->lastnote = nil;

	if((flag & RFNOTEG) == 0)
		p->noteid = up->noteid;

	p->procmode = up->procmode;
	p->privatemem = up->privatemem;
	p->noswap = up->noswap;
	p->hang = up->hang;
	if(up->procctl == Proc_tracesyscall)
		p->procctl = Proc_tracesyscall;
	p->kp = 0;

	/*
	 * Craft a return frame which will cause the child to pop out of
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
	p->kentry = up->kentry;
	p->pcycles = -p->kentry;

	pid = pidalloc(p);

	qunlock(&p->debug);
	qunlock(&up->debug);

	/* Abort the child process on error */
	if(waserror()){
		p->kp = 1;
		kprocchild(p, abortion);
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
		incref(up->fgrp);
	}

	/* Process groups */
	if(flag & (RFNAMEG|RFCNAMEG)) {
		p->pgrp = newpgrp();
		pgrpcpy(p->pgrp, up->pgrp, flag);
	}
	else {
		p->pgrp = up->pgrp;
		incref(up->pgrp);
	}

	/* Rendezvous group */
	if(flag & RFREND)
		p->rgrp = newrgrp();
	else {
		p->rgrp = up->rgrp;
		incref(up->rgrp);
	}

	/* Environment group */
	if(flag & (RFENVG|RFCENVG)) {
		p->egrp = newegrp();
		if(flag & RFENVG)
			envcpy(p->egrp, up->egrp);
	}
	else {
		p->egrp = up->egrp;
		incref(up->egrp);
	}

	procfork(p);

	poperror();	/* abortion */

	if(flag & RFNOMNT)
		devmask(p->pgrp, 1, nomntdevs);

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

	procpriority(p, up->basepri, up->fixedpri);
	if(up->wired)
		procwired(p, up->affinity);

	ready(p);
	sched();
	return pid;
}

ulong
beswal(ulong l)
{
	uchar *p;

	p = (uchar*)&l;
	return (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
}

uvlong
beswav(uvlong v)
{
	uchar *p;

	p = (uchar*)&v;
	return ((uvlong)p[0]<<56) | ((uvlong)p[1]<<48) | ((uvlong)p[2]<<40)
				  | ((uvlong)p[3]<<32) | ((uvlong)p[4]<<24)
				  | ((uvlong)p[5]<<16) | ((uvlong)p[6]<<8)
				  | (uvlong)p[7];
}

uintptr
sysexec(va_list list)
{
	union {
		struct {
			Exec;
			uvlong	hdr[1];
		} ehdr;
		char buf[256];
	} u;
	char *progarg[32+1], **argv, **argp;
	char *file, *elem, *args, *charp, *a, *e;
	int i, n, indir, nargs, argc;
	ulong magic, ssize, nbytes;
	uintptr entry, text, data, bss, adata, abss, ebss, tstk, align;
	Segment *s, *ts;
	Image *img;
	Tos *tos;
	Chan *tc;
	Fgrp *f;

	file = va_arg(list, char*);
	validaddr((uintptr)file, 1, 0);
	argp = va_arg(list, char**);
	evenaddr((uintptr)argp);
	validaddr((uintptr)argp, 2*BY2WD, 0);
	if(*argp == nil)
		error(Ebadarg);

	if(waserror()){
		/* Disaster after commit */
		if(up->seg[SSEG] == nil)
			pexit(up->errstr, 1);
		qlock(&up->seglock);
		s = up->seg[ESEG];
		up->seg[ESEG] = nil;
		qunlock(&up->seglock);
		if(s != nil) {
			putseg(s);
			flushmmu();
		}
		nexterror();
	}

	file = validnamedup(file, 1);
	if(waserror()){
		free(file);
		nexterror();
	}

	tc = namec(file, Aopen, OEXEC, 0);

	/* Last path element becomes up->text (and argv[0] for script) */
	elem = nil;
	kstrdup(&elem, up->genbuf);

	poperror();
	if(waserror()){
		free(elem);
		free(file);
		nexterror();
	}
	if(waserror()){
		cclose(tc);
		nexterror();
	}

	/* Attach new stack segment, place it below current stack */
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
	qunlock(&up->seglock);
	poperror();	/* up->seglock */

	/* Setup TOS; paged in on demand */
	tos = (Tos*)(tstk - sizeof(Tos));
	memset(tos, 0, sizeof(Tos));
	tos->cyclefreq = m->cyclefreq;

	/* Stash interpreter args below TOS */
	charp = (char*)tos;
	argc = 0;

	/* Read a.out(6) header */
	for(indir=0;;indir++){
		n = devtab[tc->type]->read(tc, u.buf, sizeof(u.buf), 0);
		if(n >= sizeof(Exec)) {
			magic = beswal(u.ehdr.magic);
			if(magic == AOUT_MAGIC)
				break; /* for binary */
		}

		/* Process #! /bin/sh args ... */
		if(n <= 2 || u.buf[0] != '#' || u.buf[1] != '!'
		|| (a = memchr(u.buf+2, '\n', n-2)) == nil)
			error(Ebadexec);
		*a = '\0';

		/* First arg becomes complete file name */
		if(indir == 0) {
			argc++;
			n = strlen(file)+1;
			charp -= n;
			if(charp <= (char*)tstk-USTKSIZE)
				error(Enovmem);
			memmove(charp, file, n);
		} else if(indir >= 8)
			error(Ebadexec);

		i = tokenize(u.buf+2, progarg, nelem(progarg));
		if(i < 1 || i >= nelem(progarg))
			error(Ebadexec);

		/* Push interpreter args in reverse order */
		argc += i;
		while(--i >= 0) {
			a = progarg[i];
			n = strlen(a)+1;
			charp -= n;
			if(charp <= (char*)tstk-USTKSIZE)
				error(Enovmem);
			memmove(charp, a, n);
		}
		cclose(tc);
		poperror();	/* tc */

		/* Open interpreter */
		tc = namec(progarg[0], Aopen, OEXEC, 0);
		if(waserror()){
			cclose(tc);
			nexterror();
		}
	}

	/* Process a.out(6) header */
	if(magic & HDR_MAGIC) {
		if(n < sizeof(u.ehdr))
			error(Ebadexec);
		entry = beswav(u.ehdr.hdr[0]);
		text = UTZERO+sizeof(u.ehdr);
	} else {
		entry = beswal(u.ehdr.entry);
		text = UTZERO+sizeof(Exec);
	}
	if(entry < text)
		error(Ebadexec);
	text += beswal(u.ehdr.text);
	if(text <= entry || text >= (USTKTOP-USTKSIZE))
		error(Ebadexec);

	switch(magic){
	case S_MAGIC:	/* 2MB segment alignment for amd64 */
		align = 0x1fffff;
		break;
	case P_MAGIC:	/* 16K segment alignment for spim */
	case V_MAGIC:	/* 16K segment alignment for mips */
		align = 0x3fff;
		break;
	case R_MAGIC:	/* 64K segment alignment for arm64 */
		align = 0xffff;
		break;
	default:
		align = BY2PG-1;
	}

	adata = (text+align) & ~align;
	text -= UTZERO;
	data = beswal(u.ehdr.data);
	bss = beswal(u.ehdr.bss);
	align = BY2PG-1;
	
	abss = (adata + data + align) & ~align;
	ebss = (adata + data + bss + align) & ~align;
	if(adata >= (USTKTOP-USTKSIZE) || abss >= (USTKTOP-USTKSIZE) || ebss >= (USTKTOP-USTKSIZE))
		error(Ebadexec);

	/* Replace argv[0] with script name */
	if(indir){
		n = strlen(charp)+1;
		charp += n;
		n = strlen(elem)+1;
		charp -= n;
		if(charp <= (char*)tstk-USTKSIZE)
			error(Enovmem);
		memmove(charp, elem, n);
		argp++;
	}

	/* Count user arguments */
	nbytes = (char*)tstk - charp;
	for(i = 0; (a = argp[i]) != nil; i++) {
		validaddr((uintptr)a, 1, 0);
		if((e = vmemchr(a, 0, USTKSIZE-nbytes)) == nil)
			error(Ebadarg);
		nbytes += (e - a) + 1;
		if(nbytes >= USTKSIZE)
			error(Enovmem);
		/* crossing page boundary? */
		if(((uintptr)&argp[i]&(BY2PG-1)) >= BY2PG-BY2WD)
			validaddr((uintptr)&argp[i+1], BY2WD, 0);
	}
	argc += i;
	if(argc < 1)
		error(Ebadarg);

	ssize = BY2WD*((ulong)argc+1) + ((nbytes+(BY2WD-1)) & ~(BY2WD-1));

	/*
	 * 8-byte align SP for those (e.g. sparc) that need it.
	 * execregs() will subtract another 4 bytes for argc.
	 */
	if(BY2WD == 4 && (ssize+4) & 7)
		ssize += 4;

	if(PGROUND(ssize) >= USTKSIZE)
		error(Enovmem);

	/* save interpreter args */
	a = charp;

	/*
	 * Now we know the actual stack size and argument count,
	 * copy in the strings to charp and build argv[] array.
	 */
	argv = (char**)(tstk - ssize);
	charp = (char*)tstk - nbytes;

	i = 0;
	if(indir)	/* move interpreter args down before user args */
	for(; i < argc && a < (char*)tos; i++) {
		n = strlen(a) + 1;
		memmove(charp, a, n);
		argv[i] = charp + (USTKTOP-tstk);
		charp += n;
		a += n;
	}
	for(; i < argc; i++) {
		a = *argp++;
		validaddr((uintptr)a, 1, 0);
		if(charp >= (char*)tos
		|| (e = vmemchr(a, 0, (char*)tos-charp)) == nil)
			error(Ebadarg);
		n = (e - a) + 1;
		memmove(charp, a, n);
		argv[i] = charp + (USTKTOP-tstk);
		charp += n;
	}
	argv[i] = nil;

	/* Copy args for proc(3); easiest from new process's stack */
	a = (char*)tstk - nbytes;
	nargs = charp - a;
	if(nargs > 128)	/* don't waste too much space on huge arg lists */
		nargs = 128;
	if((args = malloc(nargs)) == nil)
		error(Enomem);
	if(waserror()){
		free(args);
		nexterror();
	}
	memmove(args, a, nargs);
	if(nargs>0 && args[nargs-1]!='\0'){
		/* make sure last arg is NUL-terminated */
		/* put NUL at UTF-8 character boundary */
		for(i=nargs-1; i>0; --i)
			if(fullrune(args+i, nargs-i))
				break;
		args[i] = '\0';
		nargs = i+1;
	}

	/* Attach text segment */
	/* attachimage returns a locked cache image */
	img = attachimage(tc, (PGROUND(text)+PGROUND(data))>>PGSHIFT);
	if((ts = img->s) != nil && ts->flen == text){
		assert(ts->image == img);
		incref(ts);
		putimage(img);
	} else {
		if(waserror()){
			putimage(img);
			nexterror();
		}
		ts = newseg(SG_TEXT | SG_RONLY, UTZERO, PGROUND(text)>>PGSHIFT);
		ts->flushme = 1;
		ts->image = img;
		ts->fstart = 0;
		ts->flen = text;
		img->s = ts;
		unlock(img);
		poperror();	/* img */
	}

	/*
	 * Committed.
	 * Free old memory.
	 * Special segments are maintained across exec
	 */
	qlock(&up->seglock);
	if(waserror()){
		qunlock(&up->seglock);
		nexterror();
	}

	for(i = SSEG; i <= BSEG; i++) {
		s = up->seg[i];
		if(s != nil) {
			/* prevent a second free if we have an error */
			up->seg[i] = nil;
			putseg(s);
		}
	}
	for(i = ESEG+1; i < NSEG; i++) {
		s = up->seg[i];
		if(s != nil && (s->type&SG_CEXEC) != 0) {
			up->seg[i] = nil;
			putseg(s);
		}
	}

	/* Text. Shared. */
	assert(ts->ref > 0);
	up->seg[TSEG] = ts;

	/* Data. Shared. */
	s = newseg(SG_DATA, adata, PGROUND(data)>>PGSHIFT);
	s->image = img;
	s->fstart = text;
	s->flen = data;
	incref(img);
	up->seg[DSEG] = s;

	/* BSS. Zero fill on demand */
	up->seg[BSEG] = newseg(SG_BSS, abss, (ebss - abss)>>PGSHIFT);

	/* Move the stack */
	s = up->seg[ESEG];
	up->seg[ESEG] = nil;
	qlock(s);
	s->base = USTKTOP-USTKSIZE;
	s->top = USTKTOP;
	relocateseg(s, USTKTOP-tstk);
	qunlock(s);
	up->seg[SSEG] = s;
	qunlock(&up->seglock);

	poperror();	/* up->seglock */
	poperror();	/* args */
	poperror();	/* tc */
	poperror();	/* elem, file */
	poperror();	/* up->seg[SSEG] */

	if(tc == img->c){
		/* avoid double caching */
		tc->flag &= ~CCACHE;
		cclunk(tc);
	}
	cclose(tc);
	free(file);

	/* Close on exec */
	if((f = up->fgrp) != nil) {
		for(i=0; i<=f->maxfd; i++)
			fdclose(i, CCEXEC);
	}

	qlock(&up->debug);
	free(up->text);
	up->text = elem;
	free(up->args);
	up->args = args;
	up->nargs = nargs;
	up->setargs = 0;

	freenotes(up);
	freenote(up->lastnote);
	up->lastnote = nil;
	up->notify = nil;
	up->notified = 0;
	up->noteureg = nil;
	up->privatemem = 0;
	up->noswap = 0;
	up->pcycles = -up->kentry;
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

	tos = (Tos*)(USTKTOP - sizeof(Tos));
	argv = (char**)(USTKTOP - ssize);

	return execregs(entry, argc, argv, tos);
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

int
donotify(Ureg *ureg)
{
	Ureg *nureg;
	char *msg;

	if(up->procctl)
		procctl();
	if(up->nnote == 0)
		return 0;

	spllo();
	qlock(&up->debug);
	msg = popnote(ureg);
	if(msg == nil){
		qunlock(&up->debug);
		splhi();
		return 0;
	}
	splhi();
	fpunotify(up);
	spllo();
	qunlock(&up->debug);

	if(up->notify == nil
	|| (nureg = notify(ureg, msg)) == nil){
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", msg);
		pexit(msg, up->lastnote->flag!=NDebug);
	}

	/* word under Ureg is old ureg */
	*(Ureg**)((uintptr)nureg-BY2WD) = up->noteureg;
	up->noteureg = nureg;

	splhi();
	return 1;
}

uintptr
sysnoted(va_list list)
{
	Ureg *nureg;
	int arg;

	arg = va_arg(list, int);

	qlock(&up->debug);
	if(up->notified){
		splhi();
		fpunoted(up);
		spllo();
	} else if(arg!=NRSTR){
		qunlock(&up->debug);
		error(Ebadarg);
	}
	qunlock(&up->debug);

	nureg = up->noteureg;
	if(!okaddr((uintptr)nureg-BY2WD, BY2WD+sizeof(Ureg), 0)){
		pprint("suicide: bad ureg in noted or call to noted when not notified\n");
		pexit("Suicide", 0);
	}

	switch(arg){
	case NCONT:
	case NRSTR:
		/* word under Ureg is old ureg */
		up->noteureg = *(Ureg**)((uintptr)nureg-BY2WD);
		/* wet floor */
	case NSAVE:
		if(noted(up->dbgreg, nureg, arg)){
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		break;
	default:
		up->lastnote->flag = NDebug;
		/* fall through */
	case NDFLT:
		noted(up->dbgreg, nureg, arg);	/* for debugging */
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", up->lastnote->msg);
		pexit(up->lastnote->msg, up->lastnote->flag!=NDebug);
	}

	/* allow next note */
	up->notified = 0;

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
	qunlock(s);
	up->seg[i] = nil;
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
	semqueue(s, addr, &phore);
	if(acquired = !waserror()){
		for(;;){
			phore.waiting = 1;
			coherence();
			if(canacquire(addr))
				break;
			sleep(&phore, semawoke, &phore);
		}
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
	int timedout, acquired;
	ulong t;
	Sema phore;

	if(canacquire(addr))
		return 1;
	if(ms == 0)
		return 0;
	timedout = 0;
	semqueue(s, addr, &phore);
	if(acquired = !waserror()){
		for(;;){
			phore.waiting = 1;
			coherence();
			if(canacquire(addr))
				break;
			t = MACHP(0)->ticks;
			tsleep(&phore, semawoke, &phore, ms);
			t = TK2MS(MACHP(0)->ticks - t);
			if(t >= ms){
				timedout = 1;
				break;
			}
			ms -= t;
		}
		poperror();
	}
	semdequeue(s, &phore);
	coherence();	/* not strictly necessary due to lock in semdequeue */
	if(!phore.waiting)
		semwakeup(s, addr, 1);
	if(!acquired)
		nexterror();
	return !timedout;
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
		return (uintptr)todget(nil, nil);
	}

	v = va_arg(list, vlong*);
	evenaddr((uintptr)v);
	validaddr((uintptr)v, sizeof(vlong), 1);
	*v = todget(nil, nil);
	return 0;
}

#include "../port/systab.h"

int
dosyscall(ulong scallnr, Sargs *args, uintptr *retp)
{
	vlong startns, stopns;
	uintptr ret;
	int s;

	m->syscall++;

	up->insyscall = 1;
	s = spllo();

	if(!waserror()){
		evenaddr((uintptr)args);
		validaddr((uintptr)args, sizeof(Sargs), 0);

		up->s = *args;
		up->scallnr = scallnr;

		if(up->procctl == Proc_tracesyscall){
			syscallfmt(scallnr, userpc(), (va_list)up->s.args);
			splhi();
			up->procctl = Proc_stopme;
			procctl();
			spllo();
			todget(nil, &startns);
		}
		if(scallnr >= nsyscall || systab[scallnr] == nil){
			postnote(up, 1, "sys: bad sys call", NDebug);
			error(Ebadarg);
		}
		up->psstate = sysctab[scallnr];
		ret = systab[scallnr]((va_list)up->s.args);			
		poperror();
		if(scallnr == NOTED){
			/* special case: noted() changes the ureg, return without setting *retp */
			splx(s);
			up->insyscall = 0;
			up->psstate = nil;
			return 1;
		}
	}else{
		/* failure: save the error buffer for errstr */
		char *e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
		ret = -1;
	}
	if(up->nerrlab){
		int i;

		print("bad errstack [%lud]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			print("sp=%#p pc=%#p\n", up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}
	*retp = ret;
	if(up->procctl == Proc_tracesyscall){
		todget(nil, &stopns);
		sysretfmt(scallnr, (va_list)up->s.args, ret, startns, stopns);
		splhi();
		up->procctl = Proc_stopme;
		procctl();
	}
	splx(s);
	up->insyscall = 0;
	up->psstate = nil;
	return 0;
}
