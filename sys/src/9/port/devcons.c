#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<authsrv.h>

void	(*consdebug)(void) = nil;
void	(*screenputs)(char*, int) = nil;

Queue*	serialoq;		/* serial console output */
Queue*	kprintoq;		/* console output, for /dev/kprint */
ulong	kprintinuse;		/* test and set whether /dev/kprint is open */
int	iprintscreenputs = 1;

int	panicking;

char	*sysname;
vlong	fasthz;

static int	readtime(ulong, char*, int);
static int	readbintime(char*, int);
static int	writetime(char*, int);
static int	writebintime(char*, int);

enum
{
	CMreboot,
	CMpanic,
	CMrdb,
};

Cmdtab rebootmsg[] =
{
	CMreboot,	"reboot",	0,
	CMpanic,	"panic",	0,
	CMrdb,		"rdb",		0,
};

void
printinit(void)
{
}

int
consactive(void)
{
	if(serialoq)
		return qlen(serialoq) > 0;
	return 0;
}

void
prflush(void)
{
	ulong now;

	now = m->ticks;
	while(consactive())
		if(m->ticks - now >= HZ)
			break;
}

static void
kmesgputs(char *str, int n)
{
	uint nn, d;

	ilock(&kmesg.lk);
	/* take the tail of huge writes */
	if(n > sizeof kmesg.buf){
		d = n - sizeof kmesg.buf;
		str += d;
		n -= d;
	}

	/* slide the buffer down to make room */
	nn = kmesg.n;
	if(nn + n >= sizeof kmesg.buf){
		d = nn + n - sizeof kmesg.buf;
		if(d)
			memmove(kmesg.buf, kmesg.buf+d, sizeof kmesg.buf-d);
		nn -= d;
	}

	/* copy the data in */
	memmove(kmesg.buf+nn, str, n);
	nn += n;
	kmesg.n = nn;
	iunlock(&kmesg.lk);
}

/*
 *   Print a string on the console.  Convert \n to \r\n for serial
 *   line consoles.  Locking of the queues is left up to the screen
 *   or uart code.  Multi-line messages to serial consoles may get
 *   interspersed with other messages.
 */
static void
putstrn0(char *str, int n, int usewrite)
{
	int m;
	char *t;
	int (*wq)(Queue*, void*, int);

	/*
	 *  how many different output devices do we need?
	 */
	kmesgputs(str, n);

	/*
	 *  if someone is reading /dev/kprint,
	 *  put the message there.
	 *  if not and there's an attached bit mapped display,
	 *  put the message there.
	 *
	 *  if there's a serial line being used as a console,
	 *  put the message there.
	 */
	wq = usewrite && islo() ? qwrite : qiwrite;
	if(kprintoq != nil && !qisclosed(kprintoq))
		(*wq)(kprintoq, str, n);
	else if(screenputs != nil)
		screenputs(str, n);

	if(serialoq == nil){
		uartputs(str, n);
		return;
	}

	while(n > 0) {
		t = memchr(str, '\n', n);
		if(t != nil) {
			m = t-str;
			(*wq)(serialoq, str, m);
			(*wq)(serialoq, "\r\n", 2);
			n -= m+1;
			str = t+1;
		} else {
			(*wq)(serialoq, str, n);
			break;
		}
	}
}

void
putstrn(char *str, int n)
{
	putstrn0(str, n, 0);
}

int
print(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	va_start(arg, fmt);
	n = vseprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	putstrn(buf, n);

	return n;
}

/*
 * Want to interlock iprints to avoid interlaced output on 
 * multiprocessor, but don't want to deadlock if one processor
 * dies during print and another has something important to say.
 * Make a good faith effort.
 */
static Lock iprintlock;
static int
iprintcanlock(Lock *l)
{
	int i;
	
	for(i=0; i<1000; i++){
		if(canlock(l))
			return 1;
		if(l->m == MACHP(m->machno))
			return 0;
		microdelay(100);
	}
	return 0;
}

int
iprint(char *fmt, ...)
{
	int n, s, locked;
	va_list arg;
	char buf[PRINTSIZE];

	s = splhi();
	va_start(arg, fmt);
	n = vseprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	locked = iprintcanlock(&iprintlock);
	if(screenputs != nil && iprintscreenputs)
		screenputs(buf, n);
	uartputs(buf, n);
	if(locked)
		unlock(&iprintlock);
	splx(s);

	return n;
}

void
panic(char *fmt, ...)
{
	int s;
	va_list arg;
	char buf[PRINTSIZE];

	kprintoq = nil;	/* don't try to write to /dev/kprint */

	if(panicking)
		for(;;);
	panicking = 1;

	s = splhi();
	strcpy(buf, "panic: ");
	va_start(arg, fmt);
	vseprint(buf+strlen(buf), buf+sizeof(buf), fmt, arg);
	va_end(arg);
	iprint("%s\n", buf);
	if(consdebug)
		(*consdebug)();
	splx(s);
	prflush();
	dumpstack();

	/* reboot cpu servers and headless machines when not debugging */
	if(getconf("*debug") == nil)
	if(cpuserver || !conf.monitor)
		exit(1);

	/* otherwise, just hang */
	while(islo()) idlehands();
	for(;;);
}

/* libmp at least contains a few calls to sysfatal; simulate with panic */
void
sysfatal(char *fmt, ...)
{
	char err[256];
	va_list arg;

	va_start(arg, fmt);
	vseprint(err, err + sizeof err, fmt, arg);
	va_end(arg);
	panic("sysfatal: %s", err);
}

void
_assert(char *fmt)
{
	panic("assert failed at %#p: %s", getcallerpc(&fmt), fmt);
}

int
pprint(char *fmt, ...)
{
	int n;
	Chan *c;
	va_list arg;
	char buf[2*PRINTSIZE];

	if(up == nil || up->fgrp == nil)
		return 0;

	c = up->fgrp->fd[2];
	if(c==nil || (c->flag&CMSG)!=0 || (c->mode!=OWRITE && c->mode!=ORDWR))
		return 0;
	n = snprint(buf, sizeof buf, "%s %lud: ", up->text, up->pid);
	va_start(arg, fmt);
	n = vseprint(buf+n, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);

	if(waserror())
		return 0;
	devtab[c->type]->write(c, buf, n, c->offset);
	poperror();

	lock(c);
	c->offset += n;
	unlock(c);

	return n;
}

enum{
	Qdir,
	Qbintime,
	Qcons,
	Qconsctl,
	Qcputime,
	Qdrivers,
	Qkmesg,
	Qkprint,
	Qhostdomain,
	Qhostowner,
	Qnull,
	Qosversion,
	Qpgrpid,
	Qpid,
	Qppid,
	Qrandom,
	Qreboot,
	Qsysname,
	Qsysstat,
	Qtime,
	Quser,
	Qzero,
	Qmordor,
	Qconfig,
};

enum
{
	VLNUMSIZE=	22,
};

static Dirtab consdir[]={
	".",	{Qdir, 0, QTDIR},	0,		DMDIR|0555,
	"bintime",	{Qbintime},	24,		0664,
	"cons",		{Qcons},	0,		0660,
	"consctl",	{Qconsctl},	0,		0220,
	"cputime",	{Qcputime},	6*NUMSIZE,	0444,
	"drivers",	{Qdrivers},	0,		0444,
	"hostdomain",	{Qhostdomain},	DOMLEN,		0664,
	"hostowner",	{Qhostowner},	0,		0664,
	"kmesg",	{Qkmesg},	0,		0440,
	"kprint",	{Qkprint, 0, QTEXCL},	0,	DMEXCL|0440,
	"null",		{Qnull},	0,		0666,
	"osversion",	{Qosversion},	0,		0444,
	"pgrpid",	{Qpgrpid},	NUMSIZE,	0444,
	"pid",		{Qpid},		NUMSIZE,	0444,
	"ppid",		{Qppid},	NUMSIZE,	0444,
	"random",	{Qrandom},	0,		0444,
	"reboot",	{Qreboot},	0,		0664,
	"sysname",	{Qsysname},	0,		0664,
	"sysstat",	{Qsysstat},	0,		0666,
	"time",		{Qtime},	NUMSIZE+3*VLNUMSIZE,	0664,
	"user",		{Quser},	0,		0666,
	"zero",		{Qzero},	0,		0444,
	"config",	{Qconfig},	0,		0444,
	"mordor",	{Qmordor},	0,		0666,
};

int
readnum(ulong off, char *buf, ulong n, ulong val, int size)
{
	char tmp[64];

	snprint(tmp, sizeof(tmp), "%*lud", size-1, val);
	tmp[size-1] = ' ';
	if(off >= size)
		return 0;
	if(off+n > size)
		n = size-off;
	memmove(buf, tmp+off, n);
	return n;
}

int
readstr(ulong off, char *buf, ulong n, char *str)
{
	int size;

	size = strlen(str);
	if(off >= size)
		return 0;
	if(off+n > size)
		n = size-off;
	memmove(buf, str+off, n);
	return n;
}

static void
consinit(void)
{
	todinit();
	randominit();
}

static Chan*
consattach(char *spec)
{
	return devattach('c', spec);
}

static Walkqid*
conswalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name,nname, consdir, nelem(consdir), devgen);
}

static int
consstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, consdir, nelem(consdir), devgen);
}

static Chan*
consopen(Chan *c, int omode)
{
	c->aux = nil;
	c = devopen(c, omode, consdir, nelem(consdir), devgen);
	switch((ulong)c->qid.path){
	case Qkprint:
		if(tas(&kprintinuse) != 0){
			c->flag &= ~COPEN;
			error(Einuse);
		}
		if(kprintoq == nil){
			kprintoq = qopen(8*1024, Qcoalesce, 0, 0);
			if(kprintoq == nil){
				c->flag &= ~COPEN;
				error(Enomem);
			}
			qnoblock(kprintoq, 1);
		}else
			qreopen(kprintoq);
		c->iounit = qiomaxatomic;
		break;
	}
	return c;
}

static void
consclose(Chan *c)
{
	switch((ulong)c->qid.path){
	/* close of kprint allows other opens */
	case Qkprint:
		if(c->flag & COPEN){
			kprintinuse = 0;
			qhangup(kprintoq, nil);
		}
		break;
	}
}

static long
consread(Chan *c, void *buf, long n, vlong off)
{
	ulong l;
	Mach *mp;
	char *b, *bp;
	char tmp[256];
	int i, k, id;
	vlong offset = off;
	extern char configfile[];

	if(n <= 0)
		return n;

	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, buf, n, consdir, nelem(consdir), devgen);

	case Qcons:
		error(Egreg);

	case Qcputime:
		k = offset;
		if(k >= 6*NUMSIZE)
			return 0;
		if(k+n > 6*NUMSIZE)
			n = 6*NUMSIZE - k;
		/* easiest to format in a separate buffer and copy out */
		for(i=0; i<6 && NUMSIZE*i<k+n; i++){
			l = up->time[i];
			if(i == TReal)
				l = MACHP(0)->ticks - l;
			readnum(0, tmp+NUMSIZE*i, NUMSIZE, tk2ms(l), NUMSIZE);
		}
		memmove(buf, tmp+k, n);
		return n;

	case Qkmesg:
		/*
		 * This is unlocked to avoid tying up a process
		 * that's writing to the buffer.  kmesg.n never 
		 * gets smaller, so worst case the reader will
		 * see a slurred buffer.
		 */
		if(off >= kmesg.n)
			n = 0;
		else{
			if(off+n > kmesg.n)
				n = kmesg.n - off;
			memmove(buf, kmesg.buf+off, n);
		}
		return n;
		
	case Qkprint:
		return qread(kprintoq, buf, n);

	case Qpgrpid:
		return readnum((ulong)offset, buf, n, up->pgrp->pgrpid, NUMSIZE);

	case Qpid:
		return readnum((ulong)offset, buf, n, up->pid, NUMSIZE);

	case Qppid:
		return readnum((ulong)offset, buf, n, up->parentpid, NUMSIZE);

	case Qtime:
		return readtime((ulong)offset, buf, n);

	case Qbintime:
		return readbintime(buf, n);

	case Qhostowner:
		return readstr((ulong)offset, buf, n, eve);

	case Qhostdomain:
		return readstr((ulong)offset, buf, n, hostdomain);

	case Quser:
		return readstr((ulong)offset, buf, n, up->user);

	case Qnull:
		return 0;

	case Qconfig:
		return readstr((ulong)offset, buf, n, configfile);

	case Qsysstat:
		b = smalloc(conf.nmach*(NUMSIZE*11+1) + 1);	/* +1 for NUL */
		bp = b;
		for(id = 0; id < MAXMACH; id++) {
			if(active.machs[id]) {
				mp = MACHP(id);
				readnum(0, bp, NUMSIZE, id, NUMSIZE);
				bp += NUMSIZE;
				readnum(0, bp, NUMSIZE, mp->cs, NUMSIZE);
				bp += NUMSIZE;
				readnum(0, bp, NUMSIZE, mp->intr, NUMSIZE);
				bp += NUMSIZE;
				readnum(0, bp, NUMSIZE, mp->syscall, NUMSIZE);
				bp += NUMSIZE;
				readnum(0, bp, NUMSIZE, mp->pfault, NUMSIZE);
				bp += NUMSIZE;
				readnum(0, bp, NUMSIZE, mp->tlbfault, NUMSIZE);
				bp += NUMSIZE;
				readnum(0, bp, NUMSIZE, mp->tlbpurge, NUMSIZE);
				bp += NUMSIZE;
				readnum(0, bp, NUMSIZE, mp->load, NUMSIZE);
				bp += NUMSIZE;
				l = mp->perf.period;
				if(l == 0)
					l = 1;
				readnum(0, bp, NUMSIZE,
					(mp->perf.avg_inidle*100)/l, NUMSIZE);
				bp += NUMSIZE;
				readnum(0, bp, NUMSIZE,
					(mp->perf.avg_inintr*100)/l, NUMSIZE);
				bp += NUMSIZE;
				*bp++ = '\n';
			}
		}
		if(waserror()){
			free(b);
			nexterror();
		}
		n = readstr((ulong)offset, buf, n, b);
		free(b);
		poperror();
		return n;

	case Qsysname:
		if(sysname == nil)
			return 0;
		return readstr((ulong)offset, buf, n, sysname);

	case Qrandom:
		return randomread(buf, n);

	case Qdrivers:
		b = smalloc(READSTR);
		k = 0;
		for(i = 0; devtab[i] != nil; i++)
			k += snprint(b+k, READSTR-k, "#%C %s\n",
				devtab[i]->dc, devtab[i]->name);
		if(waserror()){
			free(b);
			nexterror();
		}
		n = readstr((ulong)offset, buf, n, b);
		poperror();
		free(b);
		return n;

	case Qzero:
		memset(buf, 0, n);
		return n;
	
	case Qmordor:
		error("one does not simply read from mordor");
		return 0;

	case Qosversion:
		snprint(tmp, sizeof tmp, "2000");
		n = readstr((ulong)offset, buf, n, tmp);
		return n;

	default:
		print("consread %#llux\n", c->qid.path);
		error(Egreg);
	}
	return -1;		/* never reached */
}

static long
conswrite(Chan *c, void *va, long n, vlong off)
{
	char buf[256];
	long l, bp;
	char *a;
	Mach *mp;
	int id;
	ulong offset;
	Cmdbuf *cb;
	Cmdtab *ct;

	a = va;
	offset = off;

	switch((ulong)c->qid.path){
	case Qcons:
		/*
		 * Can't page fault in putstrn, so copy the data locally.
		 */
		l = n;
		while(l > 0){
			bp = l;
			if(bp > sizeof buf)
				bp = sizeof buf;
			memmove(buf, a, bp);
			putstrn0(buf, bp, 1);
			a += bp;
			l -= bp;
		}
		break;

	case Qconsctl:
		error(Egreg);

	case Qtime:
		if(!iseve())
			error(Eperm);
		return writetime(a, n);

	case Qbintime:
		if(!iseve())
			error(Eperm);
		return writebintime(a, n);

	case Qhostowner:
		return hostownerwrite(a, n);

	case Qhostdomain:
		return hostdomainwrite(a, n);

	case Quser:
		return userwrite(a, n);

	case Qnull:
		break;

	case Qconfig:
		error(Eperm);
		break;

	case Qreboot:
		if(!iseve())
			error(Eperm);
		cb = parsecmd(a, n);

		if(waserror()) {
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, rebootmsg, nelem(rebootmsg));
		switch(ct->index) {
		case CMreboot:
			rebootcmd(cb->nf-1, cb->f+1);
			break;
		case CMpanic:
			*(ulong*)0=0;
			panic("/dev/reboot");
		case CMrdb:
			if(consdebug == nil)
				consdebug = rdb;
			consdebug();
			break;
		}
		poperror();
		free(cb);
		break;

	case Qsysstat:
		for(id = 0; id < MAXMACH; id++) {
			if(active.machs[id]) {
				mp = MACHP(id);
				mp->cs = 0;
				mp->intr = 0;
				mp->syscall = 0;
				mp->pfault = 0;
				mp->tlbfault = 0;
				mp->tlbpurge = 0;
			}
		}
		break;

	case Qsysname:
		if(offset != 0)
			error(Ebadarg);
		if(n <= 0 || n >= sizeof buf)
			error(Ebadarg);
		strncpy(buf, a, n);
		buf[n] = 0;
		if(buf[n-1] == '\n')
			buf[n-1] = 0;
		kstrdup(&sysname, buf);
		break;
	
	case Qmordor:
		error("one does not simply write into mordor");
		return 0;

	default:
		print("conswrite: %#llux\n", c->qid.path);
		error(Egreg);
	}
	return n;
}

Dev consdevtab = {
	'c',
	"cons",

	devreset,
	consinit,
	devshutdown,
	consattach,
	conswalk,
	consstat,
	consopen,
	devcreate,
	consclose,
	consread,
	devbread,
	conswrite,
	devbwrite,
	devremove,
	devwstat,
};

static uvlong uvorder = 0x0001020304050607ULL;

static uchar*
le2vlong(vlong *to, uchar *f)
{
	uchar *t, *o;
	int i;

	t = (uchar*)to;
	o = (uchar*)&uvorder;
	for(i = 0; i < sizeof(vlong); i++)
		t[o[i]] = f[i];
	return f+sizeof(vlong);
}

static uchar*
vlong2le(uchar *t, vlong from)
{
	uchar *f, *o;
	int i;

	f = (uchar*)&from;
	o = (uchar*)&uvorder;
	for(i = 0; i < sizeof(vlong); i++)
		t[i] = f[o[i]];
	return t+sizeof(vlong);
}

static long order = 0x00010203;

static uchar*
le2long(long *to, uchar *f)
{
	uchar *t, *o;
	int i;

	t = (uchar*)to;
	o = (uchar*)&order;
	for(i = 0; i < sizeof(long); i++)
		t[o[i]] = f[i];
	return f+sizeof(long);
}

static uchar*
long2le(uchar *t, long from)
{
	uchar *f, *o;
	int i;

	f = (uchar*)&from;
	o = (uchar*)&order;
	for(i = 0; i < sizeof(long); i++)
		t[i] = f[o[i]];
	return t+sizeof(long);
}

char *Ebadtimectl = "bad time control";

/*
 *  like the old #c/time but with added info.  Return
 *
 *	secs	nanosecs	fastticks	fasthz
 */
static int
readtime(ulong off, char *buf, int n)
{
	vlong	nsec, ticks;
	long sec;
	char str[7*NUMSIZE];

	nsec = todget(&ticks);
	if(fasthz == 0LL)
		fastticks((uvlong*)&fasthz);
	sec = nsec/1000000000ULL;
	snprint(str, sizeof(str), "%*lud %*llud %*llud %*llud ",
		NUMSIZE-1, sec,
		VLNUMSIZE-1, nsec,
		VLNUMSIZE-1, ticks,
		VLNUMSIZE-1, fasthz);
	return readstr(off, buf, n, str);
}

/*
 *  set the time in seconds
 */
static int
writetime(char *buf, int n)
{
	char b[13];
	long i;
	vlong now;

	if(n >= sizeof(b))
		error(Ebadtimectl);
	strncpy(b, buf, n);
	b[n] = 0;
	i = strtol(b, 0, 0);
	if(i <= 0)
		error(Ebadtimectl);
	now = i*1000000000LL;
	todset(now, 0, 0);
	return n;
}

/*
 *  read binary time info.  all numbers are little endian.
 *  ticks and nsec are syncronized.
 */
static int
readbintime(char *buf, int n)
{
	int i;
	vlong nsec, ticks;
	uchar *b = (uchar*)buf;

	i = 0;
	if(fasthz == 0LL)
		fastticks((uvlong*)&fasthz);
	nsec = todget(&ticks);
	if(n >= 3*sizeof(uvlong)){
		vlong2le(b+2*sizeof(uvlong), fasthz);
		i += sizeof(uvlong);
	}
	if(n >= 2*sizeof(uvlong)){
		vlong2le(b+sizeof(uvlong), ticks);
		i += sizeof(uvlong);
	}
	if(n >= 8){
		vlong2le(b, nsec);
		i += sizeof(vlong);
	}
	return i;
}

/*
 *  set any of the following
 *	- time in nsec
 *	- nsec trim applied over some seconds
 *	- clock frequency
 */
static int
writebintime(char *buf, int n)
{
	uchar *p;
	vlong delta;
	long period;

	n--;
	p = (uchar*)buf + 1;
	switch(*buf){
	case 'n':
		if(n < sizeof(vlong))
			error(Ebadtimectl);
		le2vlong(&delta, p);
		todset(delta, 0, 0);
		break;
	case 'd':
		if(n < sizeof(vlong)+sizeof(long))
			error(Ebadtimectl);
		p = le2vlong(&delta, p);
		le2long(&period, p);
		todset(-1, delta, period);
		break;
	case 'f':
		if(n < sizeof(uvlong))
			error(Ebadtimectl);
		le2vlong(&fasthz, p);
		if(fasthz <= 0)
			error(Ebadtimectl);
		todsetfreq(fasthz);
		break;
	}
	return n;
}

void
cpushutdown(void)
{
	int ms, once;

	once = active.machs[m->machno];
	active.machs[m->machno] = 0;
	active.exiting = 1;

	if(once)
		iprint("cpu%d: exiting\n", m->machno);

	/* wait for any other processors to shutdown */
	spllo();
	for(ms = 5*1000; ms > 0; ms -= TK2MS(2)){
		delay(TK2MS(2));
		if(memchr(active.machs, 1, MAXMACH) == nil && consactive() == 0)
			break;
	}
}
