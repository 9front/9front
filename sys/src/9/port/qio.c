#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#define QDEBUG	if(0)

/*
 *  IO queues
 */
typedef struct Queue	Queue;
struct Queue
{
	Lock;

	int	state;
	int	dlen;		/* data length in bytes */
	uint	rp, wp;		/* read/write position (counting BALLOC() bytes) */
	int	limit;		/* max BALLOC() bytes in queue */
	int	inilim;		/* initial limit */
	uchar	noblock;	/* true if writes return immediately when q full */
	uchar	eof;		/* number of eofs read by user */

	Block*	bfirst;		/* buffer */
	Block*	blast;

	void*	arg;		/* argument to kick and bypass */
	void	(*kick)(void*);	/* restart output */
	void	(*bypass)(void*, Block*);	/* bypass queue altogether */

	QLock	rlock;		/* mutex for reading processes */
	Rendez	rr;		/* process waiting to read */

	QLock	wlock;		/* mutex for writing processes */
	Rendez	wr;		/* process waiting to write */

	char	err[ERRMAX];
};

enum
{
	Maxatomic	= 64*1024,
};

uint	qiomaxatomic = Maxatomic;

/*
 *  free a list of blocks
 */
void
freeblist(Block *b)
{
	Block *next;

	for(; b != nil; b = next){
		next = b->next;
		b->next = nil;
		freeb(b);
	}
}

/*
 *  return count of bytes in a string of blocks
 */
int
blocklen(Block *bp)
{
	int len;

	len = 0;
	while(bp != nil) {
		len += BLEN(bp);
		bp = bp->next;
	}
	return len;
}

/*
 *  copy the contents of a string of blocks into
 *  memory from an offset. blocklist kept unchanged.
 *  return number of copied bytes.
 */
long
readblist(Block *b, uchar *p, long n, ulong o)
{
	ulong m, r;

	r = 0;
	while(n > 0 && b != nil){
		m = BLEN(b);
		if(o >= m)
			o -= m;
		else {
			m -= o;
			if(n < m)
				m = n;
			memmove(p, b->rp + o, m);
			p += m;
			r += m;
			n -= m;
			o = 0;
		}
		b = b->next;
	}
	return r;
}

/*
 *  copy the string of blocks into
 *  a single block and free the string
 */
Block*
concatblock(Block *bp)
{
	int len;

	if(bp->next == nil)
		return bp;
	len = blocklen(bp);
	return pullupblock(bp, len);
}

/*
 *  make sure the first block has at least n bytes
 */
Block*
pullupblock(Block *bp, int n)
{
	Block *nbp;
	int i;

	assert(n >= 0);

	/*
	 *  this should almost always be true, it's
	 *  just to avoid every caller checking.
	 */
	if(BLEN(bp) >= n)
		return bp;

	/*
	 *  if not enough room in the first block,
	 *  add another to the front of the list.
	 */
	if(bp->lim - bp->rp < n){
		nbp = allocb(n);
		nbp->next = bp;
		bp = nbp;
	}

	/*
	 *  copy bytes from the trailing blocks into the first
	 */
	n -= BLEN(bp);
	while((nbp = bp->next) != nil){
		i = BLEN(nbp);
		if(i > n) {
			memmove(bp->wp, nbp->rp, n);
			bp->wp += n;
			nbp->rp += n;
			QDEBUG checkb(bp, "pullupblock 1");
			return bp;
		} else {
			/* shouldn't happen but why crash if it does */
			if(i < 0){
				print("pullup negative length packet, called from %#p\n",
					getcallerpc(&bp));
				i = 0;
			}
			memmove(bp->wp, nbp->rp, i);
			bp->wp += i;
			bp->next = nbp->next;
			nbp->next = nil;
			freeb(nbp);
			n -= i;
			if(n == 0){
				QDEBUG checkb(bp, "pullupblock 2");
				return bp;
			}
		}
	}
	freeb(bp);
	return nil;
}

/*
 *  make sure the first block has at least n bytes
 */
Block*
pullupqueue(Queue *q, int n)
{
	Block *b;

	assert(n >= 0);

	if(BLEN(q->bfirst) >= n)
		return q->bfirst;
	q->bfirst = pullupblock(q->bfirst, n);
	for(b = q->bfirst; b != nil && b->next != nil; b = b->next)
		;
	q->blast = b;
	return q->bfirst;
}

/*
 *  trim to len bytes starting at offset
 */
Block *
trimblock(Block *bp, int offset, int len)
{
	ulong l;
	Block *nb, *startb;

	assert(len >= 0);
	assert(offset >= 0);

	QDEBUG checkb(bp, "trimblock 1");
	l = blocklen(bp);
	if(offset == 0 && len == l)
		return bp;
	if(l < offset+len) {
		freeblist(bp);
		return nil;
	}

	while((l = BLEN(bp)) < offset) {
		offset -= l;
		nb = bp->next;
		bp->next = nil;
		freeb(bp);
		bp = nb;
	}

	startb = bp;
	bp->rp += offset;

	while((l = BLEN(bp)) < len) {
		len -= l;
		bp = bp->next;
	}

	bp->wp -= (BLEN(bp) - len);

	if(bp->next != nil) {
		freeblist(bp->next);
		bp->next = nil;
	}

	return startb;
}

/*
 *  pad a block to the front (or the back if size is negative)
 */
Block*
padblock(Block *bp, int size)
{
	int n;
	Block *nbp;

	QDEBUG checkb(bp, "padblock 0");
	if(size >= 0){
		if(bp->rp - bp->base >= size){
			bp->rp -= size;
			return bp;
		}
		n = BLEN(bp);
		nbp = allocb(size+n);
		nbp->rp += size;
		nbp->wp = nbp->rp;
		memmove(nbp->wp, bp->rp, n);
		nbp->wp += n;
		nbp->rp -= size;
	} else {
		size = -size;
		if(bp->lim - bp->wp >= size)
			return bp;
		n = BLEN(bp);
		nbp = allocb(n+size);
		memmove(nbp->wp, bp->rp, n);
		nbp->wp += n;
	}
	nbp->next = bp->next;
	freeb(bp);
	QDEBUG checkb(nbp, "padblock 1");
	return nbp;
}

/*
 *  copy 'count' bytes into a new block
 */
Block*
copyblock(Block *bp, int count)
{
	int l;
	Block *nbp;

	assert(count >= 0);

	QDEBUG checkb(bp, "copyblock 0");
	nbp = allocb(count);
	for(; count > 0 && bp != nil; bp = bp->next){
		l = BLEN(bp);
		if(l > count)
			l = count;
		memmove(nbp->wp, bp->rp, l);
		nbp->wp += l;
		count -= l;
	}
	if(count > 0){
		memset(nbp->wp, 0, count);
		nbp->wp += count;
	}
	QDEBUG checkb(nbp, "copyblock 1");

	return nbp;
}

Block*
adjustblock(Block* bp, int len)
{
	int n;
	Block *nbp;

	if(len < 0){
		freeb(bp);
		return nil;
	}

	if(bp->rp+len > bp->lim){
		nbp = copyblock(bp, len);
		freeblist(bp);
		QDEBUG checkb(nbp, "adjustblock 1");

		return nbp;
	}

	n = BLEN(bp);
	if(len > n)
		memset(bp->wp, 0, len-n);
	bp->wp = bp->rp+len;
	QDEBUG checkb(bp, "adjustblock 2");

	return bp;
}

/*
 *  if the allocated space is way out of line with the used
 *  space, reallocate to a smaller block
 */
Block*
packblock(Block *bp)
{
	Block **l, *nbp;
	int n;

	for(l = &bp; (nbp = *l) != nil; l = &(*l)->next){
		n = BLEN(nbp);
		if((n<<2) < BALLOC(nbp)){
			*l = allocb(n);
			memmove((*l)->wp, nbp->rp, n);
			(*l)->wp += n;
			(*l)->next = nbp->next;
			freeb(nbp);
		}
	}

	return bp;
}

/*
 *  throw away up to count bytes from a
 *  list of blocks.  Return count of bytes
 *  thrown away.
 */
int
pullblock(Block **bph, int count)
{
	Block *bp;
	int n, bytes;

	bytes = 0;
	if(bph == nil)
		return 0;

	while((bp = *bph) != nil && count > 0) {
		QDEBUG checkb(bp, "pullblock ");
		n = BLEN(bp);
		if(count < n)
			n = count;
		bytes += n;
		count -= n;
		bp->rp += n;
		if(BLEN(bp) == 0) {
			*bph = bp->next;
			bp->next = nil;
			freeb(bp);
		}
	}
	return bytes;
}

/*
 *  remove a block from the front of the queue
 */
Block*
qremove(Queue *q)
{
	Block *b;

	b = q->bfirst;
	if(b == nil)
		return nil;
	QDEBUG checkb(b, "qremove");
	q->bfirst = b->next;
	b->next = nil;
	q->dlen -= BLEN(b);
	q->rp += BALLOC(b);
	return b;
}

/*
 *  put a block back to the front of the queue
 */
void
qputback(Queue *q, Block *b)
{
	QDEBUG checkb(b, "qputback");
	b->next = q->bfirst;
	if(q->bfirst == nil)
		q->blast = b;
	q->bfirst = b;
	q->dlen += BLEN(b);
	q->rp -= BALLOC(b);
}

/*
 *  after removing data from the queue,
 *  unlock queue and wakeup blocked writer.
 *  called at interrupt level.
 */
static int
iunlock_consumer(Queue *q)
{
	int s = q->state;

	/* stop flow control when back at or below the limit */
	if((int)(q->wp - q->rp) <= q->limit)
		q->state = s & ~Qflow;

	iunlock(q);

	if(s & Qflow){
		/*
		 * wakeup flow controlled writers.
		 * note that this is done even when q->state
		 * still has Qflow set, as the unblocking
		 * condition depends on the writers local queuing
		 * position, not on the global queue length.
		 */
		wakeup(&q->wr);
	}
	return s;
}

/*
 *  after removing data from the queue,
 *  unlock queue and wakeup blocked writer.
 *  get output going again when it was blocked.
 *  called at process level.
 */
static int
iunlock_reader(Queue *q)
{
	int s = iunlock_consumer(q);

	if(q->kick != nil && s & Qflow)
		(*q->kick)(q->arg);

	return s;
}

/*
 *  after inserting into queue,
 *  unlock queue and wakeup starved reader.
 *  called at interrupt level.
 */
static int
iunlock_producer(Queue *q)
{
	int s = q->state;

	/* start flow control when above the limit */
	if((int)(q->wp - q->rp) > q->limit)
		s |= Qflow;

	q->state = s & ~Qstarve;
	iunlock(q);

	if(s & Qstarve){
		Proc *p = wakeup(&q->rr);

		/* if we just wokeup a higher priority process, let it run */
		if(p != nil && up != nil && p->priority > up->priority && islo())
			sched();
	}
	return s;
}

/*
 *  unlock queue and wakeup starved reader.
 *  get output going again when it was starved.
 *  called at process level.
 */
static int
iunlock_writer(Queue *q)
{
	int s = iunlock_producer(q);

	if(q->kick != nil && s & (Qstarve|Qkick))
		(*q->kick)(q->arg);

	return s;
}

/*
 *  get next block from a queue, return null if nothing there
 *  called at interrupt level.
 */
Block*
qget(Queue *q)
{
	Block *b;

	ilock(q);
	if((b = qremove(q)) == nil){
		q->state |= Qstarve;
		iunlock(q);
		return nil;
	}
	iunlock_consumer(q);

	return b;
}

/*
 *  Interrupt level copy out of a queue, return # bytes copied.
 */
int
qconsume(Queue *q, void *vp, int len)
{
	Block *b, *tofree = nil;
	int n;

	assert(len >= 0);

	ilock(q);
	for(;;) {
		b = q->bfirst;
		if(b == nil){
			q->state |= Qstarve;
			len = -1;
			goto out;
		}
		QDEBUG checkb(b, "qconsume 1");

		n = BLEN(b);
		if(n > 0)
			break;

		/* get rid of zero-length blocks */
		q->bfirst = b->next;
		q->rp += BALLOC(b);

		/* remember to free this */
		b->next = tofree;
		tofree = b;
	};

	if(n < len)
		len = n;
	memmove(vp, b->rp, len);
	b->rp += len;
	q->dlen -= len;

	/* discard the block if we're done with it */
	if((q->state & Qmsg) || len == n){
		q->bfirst = b->next;
		q->rp += BALLOC(b);
		q->dlen -= BLEN(b);

		/* remember to free this */
		b->next = tofree;
		tofree = b;
	}
out:
	iunlock_consumer(q);

	freeblist(tofree);

	return len;
}

/*
 *  add a block list to a queue, return bytes added
 */
int
qaddlist(Queue *q, Block *b)
{
	int len;

	QDEBUG checkb(b, "qaddlist 1");

	/* queue the block */
	if(q->bfirst != nil)
		q->blast->next = b;
	else
		q->bfirst = b;

	len = BLEN(b);
	q->wp += BALLOC(b);
	while(b->next != nil){
		b = b->next;
		QDEBUG checkb(b, "qaddlist 2");
		len += BLEN(b);
		q->wp += BALLOC(b);
	}
	q->dlen += len;
	q->blast = b;
	return len;
}

int
qpass(Queue *q, Block *b)
{
	int len;

	ilock(q);
	if(q->state & Qclosed){
		iunlock(q);
		freeblist(b);
		return 0;
	}
	if(q->state & Qflow){
		iunlock(q);
		freeblist(b);
		return -1;
	}
	len = qaddlist(q, b);
	iunlock_producer(q);

	return len;
}

int
qpassnolim(Queue *q, Block *b)
{
	int len;

	ilock(q);
	if(q->state & Qclosed){
		iunlock(q);
		freeblist(b);
		return 0;
	}
	len = qaddlist(q, b);
	iunlock_producer(q);

	return len;
}

int
qproduce(Queue *q, void *vp, int len)
{
	Block *b;

	assert(len >= 0);

	b = iallocb(len);
	if(b == nil)
		return 0;

	/* save in buffer */
	memmove(b->wp, vp, len);
	b->wp += len;

	return qpass(q, b);
}

/*
 *  copy from offset in the queue
 */
Block*
qcopy(Queue *q, int len, ulong offset)
{
	Block *b;

	assert(len >= 0);

	b = allocb(len);
	ilock(q);
	b->wp += readblist(q->bfirst, b->wp, len, offset);
	iunlock(q);
	return b;
}

/*
 *  called by non-interrupt code
 */
Queue*
qopen(int limit, int msg, void (*kick)(void*), void *arg)
{
	Queue *q;

	assert(limit >= 0);

	q = malloc(sizeof(Queue));
	if(q == nil)
		return nil;

	q->dlen = 0;
	q->wp = q->rp = 0;
	q->limit = q->inilim = limit;
	q->kick = kick;
	q->arg = arg;
	q->state = msg | Qstarve;
	q->eof = 0;
	q->noblock = 0;

	return q;
}

/* open a queue to be bypassed */
Queue*
qbypass(void (*bypass)(void*, Block*), void *arg)
{
	Queue *q;

	q = malloc(sizeof(Queue));
	if(q == nil)
		return nil;

	q->dlen = 0;
	q->wp = q->rp = 0;
	q->limit = 0;
	q->arg = arg;
	q->bypass = bypass;
	q->state = 0;
	q->eof = 0;
	q->noblock = 0;

	return q;
}

static int
notempty(void *a)
{
	Queue *q = a;

	return q->bfirst != nil || (q->state & Qclosed);
}

/*
 *  wait for the queue to be non-empty or closed.
 *  called with q ilocked.
 */
static int
qwait(Queue *q)
{
	/* wait for data */
	for(;;){
		if(q->bfirst != nil)
			break;

		if(q->state & Qclosed){
			if(q->eof >= 3 || *q->err && strcmp(q->err, Ehungup) != 0)
				return -1;
			q->eof++;
			return 0;
		}

		q->state |= Qstarve;	/* flag requesting producer to wake me */
		iunlock(q);
		sleep(&q->rr, notempty, q);
		ilock(q);
	}
	return 1;
}

/*
 *  cut off n bytes from the end of *h. return a new
 *  block with the tail and change *h to refer to the
 *  head.
 */
static Block*
splitblock(Block **h, int n)
{
	Block *a, *b;
	int m;

	a = *h;
	m = BLEN(a) - n;
	if(m < n){
		b = allocb(m);
		memmove(b->wp, a->rp, m);
		b->wp += m;
		a->rp += m;
		*h = b;
		return a;
	} else {
		b = allocb(n);
		a->wp -= n;
		memmove(b->wp, a->wp, n);
		b->wp += n;
		return b;
	}
}

/*
 *  get next block from a queue (up to a limit)
 */
Block*
qbread(Queue *q, int len)
{
	Block *b;
	int n;

	assert(len >= 0);

	eqlock(&q->rlock);
	if(waserror()){
		qunlock(&q->rlock);
		nexterror();
	}

	ilock(q);
	switch(qwait(q)){
	case 0:
		/* queue closed */
		iunlock(q);
		qunlock(&q->rlock);
		poperror();
		return nil;
	case -1:
		/* multiple reads on a closed queue */
		iunlock(q);
		error(q->err);
	}

	/* if we get here, there's at least one block in the queue */
	b = qremove(q);
	n = BLEN(b);

	/* split block if it's too big and this is not a message queue */
	if(n > len){
		n -= len;
		if((q->state & Qmsg) == 0)
			qputback(q, splitblock(&b, n));
		else
			b->wp -= n;
	}
	iunlock_reader(q);

	qunlock(&q->rlock);
	poperror();

	return b;
}

/*
 *  read a queue.  if no data is queued, post a Block
 *  and wait on its Rendez.
 */
long
qread(Queue *q, void *vp, int len)
{
	Block *b, *first, **last;
	int m, n;

	assert(len >= 0);

	eqlock(&q->rlock);
	if(waserror()){
		qunlock(&q->rlock);
		nexterror();
	}

	ilock(q);
again:
	switch(qwait(q)){
	case 0:
		/* queue closed */
		iunlock(q);
		qunlock(&q->rlock);
		poperror();
		return 0;
	case -1:
		/* multiple reads on a closed queue */
		iunlock(q);
		error(q->err);
	}

	/* if we get here, there's at least one block in the queue */
	last = &first;
	if(q->state & Qcoalesce){
		/* when coalescing, 0 length blocks just go away */
		b = q->bfirst;
		m = BLEN(b);
		if(m <= 0){
			freeb(qremove(q));
			goto again;
		}
		/*
		 *  grab the first block plus as many
		 *  following blocks as will partially
		 *  fit in the read.
		 */
		n = 0;
		for(;;) {
			*last = qremove(q);
			n += m;
			if(n >= len || q->bfirst == nil)
				break;
			last = &b->next;
			b = q->bfirst;
			m = BLEN(b);
		}
	} else {
		first = qremove(q);
		n = BLEN(first);
	}

	/* split last block if it's too big and this is not a message queue */
	if(n > len && (q->state & Qmsg) == 0)
		qputback(q, splitblock(last, n - len));

	iunlock_reader(q);

	qunlock(&q->rlock);
	poperror();

	if(waserror()){
		freeblist(first);
		nexterror();
	}
	n = readblist(first, vp, len, 0);
	freeblist(first);
	poperror();

	return n;
}

/*
 *  a Flow represens a flow controlled
 *  writer on queue q with position p.
 */
typedef struct {
	Queue*	q;
	uint	p;
} Flow;

static int
unblocked(void *a)
{
	Flow *f = a;
	Queue *q = f->q;

	return q->noblock || (int)(f->p - q->rp) <= q->limit || (q->state & Qclosed);
}

/*
 *  flow control, wait for queue to drain back to the limit
 */
static void
qflow(Flow *f)
{
	Queue *q = f->q;

	while(!unblocked(f)){
		eqlock(&q->wlock);
		if(waserror()){
			qunlock(&q->wlock);
			nexterror();
		}
		sleep(&q->wr, unblocked, f);
		qunlock(&q->wlock);
		poperror();
	}
}

/*
 *  add a block to a queue obeying flow control
 */
long
qbwrite(Queue *q, Block *b)
{
	Flow flow;
	int len;

	if(q->bypass != nil){
		len = blocklen(b);
		(*q->bypass)(q->arg, b);
		return len;
	}

	if(waserror()){
		freeblist(b);
		nexterror();
	}
	ilock(q);

	/* give up if the queue is closed */
	if(q->state & Qclosed){
		iunlock(q);
		error(q->err);
	}
	/*
	 * if the queue is full,
	 * silently discard when non-blocking
	 */
	if(q->state & Qflow && q->noblock){
		iunlock(q);
		poperror();
		len = blocklen(b);
		freeblist(b);
		return len;
	}
	len = qaddlist(q, b);
	poperror();

	/*
	 * save our current position in queue
	 * for flow control below.
	 */
	flow.q = q;
	flow.p = q->wp;
	if(iunlock_writer(q) & Qflow){
		/*
		 *  flow control, before allowing the process to continue and
		 *  queue more. We do this here so that postnote can only
		 *  interrupt us after the data has been queued.  This means that
		 *  things like 9p flushes and ssl messages will not be disrupted
		 *  by software interrupts.
		 */
		qflow(&flow);
	}

	return len;
}

/*
 *  block here uninterruptable until queue drains.
 */
static void
qbloated(Queue *q)
{
	Flow flow;

	flow.q = q;
	flow.p = q->wp;
	while(waserror()){
		if(up->procctl == Proc_exitme || up->procctl == Proc_exitbig)
			error(Egreg);
	}
	qflow(&flow);
	poperror();
}

/*
 *  write to a queue.  only Maxatomic bytes at a time is atomic.
 */
int
qwrite(Queue *q, void *vp, int len)
{
	int n, sofar;
	Block *b;
	uchar *p = vp;

	assert(len >= 0);

	QDEBUG if(!islo())
		print("qwrite hi %#p\n", getcallerpc(&q));

	/*
	 * when the queue length grew over twice the limit,
	 * block here before allocating more blocks.
	 * this can happen when qflow() is getting
	 * interrupted by notes, preventing effective
	 * flow control.
	 */
	if(q->state & Qflow && (int)(q->wp - q->rp)/2 > q->limit)
		qbloated(q);

	sofar = 0;
	do {
		n = len-sofar;
		if(n > Maxatomic)
			n = Maxatomic;

		b = allocb(n);
		if(waserror()){
			freeb(b);
			nexterror();
		}
		memmove(b->wp, p+sofar, n);
		poperror();
		b->wp += n;

		sofar += qbwrite(q, b);
	} while(sofar < len && (q->state & Qmsg) == 0);

	return len;
}

/*
 *  used by print() to write to a queue.  Since we may be splhi or not in
 *  a process, don't qlock.
 */
int
qiwrite(Queue *q, void *vp, int len)
{
	int n, sofar;
	Block *b;
	uchar *p = vp;

	assert(len >= 0);

	sofar = 0;
	do {
		n = len-sofar;
		if(n > Maxatomic)
			n = Maxatomic;

		b = iallocb(n);
		if(b == nil)
			break;
		memmove(b->wp, p+sofar, n);
		b->wp += n;

		ilock(q);
		if(q->state & (Qflow|Qclosed)){
			iunlock(q);
			freeb(b);
			break;
		}
		sofar += qaddlist(q, b);
		iunlock_writer(q);
	} while(sofar < len && (q->state & Qmsg) == 0);

	return sofar;
}

/*
 *  throw away the next 'len' bytes in the queue
 */
int
qdiscard(Queue *q, int len)
{
	Block *b, *tofree = nil;
	int n, sofar;

	assert(len >= 0);

	ilock(q);
	for(sofar = 0; sofar < len; sofar += n){
		b = q->bfirst;
		if(b == nil)
			break;
		QDEBUG checkb(b, "qdiscard");
		n = BLEN(b);
		if(n <= len - sofar){
			q->bfirst = b->next;
			q->rp += BALLOC(b);

			/* remember to free this */
			b->next = tofree;
			tofree = b;
		} else {
			n = len - sofar;
			b->rp += n;
		}
		q->dlen -= n;
	}
	iunlock_reader(q);

	freeblist(tofree);

	return sofar;
}

/*
 *  flush the output queue
 */
void
qflush(Queue *q)
{
	Block *tofree;

	ilock(q);
	tofree = q->bfirst;
	q->bfirst = nil;
	q->rp = q->wp;
	q->dlen = 0;
	iunlock_reader(q);

	freeblist(tofree);
}

/*
 *  Mark a queue as closed.  No further IO is permitted.
 *  All blocks are released.
 */
void
qclose(Queue *q)
{
	Block *tofree;

	if(q == nil)
		return;

	ilock(q);
	q->state |= Qclosed;
	q->state &= ~(Qflow|Qstarve);
	kstrcpy(q->err, Ehungup, ERRMAX);
	tofree = q->bfirst;
	q->bfirst = nil;
	q->rp = q->wp;
	q->dlen = 0;
	q->noblock = 0;
	iunlock(q);

	/* wake up readers/writers */
	wakeup(&q->rr);
	wakeup(&q->wr);

	/* free queued blocks */
	freeblist(tofree);
}

/*
 *  be extremely careful when calling this,
 *  as there is no reference accounting
 */
void
qfree(Queue *q)
{
	qclose(q);
	free(q);
}

/*
 *  Mark a queue as closed.  Wakeup any readers.  Don't remove queued
 *  blocks.
 */
void
qhangup(Queue *q, char *msg)
{
	ilock(q);
	q->state |= Qclosed;
	if(msg == nil || *msg == '\0')
		msg = Ehungup;
	kstrcpy(q->err, msg, ERRMAX);
	iunlock(q);

	/* wake up readers/writers */
	wakeup(&q->rr);
	wakeup(&q->wr);
}

/*
 *  return non-zero if the q is hungup
 */
int
qisclosed(Queue *q)
{
	return q->state & Qclosed;
}

/*
 *  mark a queue as no longer hung up
 */
void
qreopen(Queue *q)
{
	ilock(q);
	q->state &= ~Qclosed;
	q->state |= Qstarve;
	q->eof = 0;
	q->limit = q->inilim;
	iunlock(q);
}

/*
 *  return bytes queued
 */
int
qlen(Queue *q)
{
	return q->dlen;
}

/*
 *  return true if we can read without blocking
 */
int
qcanread(Queue *q)
{
	return q->bfirst != nil;
}

/*
 *  return non-zero when the queue is full
 */
int
qfull(Queue *q)
{
	return q->state & Qflow;
}

/*
 *  change queue limit
 */
void
qsetlimit(Queue *q, int limit)
{
	assert(limit >= 0);

	ilock(q);
	q->limit = limit;
	iunlock_consumer(q);
}

/*
 *  set blocking/nonblocking
 */
void
qnoblock(Queue *q, int onoff)
{
	ilock(q);
	q->noblock = onoff;
	iunlock_consumer(q);
}
