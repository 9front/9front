#include	"u.h"
#include	<trace.h>
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"ureg.h"
#include	"edf.h"

#include	<pool.h>

enum
{
	Qdir,
	Qtrace,
	Qargs,
	Qctl,
	Qfd,
	Qfpregs,
	Qkregs,
	Qmem,
	Qnote,
	Qnoteid,
	Qnotepg,
	Qns,
	Qppid,
	Qproc,
	Qregs,
	Qsegment,
	Qstatus,
	Qtext,
	Qwait,
	Qprofile,
	Qsyscall,
	Qwatchpt,
};

enum
{
	CMclose,
	CMclosefiles,
	CMfixedpri,
	CMhang,
	CMkill,
	CMnohang,
	CMnoswap,
	CMpri,
	CMprivate,
	CMprofile,
	CMstart,
	CMstartstop,
	CMstartsyscall,
	CMstop,
	CMwaitstop,
	CMwired,
	CMtrace,
	CMinterrupt,
	CMnointerrupt,
	/* real time */
	CMperiod,
	CMdeadline,
	CMcost,
	CMsporadic,
	CMdeadlinenotes,
	CMadmit,
	CMextra,
	CMexpel,
	CMevent,
};

enum{
	Nevents = 0x4000,
	Emask = Nevents - 1,
};

#define	STATSIZE	(2*28+12+9*12)
/*
 * Status, fd, and ns are left fully readable (0444) because of their use in debugging,
 * particularly on shared servers.
 * Arguably, ns and fd shouldn't be readable; if you'd prefer, change them to 0000
 */
Dirtab procdir[] =
{
	"args",		{Qargs},	0,			0660,
	"ctl",		{Qctl},		0,			0000,
	"fd",		{Qfd},		0,			0444,
	"fpregs",	{Qfpregs},	sizeof(FPsave),		0000,
	"kregs",	{Qkregs},	sizeof(Ureg),		0400,
	"mem",		{Qmem},		0,			0000,
	"note",		{Qnote},	0,			0000,
	"noteid",	{Qnoteid},	0,			0664,
	"notepg",	{Qnotepg},	0,			0000,
	"ns",		{Qns},		0,			0444,
	"ppid",		{Qppid},	0,			0444,
	"proc",		{Qproc},	0,			0400,
	"regs",		{Qregs},	sizeof(Ureg),		0000,
	"segment",	{Qsegment},	0,			0444,
	"status",	{Qstatus},	STATSIZE,		0444,
	"text",		{Qtext},	0,			0000,
	"wait",		{Qwait},	0,			0400,
	"profile",	{Qprofile},	0,			0400,
	"syscall",	{Qsyscall},	0,			0400,	
	"watchpt",	{Qwatchpt},	0,			0600,
};

static
Cmdtab proccmd[] = {
	CMclose,		"close",		2,
	CMclosefiles,		"closefiles",		1,
	CMfixedpri,		"fixedpri",		2,
	CMhang,			"hang",			1,
	CMnohang,		"nohang",		1,
	CMnoswap,		"noswap",		1,
	CMkill,			"kill",			1,
	CMpri,			"pri",			2,
	CMprivate,		"private",		1,
	CMprofile,		"profile",		1,
	CMstart,		"start",		1,
	CMstartstop,		"startstop",		1,
	CMstartsyscall,		"startsyscall",		1,
	CMstop,			"stop",			1,
	CMwaitstop,		"waitstop",		1,
	CMwired,		"wired",		2,
	CMtrace,		"trace",		0,
	CMinterrupt,		"interrupt",		1,
	CMnointerrupt,		"nointerrupt",		1,
	CMperiod,		"period",		2,
	CMdeadline,		"deadline",		2,
	CMcost,			"cost",			2,
	CMsporadic,		"sporadic",		1,
	CMdeadlinenotes,	"deadlinenotes",	1,
	CMadmit,		"admit",		1,
	CMextra,		"extra",		1,
	CMexpel,		"expel",		1,
	CMevent,		"event",		1,
};

/* Segment type from portdat.h */
static char *sname[]={ "Text", "Data", "Bss", "Stack", "Shared", "Phys", "Fixed", "Sticky" };

/*
 * Qids are, in path:
 *	 4 bits of file type (qids above)
 *	23 bits of process slot number + 1
 *	     in vers,
 *	32 bits of pid, for consistency checking
 */
#define	QSHIFT	5	/* location in qid of proc slot # */

#define	QID(q)		((((ulong)(q).path)&0x0000001F)>>0)
#define	SLOT(q)		(((((ulong)(q).path)&0x07FFFFFE0)>>QSHIFT)-1)
#define	PID(q)		((q).vers)
#define	NOTEID(q)	((q).vers)

static void	procctlreq(Proc*, char*, int);
static long	procctlmemio(Chan*, Proc*, uintptr, void*, long, int);
static Chan*	proctext(Chan*, Proc*);
static int	procstopped(void*);

static Traceevent *tevents;
static Lock tlock;
static int topens;
static int tproduced, tconsumed;
void (*proctrace)(Proc*, int, vlong);

static int lenwatchpt(Proc *);

static int
procgen(Chan *c, char *name, Dirtab *tab, int, int s, Dir *dp)
{
	Qid qid;
	Proc *p;
	char *ename;
	Segment *q;
	ulong pid, path, perm, len;

	if(s == DEVDOTDOT){
		mkqid(&qid, Qdir, 0, QTDIR);
		devdir(c, qid, "#p", 0, eve, 0555, dp);
		return 1;
	}

	if(c->qid.path == Qdir){
		if(s == 0){
			strcpy(up->genbuf, "trace");
			mkqid(&qid, Qtrace, -1, QTFILE);
			devdir(c, qid, up->genbuf, 0, eve, 0400, dp);
			return 1;
		}

		if(name != nil){
			/* ignore s and use name to find pid */
			pid = strtol(name, &ename, 10);
			if(pid==0 || ename[0]!='\0')
				return -1;
			s = procindex(pid);
			if(s < 0)
				return -1;
		}
		else if(--s >= conf.nproc)
			return -1;

		p = proctab(s);
		pid = p->pid;
		if(pid == 0)
			return 0;
		/*
		 * String comparison is done in devwalk so name must match its formatted pid
		*/
		snprint(up->genbuf, sizeof(up->genbuf), "%lud", pid);
		if(name != nil && strcmp(name, up->genbuf) != 0)
			return -1;
		mkqid(&qid, (s+1)<<QSHIFT, pid, QTDIR);
		devdir(c, qid, up->genbuf, 0, p->user, 0555, dp);
		return 1;
	}
	if(c->qid.path == Qtrace){
		strcpy(up->genbuf, "trace");
		mkqid(&qid, Qtrace, -1, QTFILE);
		devdir(c, qid, up->genbuf, 0, eve, 0400, dp);
		return 1;
	}
	if(s >= nelem(procdir))
		return -1;
	if(tab)
		panic("procgen");

	tab = &procdir[s];
	path = c->qid.path&~(((1<<QSHIFT)-1));	/* slot component */

	/* p->procmode determines default mode for files in /proc */
	p = proctab(SLOT(c->qid));
	perm = tab->perm;
	if(perm == 0)
		perm = p->procmode;
	else	/* just copy read bits */
		perm |= p->procmode & 0444;

	len = tab->length;
	switch(QID(c->qid)) {
	case Qwait:
		len = p->nwait;	/* incorrect size, but >0 means there's something to read */
		break;
	case Qprofile:
		q = p->seg[TSEG];
		if(q != nil && q->profile != nil) {
			len = (q->top-q->base)>>LRESPROF;
			len *= sizeof(*q->profile);
		}
		break;
	case Qwatchpt:
		len = lenwatchpt(p);
		break;
	}

	mkqid(&qid, path|tab->qid.path, c->qid.vers, QTFILE);
	devdir(c, qid, tab->name, len, p->user, perm, dp);
	return 1;
}

static void
_proctrace(Proc* p, Tevent etype, vlong ts)
{
	Traceevent *te;

	if (p->trace == 0 || topens == 0 ||
		tproduced - tconsumed >= Nevents)
		return;

	te = &tevents[tproduced&Emask];
	te->pid = p->pid;
	te->etype = etype;
	if (ts == 0)
		te->time = todget(nil);
	else
		te->time = ts;
	tproduced++;
}

static void
procinit(void)
{
	if(conf.nproc >= (1<<(16-QSHIFT))-1)
		print("warning: too many procs for devproc\n");
}

static Chan*
procattach(char *spec)
{
	return devattach('p', spec);
}

static Walkqid*
procwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, procgen);
}

static int
procstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, 0, 0, procgen);
}

/*
 *  none can't read or write state on other
 *  processes.  This is to contain access of
 *  servers running as none should they be
 *  subverted by, for example, a stack attack.
 */
static void
nonone(Proc *p)
{
	if(p == up)
		return;
	if(strcmp(up->user, "none") != 0)
		return;
	if(iseve())
		return;
	error(Eperm);
}

static void
changenoteid(Proc *p, ulong noteid)
{
	Proc *pp;
	int i;

	if(noteid <= 0)
		error(Ebadarg);
	if(noteid == p->noteid)
		return;
	if(noteid == p->pid){
		setnoteid(p, noteid);
		return;
	}
	for(i = 0; i < conf.nproc; i++){
		pp = proctab(i);
		if(pp->noteid != noteid || pp->kp)
			continue;
		if(strcmp(pp->user, p->user) == 0){
			nonone(pp);
			setnoteid(p, noteid);
			return;
		}
	}
	error(Eperm);
}

static void
postnotepg(ulong noteid, char *n, int flag)
{
	Proc *p;
	int i;

	for(i = 0; i < conf.nproc; i++){
		p = proctab(i);
		if(p == up)
			continue;
		if(p->noteid != noteid || p->kp)
			continue;
		qlock(&p->debug);
		if(p->noteid == noteid)
			postnote(p, 0, n, flag);
		qunlock(&p->debug);
	}
}

static void clearwatchpt(Proc *p);

static Chan*
procopen(Chan *c, int omode0)
{
	Proc *p;
	Chan *tc;
	int pid;
	int omode;

	if(c->qid.type & QTDIR)
		return devopen(c, omode0, 0, 0, procgen);

	if(QID(c->qid) == Qtrace){
		if (omode0 != OREAD || !iseve()) 
			error(Eperm);
		lock(&tlock);
		if (waserror()){
			topens--;
			unlock(&tlock);
			nexterror();
		}
		if (topens++ > 0)
			error("already open");
		if (tevents == nil){
			tevents = (Traceevent*)malloc(sizeof(Traceevent) * Nevents);
			if(tevents == nil)
				error(Enomem);
			tproduced = tconsumed = 0;
		}
		proctrace = _proctrace;
		unlock(&tlock);
		poperror();

		c->mode = openmode(omode0);
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}
		
	p = proctab(SLOT(c->qid));
	eqlock(&p->debug);
	if(waserror()){
		qunlock(&p->debug);
		nexterror();
	}
	pid = PID(c->qid);
	if(p->pid != pid)
		error(Eprocdied);

	omode = openmode(omode0);

	switch(QID(c->qid)){
	case Qtext:
		if(omode != OREAD)
			error(Eperm);
		nonone(p);
		qunlock(&p->debug);
		poperror();
		tc = proctext(c, p);
		tc->offset = 0;
		cclose(c);
		return tc;

	case Qstatus:
	case Qppid:
	case Qproc:
	case Qkregs:
	case Qsegment:
	case Qns:
	case Qfd:
		if(omode != OREAD)
			error(Eperm);
		break;

	case Qctl:
	case Qargs:
	case Qwait:
	case Qnoteid:
		if(omode == OREAD)
			break;
	case Qnote:
		if(p->kp)
			error(Eperm);
		break;

	case Qnotepg:
		if(p->kp || omode != OWRITE)
			error(Eperm);
		pid = p->noteid;
		break;

	case Qmem:
	case Qregs:
	case Qfpregs:
	case Qprofile:
	case Qsyscall:	
	case Qwatchpt:
		if(p->kp || p->privatemem)
			error(Eperm);
		break;

	default:
		print("procopen %#lux\n", QID(c->qid));
		error(Egreg);
	}
	nonone(p);

	/* Affix pid to qid */
	if(pid == 0)
		error(Eprocdied);
	c->qid.vers = pid;

	tc = devopen(c, omode, 0, 0, procgen);
	if(waserror()){
		cclose(tc);
		nexterror();
	}
	
	switch(QID(c->qid)){
	case Qwatchpt:
		if((omode0 & OTRUNC) != 0)
			clearwatchpt(p);
		break;
	}
	
	poperror();
	qunlock(&p->debug);
	poperror();

	return tc;
}

static int
procwstat(Chan *c, uchar *db, int n)
{
	Dir *d;
	Proc *p;

	if(c->qid.type&QTDIR)
		error(Eperm);

	switch(QID(c->qid)){
	case Qnotepg:
	case Qtrace:
		return devwstat(c, db, n);
	}
		
	d = smalloc(sizeof(Dir)+n);
	if(waserror()){
		free(d);
		nexterror();
	}
	n = convM2D(db, n, &d[0], (char*)&d[1]);
	if(n == 0)
		error(Eshortstat);

	p = proctab(SLOT(c->qid));
	eqlock(&p->debug);
	if(waserror()){
		qunlock(&p->debug);
		nexterror();
	}

	if(p->pid != PID(c->qid))
		error(Eprocdied);

	nonone(p);
	if(strcmp(up->user, p->user) != 0 && !iseve())
		error(Eperm);

	if(!emptystr(d->uid) && strcmp(d->uid, p->user) != 0){
		if(!iseve())
			error(Eperm);
		kstrdup(&p->user, d->uid);
	}
	/* p->procmode determines default mode for files in /proc */
	if(d->mode != ~0UL)
		p->procmode = d->mode&0777;

	qunlock(&p->debug);
	poperror();
	poperror();
	free(d);
	return n;
}

static void
procclose(Chan *c)
{
	Segio *sio;

	if((c->flag & COPEN) == 0)
		return;

	switch(QID(c->qid)){
	case Qtrace:
		lock(&tlock);
		if(topens > 0)
			topens--;
		if(topens == 0)
			proctrace = nil;
		unlock(&tlock);
		return;
	case Qmem:
		sio = c->aux;
		if(sio != nil){
			c->aux = nil;
			segio(sio, nil, nil, 0, 0, 0);
			free(sio);
		}
		return;
	}
}

static int
procargs(Proc *p, char *buf, int nbuf)
{
	int j, k, m;
	char *a;
	int n;

	a = p->args;
	if(p->setargs)
		return snprint(buf, nbuf, "%s [%s]", p->text, p->args);
	n = p->nargs;
	for(j = 0; j < nbuf - 1; j += m){
		if(n <= 0)
			break;
		if(j != 0)
			buf[j++] = ' ';
		m = snprint(buf+j, nbuf-j, "%q",  a);
		k = strlen(a) + 1;
		a += k;
		n -= k;
	}
	return j;
}

static int
eventsavailable(void *)
{
	return tproduced > tconsumed;
}

static int
prochaswaitq(void *x)
{
	Chan *c;
	Proc *p;

	c = (Chan *)x;
	p = proctab(SLOT(c->qid));
	return p->pid != PID(c->qid) || p->waitq != nil;
}

static void
int2flag(int flag, char *s)
{
	if(flag == 0){
		*s = '\0';
		return;
	}
	*s++ = '-';
	if(flag & MAFTER)
		*s++ = 'a';
	if(flag & MBEFORE)
		*s++ = 'b';
	if(flag & MCREATE)
		*s++ = 'c';
	if(flag & MCACHE)
		*s++ = 'C';
	*s = '\0';
}

static int
readns1(Chan *c, Proc *p, char *buf, int nbuf)
{
	Pgrp *pg;
	Mount *t, *cm;
	Mhead *f, *mh;
	ulong minid, bestmid;
	char flag[10], *srv;
	int i;

	pg = p->pgrp;
	if(pg == nil || p->dot == nil || p->pid != PID(c->qid))
		error(Eprocdied);

	bestmid = ~0;
	minid = c->nrock;
	if(minid == bestmid)
		return 0;

	rlock(&pg->ns);

	mh = nil;
	cm = nil;
	for(i = 0; i < MNTHASH; i++) {
		for(f = pg->mnthash[i]; f != nil; f = f->hash) {
			for(t = f->mount; t != nil; t = t->next) {
				if(t->mountid >= minid && t->mountid < bestmid) {
					bestmid = t->mountid;
					cm = t;
					mh = f;
				}
			}
		}
	}

	if(bestmid == ~0) {
		c->nrock = bestmid;
		i = snprint(buf, nbuf, "cd %q\n", p->dot->path->s);
	} else {
		c->nrock = bestmid+1;

		int2flag(cm->mflag, flag);
		if(strcmp(cm->to->path->s, "#M") == 0){
			srv = srvname(cm->to->mchan);
			i = snprint(buf, nbuf, (cm->spec && *cm->spec)?
				"mount %s %q %q %q\n": "mount %s %q %q\n", flag,
				srv? srv: cm->to->mchan->path->s, mh->from->path->s, cm->spec);
			free(srv);
		}else{
			i = snprint(buf, nbuf, "bind %s %q %q\n", flag,
				cm->to->path->s, mh->from->path->s);
		}
	}

	runlock(&pg->ns);

	return i;
}

int
procfdprint(Chan *c, int fd, char *s, int ns)
{
	return snprint(s, ns, "%3d %.2s %C %4ld (%.16llux %lud %.2ux) %5ld %8lld %s\n",
		fd,
		&"r w rw"[(c->mode&3)<<1],
		devtab[c->type]->dc, c->dev,
		c->qid.path, c->qid.vers, c->qid.type,
		c->iounit, c->offset, c->path->s);
}

static int
readfd1(Chan *c, Proc *p, char *buf, int nbuf)
{
	Fgrp *fg;
	int n, i;

	fg = p->fgrp;
	if(fg == nil || p->dot == nil || p->pid != PID(c->qid))
		return 0;

	if(c->nrock == 0){
		c->nrock = 1;
		return snprint(buf, nbuf, "%s\n", p->dot->path->s);
	}

	lock(fg);
	n = 0;
	for(;;){
		i = c->nrock-1;
		if(i < 0 || i > fg->maxfd)
			break;
		c->nrock++;
		if(fg->fd[i] != nil){
			n = procfdprint(fg->fd[i], i, buf, nbuf);
			break;
		}
	}
	unlock(fg);

	return n;
}

/*
 * setupwatchpts(Proc *p, Watchpt *wp, int nwp) is defined for all arches separately.
 * It tests whether wp is a valid set of watchpoints and errors out otherwise.
 * If and only if they are valid, it sets up all watchpoints (clearing any preexisting ones).
 * This is to make sure that failed writes to watchpt don't touch the existing watchpoints.
 */

static void
clearwatchpt(Proc *p)
{
	setupwatchpts(p, nil, 0);
	free(p->watchpt);
	p->watchpt = nil;
	p->nwatchpt = 0;
}

static int
lenwatchpt(Proc *pr)
{
	/* careful, not holding debug lock */
	return pr->nwatchpt * (10 + 4 * sizeof(uintptr));
}

static int
readwatchpt(Proc *pr, char *buf, int nbuf)
{
	char *p, *e;
	Watchpt *w;
	
	p = buf;
	e = buf + nbuf;
	/* careful, length has to match lenwatchpt() */
	for(w = pr->watchpt; w < pr->watchpt + pr->nwatchpt; w++)
		p = seprint(p, e, sizeof(uintptr) == 8 ? "%c%c%c %#.16p %#.16p\n" : "%c%c%c %#.8p %#.8p\n",
			(w->type & WATCHRD) != 0 ? 'r' : '-',
			(w->type & WATCHWR) != 0 ? 'w' : '-',
			(w->type & WATCHEX) != 0 ? 'x' : '-',
			(void *) w->addr, (void *) w->len);
	return p - buf;
}

static int
writewatchpt(Proc *pr, char *buf, int nbuf, uvlong offset)
{
	char *p, *q, *e;
	char line[256], *f[4];
	Watchpt *wp, *wq;
	int rc, nwp, nwp0;
	uvlong x;
	
	p = buf;
	e = buf + nbuf;
	if(offset != 0)
		nwp0 = pr->nwatchpt;
	else
		nwp0 = 0;
	nwp = 0;
	for(q = p; q < e; q++)
		nwp += *q == '\n';
	if(nwp > 65536) error(Egreg);
	wp = malloc((nwp0+nwp) * sizeof(Watchpt));
	if(wp == nil) error(Enomem);
	if(waserror()){
		free(wp);
		nexterror();
	}
	if(nwp0 > 0)
		memmove(wp, pr->watchpt, sizeof(Watchpt) * nwp0);
	for(wq = wp + nwp0; wq < wp + nwp0+nwp;){
		q = memchr(p, '\n', e - p);
		if(q == nil)
			break;
		if(q - p > sizeof(line) - 1)
			error("line too long");
		memmove(line, p, q - p);
		line[q - p] = 0;
		p = q + 1;
		
		rc = tokenize(line, f, nelem(f));
		if(rc == 0) continue;
		if(rc != 3)
			error("wrong number of fields");
		for(q = f[0]; *q != 0; q++)
			switch(*q){
			case 'r': if((wq->type & WATCHRD) != 0) goto tinval; wq->type |= WATCHRD; break;
			case 'w': if((wq->type & WATCHWR) != 0) goto tinval; wq->type |= WATCHWR; break;
			case 'x': if((wq->type & WATCHEX) != 0) goto tinval; wq->type |= WATCHEX; break;
			case '-': break;
			default: tinval: error("invalid type");
			}
		x = strtoull(f[1], &q, 0);
		if(f[1] == q || *q != 0 || x != (uintptr) x) error("invalid address");
		wq->addr = x;
		x = strtoull(f[2], &q, 0);
		if(f[2] == q || *q != 0 || x > (uintptr)-wq->addr) error("invalid length");
		wq->len = x;
		if(wq->addr + wq->len > USTKTOP) error("bad address");
		wq++;
	}
	nwp = wq - (wp + nwp0);
	if(nwp == 0 && nwp0 == pr->nwatchpt){
		poperror();
		free(wp);
		return p - buf;
	}
	setupwatchpts(pr, wp, nwp0 + nwp);
	poperror();
	free(pr->watchpt);
	pr->watchpt = wp;
	pr->nwatchpt = nwp0 + nwp;
	return p - buf;
}

/*
 * userspace can't pass negative file offset for a
 * 64 bit kernel address, so we use 63 bit and sign
 * extend to 64 bit.
 */
static uintptr
off2addr(vlong off)
{
	off <<= 1;
	off >>= 1;
	return off;
}

static long
procread(Chan *c, void *va, long n, vlong off)
{
	char statbuf[1024], *sps;
	ulong offset;
	int i, j, rsize;
	uchar *rptr;
	uintptr addr;
	Segment *s;
	Ureg kur;
	Waitq *wq;
	Proc *p;
	
	offset = off;
	if(c->qid.type & QTDIR)
		return devdirread(c, va, n, 0, 0, procgen);

	if(QID(c->qid) == Qtrace){
		int navail, ne;

		if(!eventsavailable(nil))
			return 0;

		rptr = (uchar*)va;
		navail = tproduced - tconsumed;
		if(navail > n / sizeof(Traceevent))
			navail = n / sizeof(Traceevent);
		while(navail > 0) {
			ne = ((tconsumed & Emask) + navail > Nevents)? 
					Nevents - (tconsumed & Emask): navail;
			memmove(rptr, &tevents[tconsumed & Emask], 
					ne * sizeof(Traceevent));

			tconsumed += ne;
			rptr += ne * sizeof(Traceevent);
			navail -= ne;
		}
		return rptr - (uchar*)va;
	}

	p = proctab(SLOT(c->qid));
	if(p->pid != PID(c->qid))
		error(Eprocdied);

	switch(QID(c->qid)){
	case Qmem:
		addr = off2addr(off);
		if(addr < KZERO)
			return procctlmemio(c, p, addr, va, n, 1);

		if(!iseve() || poolisoverlap(secrmem, (uchar*)addr, n))
			error(Eperm);

		/* validate kernel addresses */
		if(addr < (uintptr)end) {
			if(addr+n > (uintptr)end)
				n = (uintptr)end - addr;
			memmove(va, (char*)addr, n);
			return n;
		}
		for(i=0; i<nelem(conf.mem); i++){
			Confmem *cm = &conf.mem[i];
			/* klimit-1 because klimit might be zero! */
			if(cm->kbase <= addr && addr <= cm->klimit-1){
				if(addr+n >= cm->klimit-1)
					n = cm->klimit - addr;
				memmove(va, (char*)addr, n);
				return n;
			}
		}
		error(Ebadarg);

	case Qctl:
		return readnum(offset, va, n, p->pid, NUMSIZE);

	case Qnoteid:
		return readnum(offset, va, n, p->noteid, NUMSIZE);

	case Qppid:
		return readnum(offset, va, n, p->parentpid, NUMSIZE);

	case Qprofile:
		s = p->seg[TSEG];
		if(s == nil || s->profile == nil)
			error("profile is off");
		i = (s->top-s->base)>>LRESPROF;
		i *= sizeof(s->profile[0]);
		if(i < 0 || offset >= i)
			return 0;
		if(offset+n > i)
			n = i - offset;
		memmove(va, ((char*)s->profile)+offset, n);
		return n;

	case Qproc:
		rptr = (uchar*)p;
		rsize = sizeof(Proc);
		goto regread;

	case Qregs:
		rptr = (uchar*)p->dbgreg;
		rsize = sizeof(Ureg);
		goto regread;

	case Qkregs:
		memset(&kur, 0, sizeof(Ureg));
		setkernur(&kur, p);
		rptr = (uchar*)&kur;
		rsize = sizeof(Ureg);
		goto regread;

	case Qfpregs:
		if(p->fpstate != FPinactive)
			error(Enoreg);
		rptr = (uchar*)p->fpsave;
		rsize = sizeof(FPsave);
	regread:
		if(rptr == nil)
			error(Enoreg);
		if(offset >= rsize)
			return 0;
		if(offset+n > rsize)
			n = rsize - offset;
		memmove(va, rptr+offset, n);
		return n;

	case Qstatus:
		sps = p->psstate;
		if(sps == nil)
			sps = statename[p->state];
		j = snprint(statbuf, sizeof(statbuf),
			"%-27s %-27s %-11s "
			"%11lud %11lud %11lud "
			"%11lud %11lud %11lud "
			"%11lud %11lud %11lud\n",
			p->text, p->user, sps,
			tk2ms(p->time[TUser]),
			tk2ms(p->time[TSys]),
			tk2ms(MACHP(0)->ticks - p->time[TReal]),
			tk2ms(p->time[TCUser]),
			tk2ms(p->time[TCSys]),
			tk2ms(p->time[TCReal]),
			(ulong)(procpagecount(p)*BY2PG/1024),
			p->basepri, p->priority);
	statbufread:
		if(offset >= j)
			return 0;
		if(offset+n > j)
			n = j - offset;
		memmove(va, statbuf+offset, n);
		return n;

	case Qsegment:
		j = 0;
		for(i = 0; i < NSEG; i++) {
			s = p->seg[i];
			if(s == nil)
				continue;
			j += snprint(statbuf+j, sizeof(statbuf)-j,
				"%-6s %c%c %8p %8p %4ld\n",
				sname[s->type&SG_TYPE],
				s->type&SG_FAULT ? 'F' : (s->type&SG_RONLY ? 'R' : ' '),
				s->profile ? 'P' : ' ',
				s->base, s->top, s->ref);
		}
		goto statbufread;

	case Qwait:
		if(!canqlock(&p->qwaitr))
			error(Einuse);

		if(waserror()) {
			qunlock(&p->qwaitr);
			nexterror();
		}

		lock(&p->exl);
		while(p->waitq == nil && p->pid == PID(c->qid)) {
			if(up == p && p->nchild == 0) {
				unlock(&p->exl);
				error(Enochild);
			}
			unlock(&p->exl);
			sleep(&p->waitr, prochaswaitq, c);
			lock(&p->exl);
		}
		if(p->pid != PID(c->qid)){
			unlock(&p->exl);
			error(Eprocdied);
		}
		wq = p->waitq;
		p->waitq = wq->next;
		p->nwait--;
		unlock(&p->exl);

		qunlock(&p->qwaitr);
		poperror();

		j = snprint(statbuf, sizeof(statbuf), "%d %lud %lud %lud %q",
			wq->w.pid,
			wq->w.time[TUser], wq->w.time[TSys], wq->w.time[TReal],
			wq->w.msg);
		free(wq);
		offset = 0;
		goto statbufread;
	}

	eqlock(&p->debug);
	if(waserror()){
		qunlock(&p->debug);
		nexterror();
	}
	if(p->pid != PID(c->qid))
		error(Eprocdied);

	switch(QID(c->qid)){
	case Qns:
	case Qfd:
		if(offset == 0 || offset != c->mrock)
			c->nrock = c->mrock = 0;
		do {
			if(QID(c->qid) == Qns)
				j = readns1(c, p, statbuf, sizeof(statbuf));
			else
				j = readfd1(c, p, statbuf, sizeof(statbuf));
			if(j == 0)
				break;
			c->mrock += j;
		} while(c->mrock <= offset);
		i = c->mrock - offset;
		qunlock(&p->debug);
		poperror();
		if(i <= 0 || i > j)
			return 0;
		if(i < n)
			n = i;
		offset = j - i;
		goto statbufread;
	
	case Qargs:
		j = procargs(p, statbuf, sizeof(statbuf));
		qunlock(&p->debug);
		poperror();
		goto statbufread;

	case Qwatchpt:
		j = readwatchpt(p, statbuf, sizeof(statbuf));
		qunlock(&p->debug);
		poperror();
		goto statbufread;

	case Qsyscall:
		if(p->syscalltrace != nil)
			n = readstr(offset, va, n, p->syscalltrace);
		else
			n = 0;
		break;

	case Qnote:
		if(n < 1)	/* must accept at least the '\0' */
			error(Etoosmall);
		if(p->nnote == 0)
			n = 0;
		else {
			i = strlen(p->note[0].msg) + 1;
			if(i < n)
				n = i;
			memmove(va, p->note[0].msg, n-1);
			((char*)va)[n-1] = '\0';
			if(--p->nnote == 0)
				p->notepending = 0;
			memmove(p->note, p->note+1, p->nnote*sizeof(Note));
		}
		break;

	default:
		print("unknown qid in procwread\n");
		error(Egreg);
	}

	qunlock(&p->debug);
	poperror();
	return n;
}

static long
procwrite(Chan *c, void *va, long n, vlong off)
{
	char buf[ERRMAX];
	ulong offset;
	Proc *p;

	offset = off;
	if(c->qid.type & QTDIR)
		error(Eisdir);

	/* use the remembered noteid in the channel qid */
	if(QID(c->qid) == Qnotepg) {
		if(n >= sizeof(buf))
			error(Etoobig);
		memmove(buf, va, n);
		buf[n] = 0;
		postnotepg(NOTEID(c->qid), buf, NUser);
		return n;
	}

	p = proctab(SLOT(c->qid));
	eqlock(&p->debug);
	if(waserror()){
		qunlock(&p->debug);
		nexterror();
	}
	if(p->pid != PID(c->qid))
		error(Eprocdied);

	switch(QID(c->qid)){
	case Qargs:
		if(offset != 0 || n >= sizeof(buf))
			error(Etoobig);
		memmove(buf, va, n);
		buf[n] = 0;
		kstrdup(&p->args, buf);
		p->nargs = 0;
		p->setargs = 1;
		break;

	case Qmem:
		if(p->state != Stopped)
			error(Ebadctl);
		n = procctlmemio(c, p, off2addr(off), va, n, 0);
		break;

	case Qregs:
		if(offset >= sizeof(Ureg))
			n = 0;
		else if(offset+n > sizeof(Ureg))
			n = sizeof(Ureg) - offset;
		if(p->dbgreg == nil)
			error(Enoreg);
		setregisters(p->dbgreg, (char*)(p->dbgreg)+offset, va, n);
		break;

	case Qfpregs:
		if(offset >= sizeof(FPsave))
			n = 0;
		else if(offset+n > sizeof(FPsave))
			n = sizeof(FPsave) - offset;
		if(p->fpstate != FPinactive || p->fpsave == nil)
			error(Enoreg);
		memmove((uchar*)p->fpsave+offset, va, n);
		break;

	case Qctl:
		procctlreq(p, va, n);
		break;

	case Qnote:
		if(n >= sizeof(buf))
			error(Etoobig);
		memmove(buf, va, n);
		buf[n] = 0;
		if(!postnote(p, 0, buf, NUser))
			error("note not posted");
		break;

	case Qnoteid:
		if(offset != 0 || n >= sizeof(buf))
			error(Etoobig);
		memmove(buf, va, n);
		buf[n] = 0;
		changenoteid(p, atoi(buf));
		break;

	case Qwatchpt:
		writewatchpt(p, va, n, off);
		break;

	default:
		print("unknown qid in procwrite\n");
		error(Egreg);
	}
	poperror();
	qunlock(&p->debug);
	return n;
}

Dev procdevtab = {
	'p',
	"proc",

	devreset,
	procinit,
	devshutdown,
	procattach,
	procwalk,
	procstat,
	procopen,
	devcreate,
	procclose,
	procread,
	devbread,
	procwrite,
	devbwrite,
	devremove,
	procwstat,
};

static Chan*
proctext(Chan *c, Proc *p)
{
	Chan *tc;
	Image *i;
	Segment *s;

	eqlock(&p->seglock);
	if(waserror()){
		qunlock(&p->seglock);
		nexterror();
	}
	if(p->state == Dead || p->pid != PID(c->qid))
		error(Eprocdied);
	if((s = p->seg[TSEG]) == nil)
		error(Enonexist);
	if((i = s->image) == nil)
		error(Enonexist);
	lock(i);
	tc = i->c;
	if(i->notext || tc == nil || (tc->flag&COPEN) == 0 || tc->mode != OREAD){
		unlock(i);
		error(Enonexist);
	}
	incref(tc);
	unlock(i);
	qunlock(&p->seglock);
	poperror();

	return tc;
}

static void
procstopwait(Proc *p, int ctl)
{
	char *state;
	int pid;

	if(p->pdbg != nil)
		error(Einuse);
	if(procstopped(p) || p->state == Broken)
		return;
	pid = p->pid;
	if(pid == 0)
		error(Eprocdied);
	if(ctl != 0)
		p->procctl = ctl;
	if(p == up)
		return;
	p->pdbg = up;
	qunlock(&p->debug);
	state = up->psstate;
	up->psstate = "Stopwait";
	if(waserror()) {
		up->psstate = state;
		qlock(&p->debug);
		if(p->pdbg == up)
			p->pdbg = nil;
		nexterror();
	}
	sleep(&up->sleep, procstopped, p);
	poperror();
	up->psstate = state;
	qlock(&p->debug);
	if(p->pid != pid)
		error(Eprocdied);
}

static void
procctlclosefiles(Proc *p, int all, int fd)
{
	Fgrp *f;
	Chan *c;

	if(fd < 0)
		error(Ebadfd);
	f = p->fgrp;
	if(f == nil)
		error(Eprocdied);

	incref(f);
	lock(f);
	while(fd <= f->maxfd){
		c = f->fd[fd];
		if(c != nil){
			f->fd[fd] = nil;
			unlock(f);
			qunlock(&p->debug);
			cclose(c);
			qlock(&p->debug);
			lock(f);
		}
		if(!all)
			break;
		fd++;
	}
	unlock(f);
	closefgrp(f);
}

static char *
parsetime(vlong *rt, char *s)
{
	uvlong ticks;
	ulong l;
	char *e, *p;
	static int p10[] = {100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1};

	if (s == nil)
		return("missing value");
	ticks=strtoul(s, &e, 10);
	if (*e == '.'){
		p = e+1;
		l = strtoul(p, &e, 10);
		if(e-p > nelem(p10))
			return "too many digits after decimal point";
		if(e-p == 0)
			return "ill-formed number";
		l *= p10[e-p-1];
	}else
		l = 0;
	if (*e == '\0' || strcmp(e, "s") == 0){
		ticks = 1000000000 * ticks + l;
	}else if (strcmp(e, "ms") == 0){
		ticks = 1000000 * ticks + l/1000;
	}else if (strcmp(e, "µs") == 0 || strcmp(e, "us") == 0){
		ticks = 1000 * ticks + l/1000000;
	}else if (strcmp(e, "ns") != 0)
		return "unrecognized unit";
	*rt = ticks;
	return nil;
}

static void
procctlreq(Proc *p, char *va, int n)
{
	Segment *s;
	uintptr npc;
	int pri;
	Cmdbuf *cb;
	Cmdtab *ct;
	vlong time;
	char *e;
	void (*pt)(Proc*, int, vlong);

	cb = parsecmd(va, n);
	if(waserror()){
		free(cb);
		nexterror();
	}

	ct = lookupcmd(cb, proccmd, nelem(proccmd));

	switch(ct->index){
	case CMclose:
		procctlclosefiles(p, 0, atoi(cb->f[1]));
		break;
	case CMclosefiles:
		procctlclosefiles(p, 1, 0);
		break;
	case CMhang:
		p->hang = 1;
		break;
	case CMkill:
		switch(p->state) {
		case Broken:
			unbreak(p);
			break;
		case Stopped:
			p->procctl = Proc_exitme;
			postnote(p, 0, "sys: killed", NExit);
			ready(p);
			break;
		default:
			p->procctl = Proc_exitme;
			postnote(p, 0, "sys: killed", NExit);
		}
		break;
	case CMnohang:
		p->hang = 0;
		break;
	case CMnoswap:
		p->noswap = 1;
		break;
	case CMpri:
		pri = atoi(cb->f[1]);
		if(pri > PriNormal && !iseve())
			error(Eperm);
		procpriority(p, pri, 0);
		break;
	case CMfixedpri:
		pri = atoi(cb->f[1]);
		if(pri > PriNormal && !iseve())
			error(Eperm);
		procpriority(p, pri, 1);
		break;
	case CMprivate:
		p->privatemem = 1;
		break;
	case CMprofile:
		s = p->seg[TSEG];
		if(s == nil || (s->type&SG_TYPE) != SG_TEXT)	/* won't expand */
			error(Egreg);
		eqlock(s);
		npc = (s->top-s->base)>>LRESPROF;
		if(s->profile == nil){
			s->profile = malloc(npc*sizeof(*s->profile));
			if(s->profile == nil){
				qunlock(s);
				error(Enomem);
			}
		} else {
			memset(s->profile, 0, npc*sizeof(*s->profile));
		}
		qunlock(s);
		break;
	case CMstart:
		if(p->state != Stopped)
			error(Ebadctl);
		ready(p);
		break;
	case CMstartstop:
		if(p->state != Stopped)
			error(Ebadctl);
		p->procctl = Proc_traceme;
		ready(p);
		procstopwait(p, Proc_traceme);
		break;
	case CMstartsyscall:
		if(p->state != Stopped)
			error(Ebadctl);
		p->procctl = Proc_tracesyscall;
		ready(p);
		procstopwait(p, Proc_tracesyscall);
		break;
	case CMstop:
		procstopwait(p, Proc_stopme);
		break;
	case CMwaitstop:
		procstopwait(p, 0);
		break;
	case CMwired:
		procwired(p, atoi(cb->f[1]));
		break;
	case CMtrace:
		switch(cb->nf){
		case 1:
			p->trace ^= 1;
			break;
		case 2:
			p->trace = (atoi(cb->f[1]) != 0);
			break;
		default:
			error("args");
		}
		break;
	case CMinterrupt:
		postnote(p, 0, nil, NUser);
		break;
	case CMnointerrupt:
		if(p->nnote == 0)
			p->notepending = 0;
		else
			error("notes pending");
		break;
	/* real time */
	case CMperiod:
		if(p->edf == nil)
			edfinit(p);
		if(e=parsetime(&time, cb->f[1]))	/* time in ns */
			error(e);
		edfstop(p);
		p->edf->T = time/1000;	/* Edf times are in µs */
		break;
	case CMdeadline:
		if(p->edf == nil)
			edfinit(p);
		if(e=parsetime(&time, cb->f[1]))
			error(e);
		edfstop(p);
		p->edf->D = time/1000;
		break;
	case CMcost:
		if(p->edf == nil)
			edfinit(p);
		if(e=parsetime(&time, cb->f[1]))
			error(e);
		edfstop(p);
		p->edf->C = time/1000;
		break;
	case CMsporadic:
		if(p->edf == nil)
			edfinit(p);
		p->edf->flags |= Sporadic;
		break;
	case CMdeadlinenotes:
		if(p->edf == nil)
			edfinit(p);
		p->edf->flags |= Sendnotes;
		break;
	case CMadmit:
		if(p->edf == nil)
			error("edf params");
		if(e = edfadmit(p))
			error(e);
		break;
	case CMextra:
		if(p->edf == nil)
			edfinit(p);
		p->edf->flags |= Extratime;
		break;
	case CMexpel:
		if(p->edf != nil)
			edfstop(p);
		break;
	case CMevent:
		pt = proctrace;
		if(up->trace && pt != nil)
			pt(up, SUser, 0);
		break;
	}

	poperror();
	free(cb);
}

static int
procstopped(void *a)
{
	return ((Proc*)a)->state == Stopped;
}

static long
procctlmemio(Chan *c, Proc *p, uintptr offset, void *a, long n, int read)
{
	Segio *sio;
	Segment *s;
	int i;

	s = seg(p, offset, 0);
	if(s == nil)
		error(Ebadarg);
	eqlock(&p->seglock);
	if(waserror()) {
		qunlock(&p->seglock);
		nexterror();
	}
	if(p->state == Dead || p->pid != PID(c->qid))
		error(Eprocdied);

	for(i = 0; i < NSEG; i++) {
		if(p->seg[i] == s)
			break;
	}
	if(i == NSEG)
		error(Egreg);	/* segment gone */

	eqlock(s);
	if(waserror()){
		qunlock(s);
		nexterror();
	}
	if(!read && (s->type&SG_TYPE) == SG_TEXT) {
		s = txt2data(s);
		p->seg[i] = s;
	}
	offset -= s->base;
	incref(s);		/* for us while we copy */
	qunlock(s);
	poperror();

	sio = c->aux;
	if(sio == nil){
		sio = smalloc(sizeof(Segio));
		c->aux = sio;
	}

	qunlock(&p->seglock);
	poperror();

	if(waserror()) {
		putseg(s);
		nexterror();
	}
	n = segio(sio, s, a, n, offset, read);
	putseg(s);
	poperror();

	if(!read)
		p->newtlb = 1;

	return n;
}
