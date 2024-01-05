#include	<u.h>
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"edf.h"
#include	<trace.h>
#include	"tos.h"
#include	"ureg.h"

int	schedgain = 30;	/* units in seconds */
int	nrdy;

static void updatecpu(Proc*);
static int reprioritize(Proc*);

ulong	delayedscheds;	/* statistics */
ulong	skipscheds;
ulong	preempts;
ulong	load;

static struct Procalloc
{
	Lock;
	Proc	**tab;
	Proc	*free;
	int	nextindex;
} procalloc;

enum
{
	Q=10,
	DQ=4,
	Scaling=2,
};

Schedq	runq[Nrq];
ulong	runvec;

char *statename[] =
{	/* BUG: generate automatically */
	"Dead",
	"Moribund",
	"New",
	"Ready",
	"Scheding",
	"Running",
	"Queueing",
	"QueueingR",
	"QueueingW",
	"Wakeme",
	"Broken",
	"Stopped",
	"Rendez",
	"Waitrelease",
};

static void rebalance(void);
static void pidinit(void);
static void pidfree(Proc*);

/*
 * Always splhi()'ed.
 */
void
schedinit(void)		/* never returns */
{
	Edf *e;

	setlabel(&m->sched);
	if(up != nil) {
		if((e = up->edf) != nil && (e->flags & Admitted))
			edfrecord(up);
		m->proc = nil;
		switch(up->state) {
		default:
			updatecpu(up);
			break;
		case Running:
			up->state = Scheding;
			ready(up);
			break;
		case Moribund:
			mmurelease(up);
			lock(&procalloc);
			up->state = Dead;
			up->mach = nil;
			up->qnext = procalloc.free;
			procalloc.free = up;
			/* proc is free now, make sure unlock() wont touch it */
			up = procalloc.Lock.p = nil;
			unlock(&procalloc);
			goto out;
		}
		coherence();
		up->mach = nil;
		up = nil;
	}
out:
	sched();
	panic("schedinit");
}

int
kenter(Ureg *ureg)
{
	int user;

	user = userureg(ureg);
	if(user){
		up->dbgreg = ureg;
		cycles(&up->kentry);
	}
	return user;
}

void
kexit(Ureg*)
{
	uvlong t;
	Tos *tos;

	cycles(&t);

	/* precise time accounting, kernel exit */
	tos = (Tos*)(USTKTOP-sizeof(Tos));
	tos->kcycles += t - up->kentry;
	tos->pcycles = t + up->pcycles;
	tos->pid = up->pid;
}

static void
procswitch(void)
{
	uvlong t;

	/* statistics */
	m->cs++;

	cycles(&t);
	up->kentry -= t;
	up->pcycles += t;

	procsave(up);

	if(!setlabel(&up->sched))
		gotolabel(&m->sched);

	procrestore(up);

	cycles(&t);
	up->kentry += t;
	up->pcycles -= t;
}

/*
 *  If changing this routine, look also at sleep().  It
 *  contains a copy of the guts of sched().
 */
void
sched(void)
{
	if(m->ilockdepth)
		panic("cpu%d: ilockdepth %d, last lock %#p at %#p",
			m->machno,
			m->ilockdepth,
			up != nil ? up->lastilock: nil,
			(up != nil && up->lastilock != nil) ? up->lastilock->pc: 0);
	if(up != nil) {
		/*
		 * Delay the sched until the process gives up the locks
		 * it is holding.  This avoids dumb lock loops.
		 * Don't delay if the process is Moribund.
		 * It called sched to die.
		 * But do sched eventually.  This avoids a missing unlock
		 * from hanging the entire kernel. 
		 * But don't reschedule procs holding palloc or procalloc.
		 * Those are far too important to be holding while asleep.
		 *
		 * This test is not exact.  There can still be a few instructions
		 * in the middle of taslock when a process holds a lock
		 * but Lock.p has not yet been initialized.
		 */
		if(up->nlocks)
		if(up->state != Moribund)
		if(up->delaysched < 20
		|| palloc.Lock.p == up
		|| fscache.Lock.p == up
		|| procalloc.Lock.p == up){
			up->delaysched++;
 			delayedscheds++;
			return;
		}
		up->delaysched = 0;
		splhi();
 		procswitch();
		spllo();
		return;
	}
	up = runproc();
	if(up->edf == nil)
		up->priority = reprioritize(up);
	if(up != m->readied)
		m->schedticks = m->ticks + HZ/10;
	m->readied = nil;
	m->proc = up;
	up->mach = up->mp = MACHP(m->machno);
	up->state = Running;
	mmuswitch(up);
	gotolabel(&up->sched);
}

int
anyready(void)
{
	return runvec;
}

int
anyhigher(void)
{
	return runvec & ~((1<<(up->priority+1))-1);
}

/*
 *  here once per clock tick to see if we should resched
 */
void
hzsched(void)
{
	/* once a second, rebalance will reprioritize ready procs */
	if(m->machno == 0)
		rebalance();

	/* unless preempted, get to run for at least 100ms */
	if(anyhigher()
	|| (!up->fixedpri && (long)(m->ticks - m->schedticks) > 0 && anyready())){
		m->readied = nil;	/* avoid cooperative scheduling */
		up->delaysched++;
	}
}

/*
 *  here at the end of interrupts to see if we should preempt the
 *  current process.
 */
void
preempted(int clockintr)
{
	if(up == nil || up->state != Running || active.exiting)
		return;
	if(!clockintr){
		if(up->preempted || !anyhigher())
			return;
		m->readied = nil;	/* avoid cooperative scheduling */
		up->preempted = 1;
		sched();
		up->preempted = 0;
	} else if(up->delaysched)
		sched();		/* quantum ended or we held a lock */
}

/*
 * Update the cpu time average for this particular process,
 * which is about to change from up -> not up or vice versa.
 * p->lastupdate is the last time an updatecpu happened.
 *
 * The cpu time average is a decaying average that lasts
 * about D clock ticks.  D is chosen to be approximately
 * the cpu time of a cpu-intensive "quick job".  A job has to run
 * for approximately D clock ticks before we home in on its 
 * actual cpu usage.  Thus if you manage to get in and get out
 * quickly, you won't be penalized during your burst.  Once you
 * start using your share of the cpu for more than about D
 * clock ticks though, your p->cpu hits 1000 (1.0) and you end up 
 * below all the other quick jobs.  Interactive tasks, because
 * they basically always use less than their fair share of cpu,
 * will be rewarded.
 *
 * If the process has not been running, then we want to
 * apply the filter
 *
 *	cpu = cpu * (D-1)/D
 *
 * n times, yielding 
 * 
 *	cpu = cpu * ((D-1)/D)^n
 *
 * but D is big enough that this is approximately 
 *
 * 	cpu = cpu * (D-n)/D
 *
 * so we use that instead.
 * 
 * If the process has been running, we apply the filter to
 * 1 - cpu, yielding a similar equation.  Note that cpu is 
 * stored in fixed point (* 1000).
 *
 * Updatecpu must be called before changing up, in order
 * to maintain accurate cpu usage statistics.  It can be called
 * at any time to bring the stats for a given proc up-to-date.
 */
static void
updatecpu(Proc *p)
{
	ulong t, ocpu, n, D;

	if(p->edf != nil)
		return;

	t = MACHP(0)->ticks*Scaling + Scaling/2;
	n = t - p->lastupdate;
	if(n == 0)
		return;
	p->lastupdate = t;

	D = schedgain*HZ*Scaling;
	if(n > D)
		n = D;

	ocpu = p->cpu;
	if(p != up)
		p->cpu = (ocpu*(D-n))/D;
	else{
		t = 1000 - ocpu;
		t = (t*(D-n))/D;
		p->cpu = 1000 - t;
	}
//iprint("pid %lud %s for %lud cpu %lud -> %lud\n", p->pid,p==up?"active":"inactive",n, ocpu,p->cpu);
}

/*
 * On average, p has used p->cpu of a cpu recently.
 * Its fair share is conf.nmach/m->load of a cpu.  If it has been getting
 * too much, penalize it.  If it has been getting not enough, reward it.
 * I don't think you can get much more than your fair share that 
 * often, so most of the queues are for using less.  Having a priority
 * of 3 means you're just right.  Having a higher priority (up to p->basepri) 
 * means you're not using as much as you could.
 */
static int
reprioritize(Proc *p)
{
	int fairshare, n, load, ratio;

	updatecpu(p);
	load = MACHP(0)->load;
	if(load == 0)
		return p->basepri;

	/*
	 * fairshare = 1.000 * conf.nmach * 1.000/load,
	 * except the decimal point is moved three places
	 * on both load and fairshare.
	 */
	fairshare = (conf.nmach*1000*1000)/load;
	n = p->cpu;
	if(n == 0)
		n = 1;
	ratio = (fairshare+n/2) / n;
	if(ratio > p->basepri)
		ratio = p->basepri;
	if(ratio < 0)
		panic("reprioritize");
//iprint("pid %lud cpu %lud load %d fair %d pri %d\n", p->pid, p->cpu, load, fairshare, ratio);
	return ratio;
}

/*
 * add a process to a scheduling queue
 */
static int
queueproc(Schedq *rq, Proc *p)
{
	int pri = rq - runq;

	lock(runq);
	switch(p->state){
	case New:
	case Queueing:
	case QueueingR:
	case QueueingW:
	case Wakeme:
	case Broken:
	case Stopped:
	case Rendezvous:
		if(p != up)
			break;
		/* wet floor */
	case Dead:
	case Moribund:
	case Ready:
	case Running:
	case Waitrelease:
		unlock(runq);
		return -1;
	}
	p->state = Ready;
	p->priority = pri;
	if(pri == PriEdf){
		Proc *pp, *l;

		/* insert in queue in earliest deadline order */
		l = nil;
		for(pp = rq->head; pp != nil; pp = pp->rnext){
			if(pp->edf->d > p->edf->d)
				break;
			l = pp;
		}
		p->rnext = pp;
		if(l == nil)
			rq->head = p;
		else
			l->rnext = p;
		if(pp == nil)
			rq->tail = p;
	} else {
		p->rnext = nil;
		if(rq->tail != nil)
			rq->tail->rnext = p;
		else
			rq->head = p;
		rq->tail = p;
	}
	rq->n++;
	nrdy++;
	runvec |= 1<<pri;
	unlock(runq);
	return 0;
}

/*
 *  ready(p) picks a new priority for a process and sticks it in the
 *  runq for that priority.
 */
void
ready(Proc *p)
{
	int s, pri;

	s = splhi();
	switch(edfready(p)){
	default:
		splx(s);
		return;
	case 0:
		pri = reprioritize(p);
		break;
	case 1:
		pri = PriEdf;
		break;
	}
	if(queueproc(&runq[pri], p) < 0){
		iprint("ready %s %lud %s pc %p\n",
			p->text, p->pid, statename[p->state], getcallerpc(&p));
	} else {
		void (*pt)(Proc*, int, vlong);
		pt = proctrace;
		if(pt != nil)
			pt(p, SReady, 0);
	}
	splx(s);
}


/*
 *  try to remove a process from a scheduling queue (called splhi)
 */
Proc*
dequeueproc(Schedq *rq, Proc *tp)
{
	Proc *l, *p;

	if(!canlock(runq))
		return nil;

	/*
	 *  the queue may have changed before we locked runq,
	 *  refind the target process.
	 */
	l = nil;
	for(p = rq->head; p != nil; p = p->rnext){
		if(p == tp)
			break;
		l = p;
	}

	/*
	 *  p->mach==0 only when process state is saved
	 */
	if(p == nil || p->mach != nil){
		unlock(runq);
		return nil;
	}
	if(p->rnext == nil)
		rq->tail = l;
	if(l != nil)
		l->rnext = p->rnext;
	else
		rq->head = p->rnext;
	if(rq->head == nil)
		runvec &= ~(1<<(rq-runq));
	rq->n--;
	nrdy--;
	if(p->state != Ready){
		iprint("dequeueproc %s %lud %s pc %p\n",
			p->text, p->pid, statename[p->state], getcallerpc(&rq));
		p = nil;
	}
	unlock(runq);
	return p;
}

/*
 *  yield the processor and drop our priority
 */
void
yield(void)
{
	if(anyready()){
		/* pretend we just used 1/2 tick */
		up->lastupdate -= Scaling/2;  
		sched();
	}
}

/*
 *  recalculate priorities once a second.  We need to do this
 *  since priorities will otherwise only be recalculated when
 *  the running process blocks.
 */
ulong balancetime;

static void
rebalance(void)
{
	int pri, npri;
	Schedq *rq;
	Proc *p;
	ulong t;

	t = m->ticks;
	if(t - balancetime < HZ)
		return;
	balancetime = t;

	assert(!islo());

	for(pri=0, rq=runq; pri<Npriq; pri++, rq++){
another:
		p = rq->head;
		if(p == nil)
			continue;
		if(pri == p->basepri)
			continue;
		npri = reprioritize(p);
		if(npri != pri){
			p = dequeueproc(rq, p);
			if(p != nil){
				p->state = Scheding;
				if(queueproc(&runq[npri], p) < 0)
					iprint("rebalance: queueproc %lud %s %s\n",
						p->pid, p->text, statename[p->state]);
				goto another;
			}
		}
	}
}
	

/*
 *  pick a process to run
 */
Proc*
runproc(void)
{
	Schedq *rq;
	Proc *p;
	ulong start, now;
	int i;
	void (*pt)(Proc*, int, vlong);

	start = perfticks();

	/* cooperative scheduling until the clock ticks */
	if((p = m->readied) != nil && p->mach == nil && p->state == Ready
	&& (p->wired == nil || p->wired == MACHP(m->machno))
	&& runq[Nrq-1].head == nil && runq[Nrq-2].head == nil){
		skipscheds++;
		rq = &runq[p->priority];
		goto found;
	}

	preempts++;

loop:
	/*
	 *  find a process that last ran on this processor (affinity),
	 *  or one that can be moved to this processor.
	 */
	spllo();
	for(i = 0;; i++){
		/*
		 *  find the highest priority target process that this
		 *  processor can run given affinity constraints.
		 *
		 */
		for(rq = &runq[Nrq-1]; rq >= runq; rq--){
			for(p = rq->head; p != nil; p = p->rnext){
				if(p->mp == nil || p->mp == MACHP(m->machno)
				|| (p->wired == nil && i > 0))
					goto found;
			}
		}

		/* waste time or halt the CPU */
		idlehands();

		/* remember how much time we're here */
		now = perfticks();
		m->perf.inidle += now-start;
		start = now;
	}

found:
	splhi();
	p = dequeueproc(rq, p);
	if(p == nil)
		goto loop;
	if(edflock(p)){
		edfrun(p, rq == &runq[PriEdf]);	/* start deadline timer and do admin */
		edfunlock();
	}
	pt = proctrace;
	if(pt != nil)
		pt(p, SRun, 0);
	return p;
}

int
canpage(Proc *p)
{
	int ok = 0;

	splhi();
	lock(runq);
	/* Only reliable way to see if we are Running */
	if(p->mach == nil) {
		p->newtlb = 1;
		ok = 1;
	}
	unlock(runq);
	spllo();

	return ok;
}

Proc*
newproc(void)
{
	char *b;
	Proc *p;

	lock(&procalloc);
	p = procalloc.free;
	if(p == nil){
		if(procalloc.nextindex >= conf.nproc){
			unlock(&procalloc);
			return nil;
		}
		b = malloc(KSTACK+sizeof(Proc));
		if(b == nil){
			unlock(&procalloc);
			return nil;
		}
		p = (Proc*)(b + KSTACK);
		p->index = procalloc.nextindex++;
		procalloc.tab[p->index] = p;
	}
	assert(p->state == Dead);
	procalloc.free = p->qnext;
	p->qnext = nil;
	unlock(&procalloc);

	p->psstate = nil;
	p->state = New;
	p->fpstate = FPinit;
#ifdef KFPSTATE
	p->kfpstate = FPinit;
#endif
	p->procctl = 0;
	p->ureg = nil;
	p->dbgreg = nil;
	p->nerrlab = 0;
	p->errstr = p->errbuf0;
	p->syserrstr = p->errbuf1;
	p->errbuf0[0] = '\0';
	p->errbuf1[0] = '\0';
	p->nlocks = 0;
	p->delaysched = 0;
	p->trace = 0;

	/* sched params */
	p->mp = nil;
	p->wired = nil;
	procpriority(p, PriNormal, 0);
	p->cpu = 0;
	p->lastupdate = MACHP(0)->ticks*Scaling;
	p->edf = nil;

	return p;
}

/*
 * wire this proc to a machine
 */
void
procwired(Proc *p, int bm)
{
	Proc *pp;
	int i;
	char nwired[MAXMACH];
	Mach *wm;

	if(bm < 0){
		/* pick a machine to wire to */
		memset(nwired, 0, sizeof(nwired));
		p->wired = nil;
		for(i=0; (pp = proctab(i)) != nil; i++){
			wm = pp->wired;
			if(wm != nil && pp->pid)
				nwired[wm->machno]++;
		}
		bm = 0;
		for(i=0; i<conf.nmach; i++)
			if(nwired[i] < nwired[bm])
				bm = i;
	} else {
		/* use the virtual machine requested */
		bm = bm % conf.nmach;
	}

	p->wired = MACHP(bm);
	p->mp = p->wired;
}

void
procpriority(Proc *p, int pri, int fixed)
{
	if(pri >= Npriq)
		pri = Npriq - 1;
	else if(pri < 0)
		pri = 0;
	p->basepri = pri;
	p->priority = pri;
	if(fixed){
		p->fixedpri = 1;
	} else {
		p->fixedpri = 0;
	}
}

void
procinit0(void)		/* bad planning - clashes with devproc.c */
{
	procalloc.free = nil;
	/* allocate 1 extra for a nil terminator */
	procalloc.tab = xalloc((conf.nproc+1)*sizeof(Proc*));
	if(procalloc.tab == nil){
		xsummary();
		panic("cannot allocate proctab for %lud procs", conf.nproc);
	}
	memset(procalloc.tab, 0, (conf.nproc+1)*sizeof(Proc*));
	pidinit();
}

/*
 *  sleep if a condition is not true.  Another process will
 *  awaken us after it sets the condition.  When we awaken
 *  the condition may no longer be true.
 *
 *  we lock both the process and the rendezvous to keep r->p
 *  and p->r synchronized.
 */
void
sleep(Rendez *r, int (*f)(void*), void *arg)
{
	int s;
	void (*pt)(Proc*, int, vlong);

	s = splhi();

	if(up->nlocks)
		print("process %lud sleeps with %d locks held, last lock %#p locked at pc %#p, sleep called from %#p\n",
			up->pid, up->nlocks, up->lastlock, up->lastlock->pc, getcallerpc(&r));
	lock(r);
	lock(&up->rlock);
	if(r->p != nil){
		print("double sleep called from %#p, %lud %lud\n", getcallerpc(&r), r->p->pid, up->pid);
		dumpstack();
	}

	/*
	 *  Wakeup only knows there may be something to do by testing
	 *  r->p in order to get something to lock on.
	 *  Flush that information out to memory in case the sleep is
	 *  committed.
	 */
	r->p = up;

	if((*f)(arg) || up->notepending){
		/*
		 *  if condition happened or a note is pending
		 *  never mind
		 */
		r->p = nil;
		unlock(&up->rlock);
		unlock(r);
	} else {
		/*
		 *  now we are committed to
		 *  change state and call scheduler
		 */
		pt = proctrace;
		if(pt != nil)
			pt(up, SSleep, 0);
		up->state = Wakeme;
		up->r = r;
		unlock(&up->rlock);
		unlock(r);
		procswitch();
	}

	if(up->notepending) {
		up->notepending = 0;
		splx(s);
		interrupted();
	}

	splx(s);
}

void
interrupted(void)
{
	if(up->procctl == Proc_exitme && up->closingfgrp != nil)
		forceclosefgrp();
	error(Eintr);
}

void
twakeup(Ureg*, Timer *t)
{
	Proc *p;
	Rendez *trend;

	p = t->ta;
	trend = p->trend;
	if(trend != nil){
		p->trend = nil;
		wakeup(trend);
	}
}

static int
tfn(void *arg)
{
	return up->trend == nil || up->tfn(arg);
}

void
tsleep(Rendez *r, int (*fn)(void*), void *arg, ulong ms)
{
	if(up->tt != nil){
		print("%s %lud: tsleep timer active: mode %d, tf %#p, pc %#p\n",
			up->text, up->pid, up->tmode, up->tf, getcallerpc(&r));
		timerdel(up);
	}
	up->tns = MS2NS(ms);
	up->tmode = Trelative;
	up->tf = twakeup;
	up->ta = up;
	up->trend = r;
	up->tfn = fn;
	timeradd(up);

	if(waserror()){
		up->trend = nil;
		timerdel(up);
		nexterror();
	}
	sleep(r, tfn, arg);
	up->trend = nil;
	timerdel(up);
	poperror();
}

/*
 *  Expects that only one process can call wakeup for any given Rendez.
 *  We hold both locks to ensure that r->p and p->r remain consistent.
 *  Richard Miller has a better solution that doesn't require both to
 *  be held simultaneously, but I'm a paranoid - presotto.
 */
Proc*
wakeup(Rendez *r)
{
	Proc *p;
	int s;

	s = splhi();

	lock(r);
	p = r->p;

	if(p != nil){
		lock(&p->rlock);
		if(p->state != Wakeme || p->r != r){
			iprint("%p %p %d\n", p->r, r, p->state);
			panic("wakeup: state");
		}
		r->p = nil;
		p->r = nil;
		ready(p);
		unlock(&p->rlock);
	}
	unlock(r);

	splx(s);

	return p;
}

/*
 *  if waking a sleeping process, this routine must hold both
 *  p->rlock and r->lock.  However, it can't know them in
 *  the same order as wakeup causing a possible lock ordering
 *  deadlock.  We break the deadlock by giving up the p->rlock
 *  lock if we can't get the r->lock and retrying.
 */
void
procinterrupt(Proc *p)
{
	QLock *q;
	int s;

	p->notepending = 1;

	/* this loop is to avoid lock ordering problems. */
	for(;;){
		Rendez *r;

		s = splhi();
		lock(&p->rlock);
		r = p->r;

		/* waiting for a wakeup? */
		if(r == nil)
			break;	/* no */

		/* try for the second lock */
		if(canlock(r)){
			if(p->state != Wakeme || r->p != p)
				panic("procinterrupt: state %d %d %d",
					r->p != p, p->r != r, p->state);
			p->r = nil;
			r->p = nil;
			ready(p);
			unlock(r);
			break;
		}

		/* give other process time to get out of critical section and try again */
		unlock(&p->rlock);
		splx(s);
		sched();
	}
	unlock(&p->rlock);
	splx(s);

	switch(p->state){
	case Queueing:
		/* Try and pull out of a eqlock */
		if((q = p->eql) != nil){
			lock(&q->use);
			if(p->state == Queueing && p->eql == q){
				Proc *d, *l;

				for(l = nil, d = q->head; d != nil; l = d, d = d->qnext){
					if(d == p){
						if(l != nil)
							l->qnext = p->qnext;
						else
							q->head = p->qnext;
						if(p->qnext == nil)
							q->tail = l;
						p->qnext = nil;
						p->eql = nil;	/* not taken */
						ready(p);
						break;
					}
				}
			}
			unlock(&q->use);
		}
		break;
	case Rendezvous:
		/* Try and pull out of a rendezvous */
		lock(p->rgrp);
		if(p->state == Rendezvous) {
			Proc *d, **l;

			l = &REND(p->rgrp, p->rendtag);
			for(d = *l; d != nil; d = d->rendhash) {
				if(d == p) {
					*l = p->rendhash;
					p->rendval = ~0;
					ready(p);
					break;
				}
				l = &d->rendhash;
			}
		}
		unlock(p->rgrp);
		break;
	}
}

/*
 *  pop a note from the calling process or suicide if theres
 *  no note handler or notify during note handling. 
 *  Called from notify() with up->debug lock held.
 */
char*
popnote(Ureg *u)
{
	up->notepending = 0;
	if(up->nnote == 0)
		return nil;
	assert(up->nnote > 0);
	assert(up->note[0] != nil);
	assert(up->note[0]->ref > 0);

	/* hold off user notes during note handling */
	if(up->notified && up->note[0]->flag == NUser)
		return nil;

	freenote(up->lastnote);
	up->lastnote = up->note[0];
	if(--up->nnote > 0)
		memmove(&up->note[0], &up->note[1], up->nnote*sizeof(Note*));
	up->note[up->nnote] = nil;

	if(u != nil && up->lastnote->ref == 1 && strncmp(up->lastnote->msg, "sys:", 4) == 0){
		int l = strlen(up->lastnote->msg);
		assert(l < ERRMAX);
		assert(userureg(u));
		snprint(up->lastnote->msg+l, ERRMAX-l, " pc=%#p", u->pc);
	}

	if(up->notify == nil || up->notified){
		qunlock(&up->debug);
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", up->lastnote->msg);
		pexit(up->lastnote->msg, up->lastnote->flag!=NDebug);
	}
	up->notified = 1;

	return up->lastnote->msg;
}

static Note*
mknote(char *msg, int flag)
{
	Note *n;

	n = smalloc(sizeof(Note));
	kstrcpy(n->msg, msg, ERRMAX);
	n->flag = flag;
	n->ref = 1;
	return n;
}

int
pushnote(Proc *p, Note *n)
{
	if(p->state <= New || p->state == Broken || p->pid == 0){
		freenote(n);
		return 0;
	}
	assert(n->ref > 0);
	if(n->flag != NUser && (p->notify == nil || p->notified))
		freenotes(p);
	if(p->nnote < NNOTE){
		p->note[p->nnote++] = n;
		procinterrupt(p);
		return 1;
	}
	freenote(n);
	return 0;
}

int
postnote(Proc *p, int dolock, char *msg, int flag)
{
	Note *n;
	int ret;

	if(p == nil)
		return 0;

	n = mknote(msg, flag);
	if(dolock)
		qlock(&p->debug);
	ret = pushnote(p, n);
	if(dolock)
		qunlock(&p->debug);

	return ret;
}

void
postnotepg(ulong noteid, char *msg, int flag)
{
	Note *n;
	Proc *p;
	int i;

	n = mknote(msg, flag);
	for(i = 0; (p = proctab(i)) != nil; i++){
		if(p == up || p->noteid != noteid || p->kp)
			continue;
		qlock(&p->debug);
		if(p->noteid == noteid && !p->kp){
			incref(n);
			pushnote(p, n);
		}
		qunlock(&p->debug);
	}
	freenote(n);
}

/*
 * weird thing: keep at most NBROKEN around
 */
#define	NBROKEN 4
struct
{
	QLock;
	int	n;
	Proc	*p[NBROKEN];
}broken;

static void
addbroken(void)
{
	qlock(&broken);
	if(broken.n == NBROKEN) {
		ready(broken.p[0]);
		memmove(&broken.p[0], &broken.p[1], sizeof(Proc*)*(NBROKEN-1));
		--broken.n;
	}
	broken.p[broken.n++] = up;
	qunlock(&broken);

	edfstop(up);
	up->state = Broken;
	up->psstate = nil;
	sched();
}

void
unbreak(Proc *p)
{
	int b;

	qlock(&broken);
	for(b=0; b < broken.n; b++)
		if(broken.p[b] == p) {
			broken.n--;
			memmove(&broken.p[b], &broken.p[b+1],
					sizeof(Proc*)*(NBROKEN-(b+1)));
			ready(p);
			break;
		}
	qunlock(&broken);
}

int
freebroken(void)
{
	int i, n;

	qlock(&broken);
	n = broken.n;
	for(i=0; i<n; i++) {
		ready(broken.p[i]);
		broken.p[i] = nil;
	}
	broken.n = 0;
	qunlock(&broken);
	return n;
}

void
freenote(Note *n)
{
	if(n == nil || decref(n))
		return;
	free(n);
}

void
freenotes(Proc *p)
{
	while(p->nnote > 0){
		freenote(p->note[--p->nnote]);
		up->note[p->nnote] = nil;
	}
}

void
pexit(char *exitstr, int freemem)
{
	Proc *p;
	Segment **s;
	ulong utime, stime;
	Waitq *wq;
	Fgrp *fgrp;
	Egrp *egrp;
	Rgrp *rgrp;
	Pgrp *pgrp;
	Chan *dot;
	void (*pt)(Proc*, int, vlong);

	up->alarm = 0;
	timerdel(up);
	pt = proctrace;
	if(pt != nil)
		pt(up, SDead, 0);

	/* nil out all the resources under lock (free later) */
	qlock(&up->debug);
	fgrp = up->fgrp;
	up->fgrp = nil;
	egrp = up->egrp;
	up->egrp = nil;
	rgrp = up->rgrp;
	up->rgrp = nil;
	pgrp = up->pgrp;
	up->pgrp = nil;
	dot = up->dot;
	up->dot = nil;
	qunlock(&up->debug);

	if(fgrp != nil)
		closefgrp(fgrp);
	if(egrp != nil)
		closeegrp(egrp);
	if(rgrp != nil)
		closergrp(rgrp);
	if(dot != nil)
		cclose(dot);
	if(pgrp != nil)
		closepgrp(pgrp);

	if(up->parentpid == 0){
		if(exitstr == nil)
			exitstr = "unknown";
		panic("boot process died: %s", exitstr);
	}

	p = up->parent;
	if(p != nil && p->pid == up->parentpid && p->state != Broken){
		wq = smalloc(sizeof(Waitq));
		wq->w.pid = up->pid;
		utime = up->time[TUser] + up->time[TCUser];
		stime = up->time[TSys] + up->time[TCSys];
		wq->w.time[TUser] = tk2ms(utime);
		wq->w.time[TSys] = tk2ms(stime);
		wq->w.time[TReal] = tk2ms(MACHP(0)->ticks - up->time[TReal]);
		if(exitstr != nil && exitstr[0])
			snprint(wq->w.msg, sizeof(wq->w.msg), "%s %lud: %s", up->text, up->pid, exitstr);
		else
			wq->w.msg[0] = '\0';

		lock(&p->exl);
		/*
		 * Check that parent is still alive.
		 */
		if(p->pid == up->parentpid && p->state != Broken) {
			p->nchild--;
			p->time[TCUser] += utime;
			p->time[TCSys] += stime;
			/*
			 * If there would be more than 128 wait records
			 * processes for my parent, then don't leave a wait
			 * record behind.  This helps prevent badly written
			 * daemon processes from accumulating lots of wait
			 * records.
		 	 */
			if(p->nwait < 128) {
				wq->next = p->waitq;
				p->waitq = wq;
				p->nwait++;
				wq = nil;
				wakeup(&p->waitr);
			}
		}
		unlock(&p->exl);
		if(wq != nil)
			free(wq);
	}

	if(!freemem)
		addbroken();

	qlock(&up->debug);

	lock(&up->exl);		/* Prevent my children from leaving waits */
	pidfree(up);
	up->parent = nil;
	up->nchild = up->nwait = 0;
	wakeup(&up->waitr);
	unlock(&up->exl);

	while((wq = up->waitq) != nil){
		up->waitq = wq->next;
		free(wq);
	}

	freenotes(up);
	freenote(up->lastnote);
	up->lastnote = nil;
	up->notified = 0;

	/* release debuggers */
	if(up->pdbg != nil) {
		wakeup(&up->pdbg->sleep);
		up->pdbg = nil;
	}
	if(up->syscalltrace != nil) {
		free(up->syscalltrace);
		up->syscalltrace = nil;
	}
	if(up->watchpt != nil){
		free(up->watchpt);
		up->watchpt = nil;
	}
	up->nwatchpt = 0;
	qunlock(&up->debug);

	qlock(&up->seglock);
	for(s = up->seg; s < &up->seg[NSEG]; s++) {
		if(*s != nil) {
			putseg(*s);
			*s = nil;
		}
	}
	qunlock(&up->seglock);

	edfstop(up);
	if(up->edf != nil){
		free(up->edf);
		up->edf = nil;
	}
	up->state = Moribund;
	sched();
	panic("pexit");
}

static int
haswaitq(void *x)
{
	return ((Proc*)x)->waitq != nil;
}

ulong
pwait(Waitmsg *w)
{
	ulong cpid;
	Waitq *wq;

	if(!canqlock(&up->qwaitr))
		error(Einuse);

	if(waserror()) {
		qunlock(&up->qwaitr);
		nexterror();
	}

	lock(&up->exl);
	while(up->waitq == nil) {
		if(up->nchild == 0) {
			unlock(&up->exl);
			error(Enochild);
		}
		unlock(&up->exl);
		sleep(&up->waitr, haswaitq, up);
		lock(&up->exl);
	}
	wq = up->waitq;
	up->waitq = wq->next;
	up->nwait--;
	unlock(&up->exl);

	qunlock(&up->qwaitr);
	poperror();

	if(w != nil)
		memmove(w, &wq->w, sizeof(Waitmsg));
	cpid = wq->w.pid;
	free(wq);
	return cpid;
}

Proc*
proctab(int i)
{
#define proctab(x) (procalloc.tab[(x)])
	return proctab(i);
}

void
dumpaproc(Proc *p)
{
	ulong bss;
	char *s;

	if(p == nil)
		return;

	bss = 0;
	if(p->seg[BSEG] != nil)
		bss = p->seg[BSEG]->top;

	s = p->psstate;
	if(s == nil)
		s = statename[p->state];
	print("%3lud:%10s pc %#p dbgpc %#p  %8s (%s) ut %ld st %ld bss %lux qpc %#p nl %d nd %lud lpc %#p pri %lud\n",
		p->pid, p->text, p->pc, dbgpc(p),  s, statename[p->state],
		p->time[0], p->time[1], bss, p->qpc, p->nlocks, p->delaysched,
		p->lastlock ? p->lastlock->pc : 0, p->priority);
}

/*
 *  wait till all matching processes have flushed their mmu
 */
static void
procflushmmu(int (*match)(Proc*, void*), void *a)
{
	Proc *await[MAXMACH];
	int i, nm, nwait;
	Proc *p;

	/*
	 *  tell all matching processes to flush their mmu's
	 */
	memset(await, 0, conf.nmach*sizeof(await[0]));
	nwait = 0;
	for(i = 0; (p = proctab(i)) != nil; i++){
		if(p->state > New && (*match)(p, a)){
			p->newtlb = 1;
			for(nm = 0; nm < conf.nmach; nm++){
				if(MACHP(nm)->proc == p){
					coherence();
					MACHP(nm)->flushmmu = 1;
					if(await[nm] == nil)
						nwait++;
					await[nm] = p;
				}
			}
		}
	}

	/*
	 *  wait for all other processors to take a clock interrupt
	 *  and flush their mmu's
	 */
	for(;;){
		if(nwait == 0 || nwait == 1 && await[m->machno] != nil)
			break;

		sched();

		for(nm = 0; nm < conf.nmach; nm++){
			p = await[nm];
			if(p != nil && (MACHP(nm)->proc != p || MACHP(nm)->flushmmu == 0)){
				await[nm] = nil;
				nwait--;
			}
		}
	}
}

static int
matchseg(Proc *p, void *a)
{
	int ns;

	for(ns = 0; ns < NSEG; ns++){
		if(p->seg[ns] == a)
			return 1;
	}
	return 0;
}
void
procflushseg(Segment *s)
{
	procflushmmu(matchseg, s);
}

static int
matchpseg(Proc *p, void *a)
{
	Segment *s;
	int ns;

	for(ns = 0; ns < NSEG; ns++){
		s = p->seg[ns];
		if(s != nil && s->pseg == a)
			return 1;
	}
	return 0;
}
void
procflushpseg(Physseg *ps)
{
	procflushmmu(matchpseg, ps);
}

static int
matchother(Proc *p, void *a)
{
	return p != a;
}
void
procflushothers(void)
{
	procflushmmu(matchother, up);
}

static void
linkproc(void)
{
	spllo();
	(*up->kpfun)(up->kparg);
	pexit("kproc exiting", 0);
}

void
kproc(char *name, void (*func)(void *), void *arg)
{
	static Pgrp *kpgrp;
	Proc *p;

	while((p = newproc()) == nil){
		freebroken();
		resrcwait("no procs for kproc");
	}

	qlock(&p->debug);
	if(up != nil){
		p->slash = up->slash;
		p->dot = up->slash;	/* unlike fork, do not inherit the dot for kprocs */
		if(p->dot != nil)
			incref(p->dot);
	} else {
		p->slash = nil;
		p->dot = nil;
	}

	p->nnote = 0;
 	p->notify = nil;
	p->notified = 0;
	p->notepending = 0;
	p->lastnote = nil;

	p->procmode = 0640;
	p->privatemem = 1;
	p->noswap = 1;
	p->hang = 0;
	p->kp = 1;

	p->kpfun = func;
	p->kparg = arg;
	kprocchild(p, linkproc);

	kstrdup(&p->text, name);
	kstrdup(&p->user, eve);
	kstrdup(&p->args, "");
	p->nargs = 0;
	p->setargs = 0;

	if(kpgrp == nil)
		kpgrp = newpgrp();
	p->pgrp = kpgrp;
	incref(kpgrp);

	p->insyscall = 1;
	memset(p->time, 0, sizeof(p->time));
	p->time[TReal] = MACHP(0)->ticks;
	cycles(&p->kentry);
	p->pcycles = -p->kentry;

	pidalloc(p);

	qunlock(&p->debug);

	procpriority(p, PriKproc, 0);

	ready(p);
}

/*
 *  called splhi() by notify().  See comment in notify for the
 *  reasoning.
 */
void
procctl(void)
{
	char *state;
	ulong s;

	switch(up->procctl) {
	case Proc_exitbig:
		spllo();
		pprint("Killed: Insufficient physical memory\n");
		pexit("Killed: Insufficient physical memory", 1);

	case Proc_exitme:
		spllo();		/* pexit has locks in it */
		pexit("Killed", 1);

	case Proc_traceme:
		if(up->nnote == 0)
			return;
		/* No break */

	case Proc_stopme:
		up->procctl = 0;
		state = up->psstate;
		up->psstate = statename[Stopped];
		/* free a waiting debugger */
		s = spllo();
		qlock(&up->debug);
		if(up->pdbg != nil) {
			wakeup(&up->pdbg->sleep);
			up->pdbg = nil;
		}
		qunlock(&up->debug);
		splhi();
		up->state = Stopped;
		sched();
		up->psstate = state;
		splx(s);
		return;
	}
}

#include "errstr.h"

void
error(char *err)
{
	spllo();

	assert(up->nerrlab < NERR);
	kstrcpy(up->errstr, err, ERRMAX);
	setlabel(&up->errlab[NERR-1]);
	nexterror();
}

void
nexterror(void)
{
	assert(up->nerrlab > 0);
	gotolabel(&up->errlab[--up->nerrlab]);
}

void
exhausted(char *resource)
{
	char buf[ERRMAX];

	snprint(buf, sizeof buf, "no free %s", resource);
	iprint("%s\n", buf);
	error(buf);
}

ulong
procpagecount(Proc *p)
{
	Segment *s;
	ulong pages;
	int i;

	eqlock(&p->seglock);
	if(waserror()){
		qunlock(&p->seglock);
		nexterror();
	}
	pages = 0;
	for(i=0; i<NSEG; i++){
		if((s = p->seg[i]) != nil){
			eqlock(s);
			pages += mcountseg(s);
			qunlock(s);
		}
	}
	qunlock(&p->seglock);
	poperror();

	return pages;
}

void
killbig(char *why)
{
	int i;
	Segment *s;
	ulong l, max;
	Proc *p, *kp;

	max = 0;
	kp = nil;
	for(i = 0; (p = proctab(i)) != nil; i++) {
		if(p->state <= New || p->kp || p->parentpid == 0)
			continue;
		if((p->noswap || (p->procmode & 0222) == 0) && strcmp(eve, p->user) == 0)
			continue;
		l = procpagecount(p);
		if(l > max){
			kp = p;
			max = l;
		}
	}
	if(kp == nil)
		return;
	print("%lud: %s killed: %s\n", kp->pid, kp->text, why);
	qlock(&kp->seglock);
	for(i = 0; (p = proctab(i)) != nil; i++) {
		if(p->state <= New || p->kp)
			continue;
		if(p != kp && p->seg[BSEG] != nil && p->seg[BSEG] == kp->seg[BSEG])
			p->procctl = Proc_exitbig;
	}
	kp->procctl = Proc_exitbig;
	for(i = 0; i < NSEG; i++) {
		s = kp->seg[i];
		if(s == nil)
			continue;
		switch(s->type & SG_TYPE){
		case SG_SHARED:
		case SG_PHYSICAL:
		case SG_FIXED:
		case SG_STICKY:
			continue;
		}
		qlock(s);
		mfreeseg(s, s->base, (s->top - s->base)/BY2PG);
		qunlock(s);
	}
	qunlock(&kp->seglock);
}

/*
 *  change ownership to 'new' of all processes owned by 'old'.  Used when
 *  eve changes.
 */
void
renameuser(char *old, char *new)
{
	Proc *p;
	int i;

	for(i = 0; (p = proctab(i)) != nil; i++){
		qlock(&p->debug);
		if(p->user != nil && strcmp(old, p->user) == 0)
			kstrdup(&p->user, new);
		qunlock(&p->debug);
	}
}

void
procsetuser(char *new)
{
	qlock(&up->debug);
	kstrdup(&up->user, new);
	up->basepri = PriNormal;
	qunlock(&up->debug);
}

/*
 *  time accounting called by clock() splhi'd
 */
void
accounttime(void)
{
	Proc *p;
	ulong n, per;
	static ulong nrun;

	p = m->proc;
	if(p != nil) {
		nrun++;
		p->time[p->insyscall]++;
	}

	/* calculate decaying duty cycles */
	n = perfticks();
	per = n - m->perf.last;
	m->perf.last = n;
	per = ((uvlong)m->perf.period*(HZ-1) + per)/HZ;
	if(per != 0)
		m->perf.period = per;

	m->perf.avg_inidle = ((uvlong)m->perf.avg_inidle*(HZ-1)+m->perf.inidle)/HZ;
	m->perf.inidle = 0;

	m->perf.avg_inintr = ((uvlong)m->perf.avg_inintr*(HZ-1)+m->perf.inintr)/HZ;
	m->perf.inintr = 0;

	/* only one processor gets to compute system load averages */
	if(m->machno != 0)
		return;

	/*
	 * calculate decaying load average.
	 * if we decay by (n-1)/n then it takes
	 * n clock ticks to go from load L to .36 L once
	 * things quiet down.  it takes about 5 n clock
	 * ticks to go to zero.  so using HZ means this is
	 * approximately the load over the last second,
	 * with a tail lasting about 5 seconds.
	 */
	n = nrun;
	nrun = 0;
	n = (nrdy+n)*1000*100;
	load = ((uvlong)load*(HZ-1)+n)/HZ;
	m->load = load/100;
}

/*
 *  A Pid structure is a reference counted hashtable entry
 *  with "pid" being the key and "procindex" being the value.
 *  A entry is allocated atomically by changing the key from
 *  negative or zero to the positive process id number.
 *  Pid's outlive ther Proc's as long as other processes hold
 *  a reference to them such as noteid or parentpid.
 *  This prevents pid reuse when the pid generator wraps.
 */
typedef struct Pid Pid;
struct Pid
{
	Ref;
	long	pid;
	int	procindex;
};

enum {
	PIDMASK = 0x7FFFFFFF,
	PIDSHIFT = 4,	/* log2 bucket size of the hash table */
};

static Pid *pidhashtab;
static ulong pidhashmask;

static void
pidinit(void)
{
	/*
	 * allocate 3 times conf.nproc Pid structures for the hash table
	 * and round up to a power of two as each process can reference
	 * up to 3 unique Pid structures:
	 *	- pid
	 *	- noteid
	 *	- parentpid
	 */
	pidhashmask = 1<<PIDSHIFT;
	while(pidhashmask < conf.nproc*3)
		pidhashmask <<= 1;

	pidhashtab = xalloc(pidhashmask * sizeof(pidhashtab[0]));
	if(pidhashtab == nil){
		xsummary();
		panic("cannot allocate pid hashtable of size %lud", pidhashmask);
	}

	/* make it a mask */
	pidhashmask--;
}

static Pid*
pidlookup(long pid)
{
	Pid *i, *e;
	long o;

	i = &pidhashtab[(pid<<PIDSHIFT) & pidhashmask];
	for(e = &i[1<<PIDSHIFT]; i < e; i++){
		o = i->pid;
		if(o == pid)
			return i;
		if(o == 0)
			break;
	}
	return nil;
}

/*
 *  increment the reference count of a pid (pid>0)
 *  or allocate a new one (pid<=0)
 */
static Pid*
pidadd(long pid)
{
	Pid *i, *e;
	long o;

	if(pid > 0){
		i = pidlookup(pid);
		if(i != nil)
			incref(i);
		return i;
	}
Again:
	do {
		static Ref gen;
		pid = incref(&gen) & PIDMASK;
	} while(pid == 0 || pidlookup(pid) != nil);

	i = &pidhashtab[(pid<<PIDSHIFT) & pidhashmask];
	for(e = &i[1<<PIDSHIFT]; i < e; i++){
		while((o = i->pid) <= 0){
			if(cmpswap(&i->pid, o, pid)){
				incref(i);
				return i;
			}
		}
	}
	/* bucket full, try a different pid */
	goto Again;
}

/*
 *  decrement reference count of a pid and free it
 *  when no references are remaining
 */
static void
piddel(Pid *i)
{
	if(decref(i))
		return;
	i->pid = -1;	/* freed */
}

int
procindex(ulong pid)
{
	Proc *p;
	Pid *i;
	int x;

	i = pidlookup(pid);
	if(i != nil){
		x = i->procindex;
		p = proctab(x);
		if(p != nil && p->pid == pid)
			return x;
	}
	return -1;
}

/*
 *  for the following functions:
 *    setnoteid(), pidalloc() and pidfree()
 *
 *  They need to be called with p->debug held
 *  to prevent devproc's changenoteid() from
 *  changing the noteid under us and make the
 *  update atomic.
 */

/*
 *  change the noteid of a process
 */
ulong
setnoteid(Proc *p, ulong noteid)
{
	Pid *i, *o;

	/*
	 * avoid allocating a new pid when we are the only
	 * user of the noteid
	 */
	o = pidlookup(p->noteid);
	if(noteid == 0 && o->ref == 1)
		return o->pid;

	i = pidadd(noteid);
	if(i == nil)
		error(Ebadarg);
	piddel(o);
	return p->noteid = i->pid;
}

/*
 *  allocate pid, noteid and parentpid to a process
 */
ulong
pidalloc(Proc *p)
{
	Pid *i;

	/* skip for the boot process */
	if(up != nil){
		i = pidadd(up->pid);
		p->parentpid = i->pid;
	} else
		p->parentpid = 0;

	i = pidadd(0);
	i->procindex = p->index;

	if(p->noteid == 0){
		incref(i);
		p->noteid = i->pid;
	} else
		pidadd(p->noteid);

	return p->pid = i->pid;
}

/*
 *  release pid, noteid and parentpid from a process
 */
static void
pidfree(Proc *p)
{
	Pid *i;

	i = pidlookup(p->pid);
	piddel(i);

	if(p->noteid != p->pid)
		i = pidlookup(p->noteid);
	piddel(i);

	if(p->parentpid != 0)
		piddel(pidlookup(p->parentpid));

	p->pid = p->noteid = p->parentpid = 0;
}
