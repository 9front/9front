#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"ip.h"

enum
{
	IP_TCPPROTO	= 6,

	TCP4_IPLEN	= 8,
	TCP4_PHDRSIZE	= 12,
	TCP4_HDRSIZE	= 20,
	TCP4_TCBPHDRSZ	= 40,
	TCP4_PKT	= TCP4_IPLEN+TCP4_PHDRSIZE,

	TCP6_IPLEN	= 0,
	TCP6_PHDRSIZE	= 40,
	TCP6_HDRSIZE	= 20,
	TCP6_TCBPHDRSZ	= 60,
	TCP6_PKT	= TCP6_IPLEN+TCP6_PHDRSIZE,

	TcptimerOFF	= 0,
	TcptimerON	= 1,
	TcptimerDONE	= 2,
	MSPTICK		= 50,		/* Milliseconds per timer tick */
	TCP_ACK		= 50,		/* Timed ack sequence in ms */
	MAXBACKMS	= 9*60*1000,	/* longest backoff time (ms) before hangup */

	URG		= 0x20,		/* Data marked urgent */
	ACK		= 0x10,		/* Acknowledge is valid */
	PSH		= 0x08,		/* Whole data pipe is pushed */
	RST		= 0x04,		/* Reset connection */
	SYN		= 0x02,		/* Pkt. is synchronise */
	FIN		= 0x01,		/* Start close down */

	EOLOPT		= 0,
	NOOPOPT		= 1,
	MSSOPT		= 2,
	MSS_LENGTH	= 4,		/* Maximum segment size */
	WSOPT		= 3,
	WS_LENGTH	= 3,		/* Bits to scale window size by */
	MSL2		= 10,
	DEF_MSS		= 1460,		/* Default maximum segment */
	DEF_MSS6	= 1220,		/* Default maximum segment (min) for v6 */
	DEF_RTT		= 500,		/* Default round trip */
	DEF_KAT		= 120000,	/* Default time (ms) between keep alives */
	MAX_KAT		= 3,		/* Maximum number of keep-alive timeouts */
	TCP_LISTEN	= 0,		/* Listen connection */
	TCP_CONNECT	= 1,		/* Outgoing connection */

	TCPREXMTTHRESH	= 3,		/* dupack threshhold for rxt */

	FORCE		= 1,
	SYNACK		= 2,

	LOGAGAIN	= 3,		/* alpha 1/8 */
	LOGDGAIN	= 2,		/* beta 1/4 */

	Closed		= 0,		/* Connection states */
	Listen,
	Syn_sent,
	Syn_received,
	Established,
	Finwait1,
	Finwait2,
	Close_wait,
	Closing,
	Last_ack,
	Time_wait,

	Maxlimbo	= 1000,		/* maximum procs waiting for response to SYN ACK */
	NLHT		= 256,		/* hash table size, must be a power of 2 */
	LHTMASK		= NLHT-1,

	/*
	 * window is 64kb · 2ⁿ
	 * these factors determine the ultimate bandwidth-delay product.
	 * 64kb · 2⁵ = 2mb, or 2x overkill for 100mbps · 70ms.
	 * 64kb · 2⁷ = 8mb, or around 1000mbps · 70ms.
	 */
	QSCALE		= 7,
	QMAX		= 64*1024-1,
};

/* negative return from ipoput means no route */
static char Enoroute[] = "no route";

/* Must correspond to the enumeration above */
static char *tcpstates[] =
{
	"Closed", 	"Listen", 	"Syn_sent", "Syn_received",
	"Established", 	"Finwait1",	"Finwait2", "Close_wait",
	"Closing", 	"Last_ack", 	"Time_wait"
};

typedef struct Tcptimer Tcptimer;
struct Tcptimer
{
	Tcptimer	*next;
	Tcptimer	*prev;
	Tcptimer	*readynext;
	int	state;
	int	start;
	int	count;
	void	(*func)(void*);
	void	*arg;
};

/*
 *  v4 and v6 pseudo headers used for
 *  checksuming tcp
 */
typedef struct Tcp4hdr Tcp4hdr;
struct Tcp4hdr
{
	uchar	vihl;		/* Version and header length */
	uchar	tos;		/* Type of service */
	uchar	length[2];	/* packet length */
	uchar	id[2];		/* Identification */
	uchar	frag[2];	/* Fragment information */
	uchar	ttl;
	uchar	proto;
	uchar	tcplen[2];
	uchar	tcpsrc[4];
	uchar	tcpdst[4];
	uchar	tcpsport[2];
	uchar	tcpdport[2];
	uchar	tcpseq[4];
	uchar	tcpack[4];
	uchar	tcpflag[2];
	uchar	tcpwin[2];
	uchar	tcpcksum[2];
	uchar	tcpurg[2];
	/* Options segment */
	uchar	tcpopt[1];
};

typedef struct Tcp6hdr Tcp6hdr;
struct Tcp6hdr
{
	uchar	vcf[4];
	uchar	ploadlen[2];
	uchar	proto;
	uchar	ttl;
	uchar	tcpsrc[IPaddrlen];
	uchar	tcpdst[IPaddrlen];
	uchar	tcpsport[2];
	uchar	tcpdport[2];
	uchar	tcpseq[4];
	uchar	tcpack[4];
	uchar	tcpflag[2];
	uchar	tcpwin[2];
	uchar	tcpcksum[2];
	uchar	tcpurg[2];
	/* Options segment */
	uchar	tcpopt[1];
};

/*
 *  this represents the control info
 *  for a single packet.  It is derived from
 *  a packet in ntohtcp{4,6}() and stuck into
 *  a packet in htontcp{4,6}().
 */
typedef struct Tcp Tcp;
struct	Tcp
{
	ushort	source;
	ushort	dest;
	ulong	seq;
	ulong	ack;
	uchar	flags;
	uchar	update;
	uchar	ws;	/* window scale option */
	ulong	wnd;	/* prescaled window*/
	ushort	mss;	/* max segment size option (if not zero) */
	ushort	len;	/* size of data */
};

/*
 *  this header is malloc'd to thread together fragments
 *  waiting to be coalesced
 */
typedef struct Reseq Reseq;
struct Reseq
{
	Reseq	*next;
	Tcp	seg;
	Block	*bp;
	ushort	length;
};

/*
 *  the qlock in the Conv locks this structure
 */
typedef struct Tcpctl Tcpctl;
struct Tcpctl
{
	Conv	*bypass;		/* The other when bypassing */
	uchar	state;			/* Connection state */
	uchar	flags;			/* State flags */
	uchar	flgcnt;			/* Number of flags in the send sequence (FIN,SYN) */
	struct {
		ulong	una;		/* Unacked data pointer */
		ulong	nxt;		/* Next sequence expected */
		ulong	ptr;		/* Data pointer */
		ulong	wnd;		/* Tcp send window */
		ulong	wl2;		/* Seg.ack of last window update */
		ulong	wl1;		/* Seg.seq of last window update */
		uchar	scale;		/* how much to left shift window in received packets */
		uchar	rto;		/* retransmit timeout counter */ 
		/* to implement tahoe and reno TCP */
		uchar	recovery;	/* loss recovery flag */
		ulong	rxt;		/* right window marker for recovery "recover" rfc3782 */
		ulong	dupacks;	/* number of duplicate acks rcvd */
		ulong	partialack;	/* partial acks received during recovery */
	} snd;
	struct {
		ulong	nxt;		/* Receive pointer to next uchar slot */
		ulong	ackptr;		/* Last acked sequence */
		ulong	wptr;		/* Right side of receive window */
		ulong	wsnt;		/* Last wptr sent */
		ulong	wnd;		/* Receive window incoming */
		uchar	scale;		/* how much to right shift window in transmitted packets */
	} rcv;
	ulong	iss;			/* Initial sequence number */
	ulong	cwind;			/* Congestion window */
	ulong	abcbytes;		/* appropriate byte counting rfc 3465 */
	ulong	ssthresh;		/* Slow start threshold */
	ushort	mss;			/* Maximum segment size */
	uchar	scale;			/* desired rcv.scale */
	ulong	window;			/* Our receive window (queue) */
	ulong	overlap;		/* Overlap of data re-recevived */
	Reseq	*reseq;			/* Resequencing queue */
	int	nreseq;
	int	reseqlen;
	int	backoff;		/* Exponential backoff counter */
	int	backedoff;		/* ms we've backed off for rexmits */
	Tcptimer	timer;			/* Activity timer */
	Tcptimer	acktimer;		/* Acknowledge timer */
	Tcptimer	katimer;		/* keep alive timer */
	ulong	kato;			/* keep alive timeouts */
	ulong	time;			/* time Finwait2 or Syn_received or timer was set */
	ulong	timeuna;		/* snd.una when time was set */
	ulong	rttime;			/* Sent time for rtt measurement */
	ulong	rttseq;			/* Round trip sequence */
	int	srtt;			/* Smoothed round trip */
	int	mdev;			/* Mean deviation of round trip */

	union {
		Tcp4hdr	tcp4hdr;
		Tcp6hdr	tcp6hdr;
	} protohdr;		/* prototype header */
};

/*
 *  New calls are put in limbo rather than having a conversation structure
 *  allocated.  Thus, a SYN attack results in lots of limbo'd calls but not
 *  any real Conv structures mucking things up.  Calls in limbo rexmit their
 *  SYN ACK up to 4 times. They disappear after 2.5 seconds.
 */
typedef struct Limbo Limbo;
struct Limbo
{
	Limbo	*next;

	uchar	laddr[IPaddrlen];
	uchar	raddr[IPaddrlen];
	ushort	lport;
	ushort	rport;
	ulong	irs;		/* initial received sequence */
	ulong	iss;		/* initial sent sequence */
	ushort	mss;		/* mss from the other end */
	uchar	ws;		/* ws from the other end */
	uchar	rexmits;	/* number of retransmissions */
	ulong	lastsend;	/* last time we sent a synack */
	uchar	version;	/* v4 or v6 */
};

static int	tcp_irtt = DEF_RTT;	/* Initial guess at round trip time */

enum {
	/* MIB stats */
	MaxConn,
	ActiveOpens,
	PassiveOpens,
	EstabResets,
	CurrEstab,
	InSegs,
	OutSegs,
	RetransSegs,
	RetransSegsSent,
	RetransTimeouts,
	InErrs,
	OutRsts,

	/* non-MIB stats */
	CsumErrs,
	HlenErrs,
	LenErrs,
	Resequenced,
	OutOfOrder,
	ReseqBytelim,
	ReseqPktlim,
	Delayack,
	Wopenack,

	Recovery,
	RecoveryDone,
	RecoveryRTO,
	RecoveryNoSeq,
	RecoveryCwind,
	RecoveryPA,

	InLimbo,

	Nstats
};

static char *statnames[Nstats] =
{
[MaxConn]	"MaxConn",
[ActiveOpens]	"ActiveOpens",
[PassiveOpens]	"PassiveOpens",
[EstabResets]	"EstabResets",
[CurrEstab]	"CurrEstab",
[InSegs]	"InSegs",
[OutSegs]	"OutSegs",
[RetransSegs]	"RetransSegs",
[RetransSegsSent]	"RetransSegsSent",
[RetransTimeouts]	"RetransTimeouts",
[InErrs]	"InErrs",
[OutRsts]	"OutRsts",
[CsumErrs]	"CsumErrs",
[HlenErrs]	"HlenErrs",
[LenErrs]	"LenErrs",
[OutOfOrder]	"OutOfOrder",
[Resequenced]	"Resequenced",
[ReseqBytelim]	"ReseqBytelim",
[ReseqPktlim]	"ReseqPktlim",
[Delayack]	"Delayack",
[Wopenack]	"Wopenack",

[Recovery]	"Recovery",
[RecoveryDone]	"RecoveryDone",
[RecoveryRTO]	"RecoveryRTO",

[RecoveryNoSeq]	"RecoveryNoSeq",
[RecoveryCwind]	"RecoveryCwind",
[RecoveryPA]	"RecoveryPA",

[InLimbo]	"InLimbo",
};

typedef struct Tcppriv Tcppriv;
struct Tcppriv
{
	/* List of active timers */
	QLock 	tl;
	Tcptimer *timers;

	/* hash table for matching conversations */
	Ipht	ht;

	/* calls in limbo waiting for an ACK to our SYN ACK */
	int	nlimbo;
	Limbo	*lht[NLHT];

	uvlong	stats[Nstats];

	int ackprocstarted;
};

static	int	addreseq(Fs*, Tcpctl*, Tcppriv*, Tcp*, Block**, ushort);
static	int	dumpreseq(Tcpctl*);
static	int	getreseq(Tcpctl*, Tcp*, Block**, ushort*);
static	void	limbo(Conv*, Tcp*, uchar*, uchar*, int);
static	void	limborexmit(Proto*);
static	void	localclose(Conv*, char*);
static	void	procsyn(Conv*, Tcp*);
static	void	tcpacktimer(void*);
static	void	tcpiput(Proto*, Ipifc*, Block*);
static	void	tcpkeepalive(void*);
static	void	tcpoutput(Conv*);
static	void	tcprcvwin(Conv*);
static	ulong	tcprxmit(Conv*);
static	void	tcpsetmss(Conv*, ushort);
static	void	tcpsetscale(Conv*, uchar);
static	void	tcpsettimer(Conv*);
static	void	tcpsndsyn(Conv*);
static	void	tcpstart(Conv*, int);
static	void	tcptimeout(void*);
static	int	tcptrim(Tcpctl*, Tcp*, Block**, ushort*);

static void
tcpsetstate(Conv *s, uchar newstate)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	Tcppriv *tpriv;
	uchar oldstate;

	oldstate = tcb->state;
	if(oldstate == newstate)
		return;

	switch(newstate) {
	case Closed:
		qclose(s->rq);
		qclose(s->wq);
		qclose(s->eq);
		if(tcb->bypass != nil){
			tcb->bypass = nil;
			qsetbypass(s->wq, nil);
		}
		break;

	case Close_wait:		/* Remote closes */
		qhangup(s->rq, nil);
		break;
	}

	tcb->state = newstate;

	tpriv = (Tcppriv*)s->p->priv;
	if(oldstate == Established)
		tpriv->stats[CurrEstab]--;
	if(newstate == Established)
		tpriv->stats[CurrEstab]++;

	if(oldstate == Syn_sent && newstate != Closed)
		Fsconnected(s, nil);
}

static char*
tcpconnect(Conv *c, char **argv, int argc)
{
	Tcpctl *tcb = (Tcpctl*)c->ptcl;
	char *e;

	if(tcb->state != Closed)
		return Econinuse;

	e = Fsstdconnect(c, argv, argc);
	if(e != nil)
		return e;
	tcpstart(c, TCP_CONNECT);

	return nil;
}

static int
tcpstate(Conv *c, char *state, int n)
{
	Tcpctl *tcb = (Tcpctl*)c->ptcl;

	return snprint(state, n, "%s qin %d qout %d"
		" rq %d.%d ovl %lud"
		" srtt %d>>%d mdev %d"
		" timer %d/%d"
		" sst %lud cwin %lud"
		" swin %lud>>%d rwin %lud>>%d"
		" ka %d/%d %lud\n",
		tcpstates[tcb->state],
		c->rq != nil ? qlen(c->rq) : 0,
		c->wq != nil ? qlen(c->wq) : 0,
		tcb->nreseq, tcb->reseqlen, tcb->overlap,
		tcb->srtt, LOGAGAIN, tcb->mdev,
		tcb->timer.count*MSPTICK, tcb->timer.start*MSPTICK,
		tcb->ssthresh, tcb->cwind,
		tcb->snd.wnd, tcb->snd.scale, tcb->rcv.wnd, tcb->rcv.scale,
		tcb->katimer.count*MSPTICK, tcb->katimer.start*MSPTICK, tcb->kato);
}

static int
tcpinuse(Conv *c)
{
	Tcpctl *tcb = (Tcpctl*)c->ptcl;

	return tcb->state != Closed;
}

static char*
tcpannounce(Conv *c, char **argv, int argc)
{
	Tcpctl *tcb = (Tcpctl*)c->ptcl;
	char *e;

	if(tcb->state != Closed)
		return Econinuse;

	e = Fsstdannounce(c, argv, argc);
	if(e != nil)
		return e;
	tcpstart(c, TCP_LISTEN);
	Fsconnected(c, nil);

	return nil;
}

/*
 *  tcpclose is always called with the c locked
 */
static void
tcpclose(Conv *c)
{
	Tcpctl *tcb = (Tcpctl*)c->ptcl;

	qhangup(c->rq, nil);
	qhangup(c->wq, nil);
	qhangup(c->eq, nil);
	qflush(c->rq);

	switch(tcb->state) {
	case Listen:
		/*
		 *  reset any incoming calls to this listener
		 */
		Fsconnected(c, "Hangup");

		localclose(c, nil);
		break;
	case Closed:
	case Syn_sent:
		localclose(c, nil);
		break;
	case Established:
		if(tcb->bypass != nil){
			qhangup(tcb->bypass->rq, nil);
			localclose(c, nil);
			break;
		}
		/* wet floor */
	case Syn_received:
		tcb->flgcnt++;
		tcb->snd.nxt++;
		tcpsetstate(c, Finwait1);
		tcpoutput(c);
		break;
	case Close_wait:
		tcb->flgcnt++;
		tcb->snd.nxt++;
		tcpsetstate(c, Last_ack);
		tcpoutput(c);
		break;
	}
}

static void
tcpkick(void *arg)
{
	Conv *s = (Conv*)arg;
	Tcpctl *tcb = (Tcpctl*)s->ptcl;

	qlock(s);
	switch(tcb->state) {
	case Established:
		if(tcb->bypass != nil)
			break;
		/* wet floor */
	case Syn_sent:
	case Syn_received:
	case Close_wait:
		/*
		 * Push data
		 */
		tcpoutput(s);
		break;
	default:
		localclose(s, "Hangup");
		break;
	}
	qunlock(s);
}

static int
seq_in(ulong x, ulong low, ulong high)
{
	x -= low, high -= low;
	return (int)x >= 0 && (int)x < high;
}

static int
seq_lt(ulong x, ulong y)
{
	return (int)(x-y) < 0;
}

static int
seq_le(ulong x, ulong y)
{
	return (int)(x-y) <= 0;
}

static int
seq_gt(ulong x, ulong y)
{
	return (int)(x-y) > 0;
}

static int
seq_ge(ulong x, ulong y)
{
	return (int)(x-y) >= 0;
}

static void
tcprcvwin(Conv *s)				/* Call with tcb locked */
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	ulong w;

	w = tcb->window - qlen(s->rq);

	/* RFC 1122 § 4.2.3.3 silly window syndrome avoidance */
	if((int)w < tcb->rcv.wnd + tcb->mss)
		w = 0;

	w += tcb->rcv.nxt;

	/* RFC 1122 § 4.2.2.16 do not move right edge of window left */
	if(seq_lt(w, tcb->rcv.wptr))
		w = tcb->rcv.wptr;

	tcb->rcv.wptr = w;
	tcb->rcv.wnd = w - tcb->rcv.nxt;
}

static void
tcpacktimer(void *arg)
{
	Conv *s = (Conv*)arg;
	Tcpctl *tcb = (Tcpctl*)s->ptcl;

	qlock(s);
	if(tcb->state != Closed && tcb->bypass == nil){
		tcb->flags |= FORCE;
		tcpoutput(s);
	}
	qunlock(s);
}

static void
tcpcongestion(Tcpctl *tcb)
{
	ulong inflight;

	inflight = tcb->snd.nxt - tcb->snd.una;
	if(inflight > tcb->cwind)
		inflight = tcb->cwind;
	tcb->ssthresh = inflight / 2;
	if(tcb->ssthresh < 2*tcb->mss)
		tcb->ssthresh = 2*tcb->mss;
}

enum {
	L		= 2,		/* aggressive slow start; legal values ∈ (1.0, 2.0) */
};

static void
tcpabcincr(Tcpctl *tcb, uint acked)
{
	uint limit;

	tcb->abcbytes += acked;
	if(tcb->cwind < tcb->ssthresh){
		/* slow start */
		if(tcb->snd.rto)
			limit = 1*tcb->mss;
		else
			limit = L*tcb->mss;
		tcb->cwind += MIN(tcb->abcbytes, limit);
		tcb->abcbytes = 0;
	}
	else{
		tcb->snd.rto = 0;
		/* avoidance */
		if(tcb->abcbytes >= tcb->cwind){
			tcb->abcbytes -= tcb->cwind;
			tcb->cwind += tcb->mss;
		}
	}
}

static void
tcpcreate(Conv *c)
{
	c->rq = qopen(QMAX, Qcoalesce, tcpacktimer, c);
	c->wq = qopen(QMAX, Qkick, tcpkick, c);
}

static void
timerstate(Tcppriv *priv, Tcptimer *t, int newstate)
{
	if(newstate != TcptimerON){
		if(t->state == TcptimerON){
			/* unchain */
			if(priv->timers == t){
				priv->timers = t->next;
				if(t->prev != nil)
					panic("timerstate1");
			}
			if(t->next)
				t->next->prev = t->prev;
			if(t->prev)
				t->prev->next = t->next;
			t->next = t->prev = nil;
		}
	} else {
		if(t->state != TcptimerON){
			/* chain */
			if(t->prev != nil || t->next != nil)
				panic("timerstate2");
			t->prev = nil;
			t->next = priv->timers;
			if(t->next)
				t->next->prev = t;
			priv->timers = t;
		}
	}
	t->state = newstate;
}

static void
tcpackproc(void *arg)
{
	Proto *tcp = (Proto*)arg;
	Tcppriv *priv = (Tcppriv*)tcp->priv;
	Tcptimer *t, *tp, *timeo;

	while(waserror())
		;

	for(;;) {
		tsleep(&up->sleep, return0, 0, MSPTICK);

		qlock(&priv->tl);
		timeo = nil;
		for(t = priv->timers; t != nil; t = tp) {
			tp = t->next;
 			if(t->state == TcptimerON) {
				if(--(t->count) == 0) {
					timerstate(priv, t, TcptimerDONE);
					t->readynext = timeo;
					timeo = t;
				}
			}
		}
		qunlock(&priv->tl);

		while((t = timeo) != nil){
			timeo = t->readynext;
			if(t->state == TcptimerDONE && !waserror()){
				(*t->func)(t->arg);
				poperror();
			}
		}

		limborexmit(tcp);
	}
}

static void
tcpgo(Tcppriv *priv, Tcptimer *t)
{
	if(t == nil || t->start == 0)
		return;

	qlock(&priv->tl);
	t->count = t->start;
	timerstate(priv, t, TcptimerON);
	qunlock(&priv->tl);
}

static void
tcphalt(Tcppriv *priv, Tcptimer *t)
{
	if(t == nil)
		return;

	qlock(&priv->tl);
	timerstate(priv, t, TcptimerOFF);
	qunlock(&priv->tl);
}

static void
localclose(Conv *s, char *reason)	/* called with c locked */
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	Tcppriv *tpriv = (Tcppriv*)s->p->priv;

	iphtrem(&tpriv->ht, s);

	tcphalt(tpriv, &tcb->timer);
	tcphalt(tpriv, &tcb->acktimer);
	tcphalt(tpriv, &tcb->katimer);

	/* Flush reassembly queue; nothing more can arrive */
	dumpreseq(tcb);

	if(tcb->state == Syn_sent)
		Fsconnected(s, reason);
	if(s->state == Announced)
		wakeup(&s->listenr);

	qhangup(s->rq, reason);
	qhangup(s->wq, reason);

	tcpsetstate(s, Closed);
}

/* mtu (- TCP + IP hdr len) of 1st hop */
static int
tcpmtu(Route *r, uchar *scale, int version)
{
	Ipifc *ifc;
	int mtu;

	*scale = QSCALE;

	/*
	 * currently we do not implement path MTU discovery
	 * so use interface MTU *only* if directly reachable
	 * or when we use V4 which allows routers to fragment.
	 * otherwise, we use the default MSS which assumes a
	 * safe minimum MTU of 1280 bytes for V6.
	 */  
	if(r != nil && (ifc = r->ifc) != nil){
		mtu = ifc->maxtu - ifc->m->hsize;
		if(version == V4)
			return mtu - (TCP4_PKT + TCP4_HDRSIZE);
		mtu -= TCP6_PKT + TCP6_HDRSIZE;
		if((r->type & (Rifc|Runi)) != 0 || mtu <= DEF_MSS6)
			return mtu;
	}
	if(version == V6)
		return DEF_MSS6;
	else
		return DEF_MSS;
}

static void
inittcpctl(Conv *s, int mode)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	Tcp4hdr* h4;
	Tcp6hdr* h6;

	memset(tcb, 0, sizeof(Tcpctl));

	tcb->srtt = tcp_irtt<<LOGAGAIN;
	tcb->mdev = tcp_irtt<<(LOGDGAIN-1);

	/* setup timers */
	tcb->timer.start = tcp_irtt / MSPTICK;
	tcb->timer.func = tcptimeout;
	tcb->timer.arg = s;

	tcb->acktimer.start = TCP_ACK / MSPTICK;
	tcb->acktimer.func = tcpacktimer;
	tcb->acktimer.arg = s;

	tcb->katimer.start = 0;	/* not enabled by default */
	tcb->katimer.func = tcpkeepalive;
	tcb->katimer.arg = s;

	if(mode == TCP_LISTEN)
		return;

	if(ipcmp(s->laddr, IPnoaddr) == 0)
		findlocalip(s->p->f, s->laddr, s->raddr);

	/* create a prototype(pseudo) header */
	switch(s->ipversion){
	case V4:
		h4 = &tcb->protohdr.tcp4hdr;
		memset(h4, 0, sizeof(*h4));
		h4->vihl = IP_VER4;
		h4->proto = IP_TCPPROTO;
		hnputs(h4->tcpsport, s->lport);
		hnputs(h4->tcpdport, s->rport);
		v6tov4(h4->tcpsrc, s->laddr);
		v6tov4(h4->tcpdst, s->raddr);

		tcb->mss = DEF_MSS;
		break;
	case V6:
		h6 = &tcb->protohdr.tcp6hdr;
		memset(h6, 0, sizeof(*h6));
		h6->proto = IP_TCPPROTO;
		hnputs(h6->tcpsport, s->lport);
		hnputs(h6->tcpdport, s->rport);
		ipmove(h6->tcpsrc, s->laddr);
		ipmove(h6->tcpdst, s->raddr);

		tcb->mss = DEF_MSS6;
		break;
	default:
		panic("inittcpctl: version %d", s->ipversion);
	}
	tcpsetscale(s, 0);
}

/*
 *  called with s locked
 */
static void
tcpstart(Conv *s, int mode)
{
	Tcppriv *tpriv = (Tcppriv*)s->p->priv;

	if(tpriv->ackprocstarted == 0){
		qlock(&tpriv->tl);
		if(tpriv->ackprocstarted == 0){
			char kpname[KNAMELEN];

			snprint(kpname, sizeof(kpname), "#I%dtcpack", s->p->f->dev);
			kproc(kpname, tcpackproc, s->p);
			tpriv->ackprocstarted = 1;
		}
		qunlock(&tpriv->tl);
	}

	inittcpctl(s, mode);

	iphtadd(&tpriv->ht, s);
	switch(mode) {
	case TCP_LISTEN:
		tpriv->stats[PassiveOpens]++;
		tcpsetstate(s, Listen);
		break;

	case TCP_CONNECT:
		tpriv->stats[ActiveOpens]++;
		tcpsndsyn(s);
		tcpsetstate(s, Syn_sent);
		tcpoutput(s);
		break;
	}
}

static char*
tcpflag(char *buf, char *e, ushort flag)
{
	char *p;

	p = seprint(buf, e, "%d", flag>>10);	/* Head len */
	if(flag & URG)
		p = seprint(p, e, " URG");
	if(flag & ACK)
		p = seprint(p, e, " ACK");
	if(flag & PSH)
		p = seprint(p, e, " PSH");
	if(flag & RST)
		p = seprint(p, e, " RST");
	if(flag & SYN)
		p = seprint(p, e, " SYN");
	if(flag & FIN)
		p = seprint(p, e, " FIN");
	USED(p);
	return buf;
}

static Block*
htontcp6(Tcp *tcph, Block *data, Tcp6hdr *ph)
{
	int dlen;
	Tcp6hdr *h;
	ushort hdrlen, optpad = 0;
	uchar *opt;

	hdrlen = TCP6_HDRSIZE;
	if(tcph->flags & SYN){
		if(tcph->mss)
			hdrlen += MSS_LENGTH;
		if(tcph->ws)
			hdrlen += WS_LENGTH;
		optpad = hdrlen & 3;
		if(optpad)
			optpad = 4 - optpad;
		hdrlen += optpad;
	}

	if(data) {
		dlen = blocklen(data);
		data = padblock(data, hdrlen + TCP6_PKT);
	}
	else {
		dlen = 0;
		data = allocb(hdrlen + TCP6_PKT + 64);	/* the 64 pad is to meet mintu's */
		data->wp += hdrlen + TCP6_PKT;
	}

	/* copy in pseudo ip header plus port numbers */
	h = (Tcp6hdr *)(data->rp);
	memmove(h, ph, TCP6_TCBPHDRSZ);

	/* compose pseudo tcp header, do cksum calculation */
	hnputl(h->vcf, hdrlen + dlen);
	h->ploadlen[0] = h->ploadlen[1] = h->proto = 0;
	h->ttl = ph->proto;

	/* copy in variable bits */
	hnputl(h->tcpseq, tcph->seq);
	hnputl(h->tcpack, tcph->ack);
	hnputs(h->tcpflag, (hdrlen<<10) | tcph->flags);
	hnputs(h->tcpwin, tcph->wnd);
	hnputs(h->tcpurg, 0);

	if(tcph->flags & SYN){
		opt = h->tcpopt;
		if(tcph->mss != 0){
			*opt++ = MSSOPT;
			*opt++ = MSS_LENGTH;
			hnputs(opt, tcph->mss);
			opt += 2;
		}
		if(tcph->ws != 0){
			*opt++ = WSOPT;
			*opt++ = WS_LENGTH;
			*opt++ = tcph->ws;
		}
		while(optpad-- > 0)
			*opt++ = NOOPOPT;
	}

	hnputs(h->tcpcksum, ptclcsum(data, TCP6_IPLEN, hdrlen+dlen+TCP6_PHDRSIZE));

	/* move from pseudo header back to normal ip header */
	memset(h->vcf, 0, 4);
	h->vcf[0] = IP_VER6;
	hnputs(h->ploadlen, hdrlen+dlen);
	h->proto = ph->proto;

	return data;
}

static Block*
htontcp4(Tcp *tcph, Block *data, Tcp4hdr *ph)
{
	int dlen;
	Tcp4hdr *h;
	ushort hdrlen, optpad = 0;
	uchar *opt;

	hdrlen = TCP4_HDRSIZE;
	if(tcph->flags & SYN){
		if(tcph->mss)
			hdrlen += MSS_LENGTH;
		if(1)
			hdrlen += WS_LENGTH;
		optpad = hdrlen & 3;
		if(optpad)
			optpad = 4 - optpad;
		hdrlen += optpad;
	}

	if(data) {
		dlen = blocklen(data);
		data = padblock(data, hdrlen + TCP4_PKT);
	}
	else {
		dlen = 0;
		data = allocb(hdrlen + TCP4_PKT + 64);	/* the 64 pad is to meet mintu's */
		data->wp += hdrlen + TCP4_PKT;
	}

	/* copy in pseudo ip header plus port numbers */
	h = (Tcp4hdr *)(data->rp);
	memmove(h, ph, TCP4_TCBPHDRSZ);

	/* copy in variable bits */
	hnputs(h->tcplen, hdrlen + dlen);
	hnputl(h->tcpseq, tcph->seq);
	hnputl(h->tcpack, tcph->ack);
	hnputs(h->tcpflag, (hdrlen<<10) | tcph->flags);
	hnputs(h->tcpwin, tcph->wnd);
	hnputs(h->tcpurg, 0);

	if(tcph->flags & SYN){
		opt = h->tcpopt;
		if(tcph->mss != 0){
			*opt++ = MSSOPT;
			*opt++ = MSS_LENGTH;
			hnputs(opt, tcph->mss);
			opt += 2;
		}
		/* always offer.  rfc1323 §2.2 */
		if(1){
			*opt++ = WSOPT;
			*opt++ = WS_LENGTH;
			*opt++ = tcph->ws;
		}
		while(optpad-- > 0)
			*opt++ = NOOPOPT;
	}

	hnputs(h->tcpcksum, ptclcsum(data, TCP4_IPLEN, hdrlen+dlen+TCP4_PHDRSIZE));

	return data;
}

static int
ntohtcp6(Tcp *tcph, Block **bpp)
{
	Tcp6hdr *h;
	uchar *optr;
	ushort hdrlen;
	ushort optlen;
	int n;

	*bpp = pullupblock(*bpp, TCP6_PKT+TCP6_HDRSIZE);
	if(*bpp == nil)
		return -1;

	h = (Tcp6hdr *)((*bpp)->rp);
	tcph->source = nhgets(h->tcpsport);
	tcph->dest = nhgets(h->tcpdport);
	tcph->seq = nhgetl(h->tcpseq);
	tcph->ack = nhgetl(h->tcpack);
	hdrlen = (h->tcpflag[0]>>2) & ~3;
	if(hdrlen < TCP6_HDRSIZE) {
		freeblist(*bpp);
		*bpp = nil;
		return -1;
	}

	tcph->flags = h->tcpflag[1];
	tcph->wnd = nhgets(h->tcpwin);
	tcph->mss = 0;
	tcph->ws = 0;
	tcph->update = 0;
	tcph->len = nhgets(h->ploadlen) - hdrlen;

	*bpp = pullupblock(*bpp, hdrlen+TCP6_PKT);
	if(*bpp == nil)
		return -1;

	optr = h->tcpopt;
	n = hdrlen - TCP6_HDRSIZE;
	while(n > 0 && *optr != EOLOPT) {
		if(*optr == NOOPOPT) {
			n--;
			optr++;
			continue;
		}
		optlen = optr[1];
		if(optlen < 2 || optlen > n)
			break;
		switch(*optr) {
		case MSSOPT:
			if(optlen == MSS_LENGTH)
				tcph->mss = nhgets(optr+2);
			break;
		case WSOPT:
			if(optlen == WS_LENGTH)
				tcph->ws = optr[2];
			break;
		}
		n -= optlen;
		optr += optlen;
	}
	return hdrlen;
}

static int
ntohtcp4(Tcp *tcph, Block **bpp)
{
	Tcp4hdr *h;
	uchar *optr;
	ushort hdrlen;
	ushort optlen;
	int n;

	*bpp = pullupblock(*bpp, TCP4_PKT+TCP4_HDRSIZE);
	if(*bpp == nil)
		return -1;

	h = (Tcp4hdr *)((*bpp)->rp);
	tcph->source = nhgets(h->tcpsport);
	tcph->dest = nhgets(h->tcpdport);
	tcph->seq = nhgetl(h->tcpseq);
	tcph->ack = nhgetl(h->tcpack);

	hdrlen = (h->tcpflag[0]>>2) & ~3;
	if(hdrlen < TCP4_HDRSIZE) {
		freeblist(*bpp);
		*bpp = nil;
		return -1;
	}

	tcph->flags = h->tcpflag[1];
	tcph->wnd = nhgets(h->tcpwin);
	tcph->mss = 0;
	tcph->ws = 0;
	tcph->update = 0;
	tcph->len = nhgets(h->length) - (hdrlen + TCP4_PKT);

	*bpp = pullupblock(*bpp, hdrlen+TCP4_PKT);
	if(*bpp == nil)
		return -1;

	optr = h->tcpopt;
	n = hdrlen - TCP4_HDRSIZE;
	while(n > 0 && *optr != EOLOPT) {
		if(*optr == NOOPOPT) {
			n--;
			optr++;
			continue;
		}
		optlen = optr[1];
		if(optlen < 2 || optlen > n)
			break;
		switch(*optr) {
		case MSSOPT:
			if(optlen == MSS_LENGTH)
				tcph->mss = nhgets(optr+2);
			break;
		case WSOPT:
			if(optlen == WS_LENGTH)
				tcph->ws = optr[2];
			break;
		}
		n -= optlen;
		optr += optlen;
	}
	return hdrlen;
}

/*
 *  For outgoing calls, generate an initial sequence
 *  number and put a SYN on the send queue
 */
static void
tcpsndsyn(Conv *s)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;

	tcb->iss = (nrand(1<<16)<<16)|nrand(1<<16);
	tcb->snd.wl2 = tcb->iss;
	tcb->snd.una = tcb->iss;
	tcb->snd.rxt = tcb->iss;
	tcb->snd.ptr = tcb->iss;
	tcb->snd.nxt = tcb->iss;

	tcb->flags = (tcb->flags & ~SYNACK) | FORCE;
	tcb->flgcnt++;

	tcb->rttime = 0;	/* set in tcpoutput() */

	/* set desired mss and scale */
	tcb->mss = tcpmtu(v6lookup(s->p->f, s->raddr, s->laddr, s), &tcb->scale, s->ipversion);
}

static int
sndrst(Proto *tcp, uchar *source, uchar *dest, ushort length, Tcp *seg, int version, char *reason, Routehint *rh)
{
	Tcppriv *tpriv = (Tcppriv*)tcp->priv;
	Block *bp;
	union {
		Tcp4hdr ph4;
		Tcp6hdr ph6;
	} u;
	uchar rflags;

	netlog(tcp->f, Logtcp, "sndrst: %s\n", reason);

	if(seg->flags & RST)
		return -1;

	/* make pseudo header */
	switch(version) {
	case V4:
		memset(&u.ph4, 0, sizeof(u.ph4));
		u.ph4.vihl = IP_VER4;
		u.ph4.proto = IP_TCPPROTO;
		hnputs(u.ph4.tcplen, TCP4_HDRSIZE);
		hnputs(u.ph4.tcpsport, seg->dest);
		hnputs(u.ph4.tcpdport, seg->source);
		v6tov4(u.ph4.tcpsrc, dest);
		v6tov4(u.ph4.tcpdst, source);
		break;
	case V6:
		memset(&u.ph6, 0, sizeof(u.ph6));
		u.ph6.proto = IP_TCPPROTO;
		hnputs(u.ph6.ploadlen, TCP6_HDRSIZE);
		hnputs(u.ph6.tcpsport, seg->dest);
		hnputs(u.ph6.tcpdport, seg->source);
		ipmove(u.ph6.tcpsrc, dest);
		ipmove(u.ph6.tcpdst, source);
		break;
	default:
		panic("sndrst: version %d", version);
	}

	tpriv->stats[OutRsts]++;
	rflags = RST;

	/* convince the other end that this reset is in band */
	if(seg->flags & ACK) {
		seg->seq = seg->ack;
		seg->ack = 0;
	}
	else {
		rflags |= ACK;
		seg->ack = seg->seq;
		seg->seq = 0;
		if(seg->flags & SYN)
			seg->ack++;
		seg->ack += length;
		if(seg->flags & FIN)
			seg->ack++;
	}
	seg->flags = rflags;
	seg->wnd = 0;
	seg->mss = 0;
	seg->ws = 0;
	switch(version) {
	case V4:
		bp = htontcp4(seg, nil, &u.ph4);
		return ipoput4(tcp->f, bp, nil, MAXTTL, DFLTTOS, rh);
	case V6:
		bp = htontcp6(seg, nil, &u.ph6);
		return ipoput6(tcp->f, bp, nil, MAXTTL, DFLTTOS, rh);
	}
	return -1;
}

/*
 *  send a reset to the remote side and close the conversation
 *  called with s locked
 */
static char*
tcphangup(Conv *s)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;

	if(ipcmp(s->raddr, IPnoaddr) != 0 && tcb->state != Closed && tcb->bypass == nil) {
		Block *bp;
		Tcp seg;

		memset(&seg, 0, sizeof seg);
		seg.flags = RST | ACK;
		seg.seq = tcb->snd.ptr;
		seg.ack = tcb->rcv.ackptr = tcb->rcv.nxt;
		seg.wnd = 0;
		seg.mss = 0;
		seg.ws = 0;
		switch(s->ipversion) {
		case V4:
			bp = htontcp4(&seg, nil, &tcb->protohdr.tcp4hdr);
			qunlock(s);
			ipoput4(s->p->f, bp, nil, s->ttl, s->tos, s);
			break;
		case V6:
			bp = htontcp6(&seg, nil, &tcb->protohdr.tcp6hdr);
			qunlock(s);
			ipoput6(s->p->f, bp, nil, s->ttl, s->tos, s);
			break;
		default:
			panic("tcphangup: version %d", s->ipversion);
		}
		qlock(s);
	}
	localclose(s, nil);
	return nil;
}

/*
 *  (re)send a SYN ACK
 */
static int
sndsynack(Proto *tcp, Limbo *lp)
{
	union {
		Tcp4hdr ph4;
		Tcp6hdr ph6;
	} u;
	Tcp seg;
	Block *bp;
	Routehint rh;
	Route *rt;

	rh.r = nil;
	rh.a = nil;
	if((rt = v6lookup(tcp->f, lp->raddr, lp->laddr, &rh)) == nil)
		return -1;

	/* make pseudo header */
	switch(lp->version) {
	case V4:
		memset(&u.ph4, 0, sizeof(u.ph4));
		u.ph4.vihl = IP_VER4;
		u.ph4.proto = IP_TCPPROTO;
		hnputs(u.ph4.tcplen, TCP4_HDRSIZE);
		hnputs(u.ph4.tcpsport, lp->lport);
		hnputs(u.ph4.tcpdport, lp->rport);
		v6tov4(u.ph4.tcpsrc, lp->laddr);
		v6tov4(u.ph4.tcpdst, lp->raddr);
		break;
	case V6:
		memset(&u.ph6, 0, sizeof(u.ph6));
		u.ph6.proto = IP_TCPPROTO;
		hnputs(u.ph6.ploadlen, TCP6_HDRSIZE);
		hnputs(u.ph6.tcpsport, lp->lport);
		hnputs(u.ph6.tcpdport, lp->rport);
		ipmove(u.ph6.tcpsrc, lp->laddr);
		ipmove(u.ph6.tcpdst, lp->raddr);
		break;
	default:
		panic("sndsynack: version %d", lp->version);
	}

	memset(&seg, 0, sizeof seg);
	seg.seq = lp->iss;
	seg.ack = lp->irs+1;
	seg.flags = SYN|ACK;
	seg.mss = tcpmtu(rt, &seg.ws, lp->version);
	seg.wnd = QMAX;

	/* if the other did not set window scale, both should be zero */
	if(lp->ws == 0)
		seg.ws = 0;

	lp->rexmits++;
	lp->lastsend = NOW;

	switch(lp->version) {
	case V4:
		bp = htontcp4(&seg, nil, &u.ph4);
		return ipoput4(tcp->f, bp, nil, MAXTTL, DFLTTOS, &rh);
	case V6:
		bp = htontcp6(&seg, nil, &u.ph6);
		return ipoput6(tcp->f, bp, nil, MAXTTL, DFLTTOS, &rh);
	}
	return -1;
}

#define hashipa(a, p) ( ( (a)[IPaddrlen-2] + (a)[IPaddrlen-1] + p )&LHTMASK )

static Limbo**
limbohash(Tcppriv *tpriv, uchar *raddr, ushort rport)
{
	return &tpriv->lht[hashipa(raddr, rport)];
}

static Limbo**
limboent(Tcppriv *tpriv, uchar *raddr, ushort rport, uchar *laddr, ushort lport, int version)
{
	Limbo *lp, **l;

	for(l = limbohash(tpriv, raddr, rport); (lp = *l) != nil; l = &lp->next){
		if(lp->lport != lport || lp->rport != rport || lp->version != version)
			continue;
		if(ipcmp(lp->raddr, raddr) != 0)
			continue;
		if(ipcmp(lp->laddr, laddr) != 0)
			continue;
		break;
	}
	return l;
}

/*
 *  put a call into limbo and respond with a SYN ACK
 *
 *  called with proto locked
 */
static void
limbo(Conv *s, Tcp *seg, uchar *source, uchar *dest, int version)
{
	Tcppriv *tpriv = (Tcppriv*)s->p->priv;
	Limbo *lp, **l;

	l = limboent(tpriv, source, seg->source, dest, seg->dest, version);
	if((lp = *l) != nil){
		/* each new SYN restarts the retransmits */
		lp->irs = seg->seq;
	} else {
		Limbo **h;

		if(tpriv->nlimbo >= Maxlimbo
		/* reuse the oldest entry (head) of this hash bucket */
		&& (lp = *(h = limbohash(tpriv, source, seg->source))) != nil){
			if((*h = lp->next) == nil)
				l = h;
		} else {
			if((lp = malloc(sizeof(*lp))) == nil)
				return;
			tpriv->nlimbo++;
		}
		lp->next = nil;
		ipmove(lp->laddr, dest);
		ipmove(lp->raddr, source);
		lp->lport = seg->dest;
		lp->rport = seg->source;
		lp->mss = seg->mss;
		lp->ws = seg->ws;
		lp->irs = seg->seq;
		lp->iss = (nrand(1<<16)<<16)|nrand(1<<16);
		lp->rexmits = 0;
		lp->version = version;
		*l = lp;
	}
	if(sndsynack(s->p, lp) < 0){
		tpriv->nlimbo--;
		*l = lp->next;
		free(lp);
	}
}

/*
 *  resend SYN ACK's.
 */
static void
limborexmit(Proto *tcp)
{
	Tcppriv *tpriv;
	Limbo **l, *lp;
	ulong now;
	int h;

	if(!canqlock(tcp))
		return;

	now = NOW;
	tpriv = tcp->priv;
	for(h = 0; h < NLHT; h++){
		for(l = &tpriv->lht[h]; (lp = *l) != nil; ){
			if(now - lp->lastsend >= lp->rexmits*250){
				if(lp->rexmits > 4 || sndsynack(tcp, lp) < 0){
					tpriv->nlimbo--;
					*l = lp->next;
					free(lp);
					continue;
				}
			}
			l = &lp->next;
		}
	}
	qunlock(tcp);
}

/*
 *  lookup call in limbo.  if found, throw it out.
 *
 *  called with proto locked
 */
static void
limborst(Conv *s, Tcp *seg, uchar *source, uchar *dest, int version)
{
	Tcppriv *tpriv = (Tcppriv*)s->p->priv;
	Limbo *lp, **l;

	l = limboent(tpriv, source, seg->source, dest, seg->dest, version);
	if((lp = *l) == nil)
		return;
	/* RST can only follow the SYN */
	if(seg->seq == lp->irs+1){
		tpriv->nlimbo--;
		*l = lp->next;
		free(lp);
	}
}
/*
 *  use the time between the first SYN and it's ack as the
 *  initial round trip time
 */
static void
tcpsynackrtt(Tcpctl *tcb)
{
	int rtt = (int)(NOW - tcb->rttime);
	tcb->rttime = 0;
	tcb->srtt = rtt<<LOGAGAIN;
	tcb->mdev = rtt<<(LOGDGAIN-1);
}

static void
tcpbypass(void *arg, Block *b)
{
	Conv *c = (Conv*)arg;
	Tcpctl *tcb = (Tcpctl*)c->ptcl;
	Conv *o = tcb->bypass;

	if(o == nil || ((Tcpctl*)o->ptcl)->bypass != c){
		freeblist(b);
		return;
	}
	qbwrite(o->rq, b);
}

/*
 *  splice two local tcp conversations together,
 *  having the wq directly bypass into the other
 *  ends rq.
 *
 *  both new, old and proto are locked.
 */
static void
tcpsplice(Conv *new, Conv *old)
{
	Tcppriv *tpriv;
	Tcpctl *ntcb, *otcb;

	ntcb = (Tcpctl*)new->ptcl;
	otcb = (Tcpctl*)old->ptcl;

	if(otcb->state != Established)
		return;

	ntcb->bypass = old;
	otcb->bypass = new;

	qsetlimit(new->rq, conf.pipeqsize);
	qsetlimit(old->rq, conf.pipeqsize);
	qsetbypass(new->wq, tcpbypass);
	qsetbypass(old->wq, tcpbypass);

	/* stop timers and dump resequencing queue */
	tpriv = (Tcppriv*)new->p->priv;
	tcphalt(tpriv, &ntcb->timer);
	tcphalt(tpriv, &otcb->timer);
	tcphalt(tpriv, &ntcb->acktimer);
	tcphalt(tpriv, &otcb->acktimer);
	tcphalt(tpriv, &ntcb->katimer);
	tcphalt(tpriv, &otcb->katimer);
	dumpreseq(ntcb);
	dumpreseq(otcb);
}

/*
 *  come here when we finally get an ACK to our SYN-ACK.
 *  lookup call in limbo.  if found, create a new conversation
 *
 *  called with proto locked
 */
static Conv*
tcpincoming(Conv *s, Tcp *seg, uchar *src, uchar *dst, int version)
{
	Tcppriv *tpriv;
	Route *rp;
	Limbo *lp, **l;
	Conv *new;
	Tcpctl *tcb;
	Tcp4hdr *h4;
	Tcp6hdr *h6;

	/* unless it's just an ack, it can't be someone coming out of limbo */
	if((seg->flags & SYN) || (seg->flags & ACK) == 0)
		return nil;

	tpriv = (Tcppriv*)s->p->priv;
	l = limboent(tpriv, src, seg->source, dst, seg->dest, version);
	if((lp = *l) == nil)
		return nil;
	if(seg->seq != lp->irs+1 || seg->ack != lp->iss+1){
		netlog(s->p->f, Logtcp, "tcpincoming s %lux/%lux a %lux %lux\n",
			seg->seq, lp->irs+1, seg->ack, lp->iss+1);
		return nil;
	}
	tpriv->nlimbo--;
	*l = lp->next;

	new = Fsnewcall(s, src, seg->source, dst, seg->dest, version);
	if(new == nil){
		free(lp);
		return nil;
	}

	memmove(new->ptcl, s->ptcl, sizeof(Tcpctl));
	tcb = (Tcpctl*)new->ptcl;
	tcb->timer.arg = new;
	tcb->timer.state = TcptimerOFF;
	tcb->acktimer.arg = new;
	tcb->acktimer.state = TcptimerOFF;
	tcb->katimer.arg = new;
	tcb->katimer.state = TcptimerOFF;

	tcb->rcv.nxt = tcb->rcv.ackptr = seg->seq;
	tcb->rcv.wptr = tcb->rcv.wsnt = tcb->rcv.nxt;
	tcb->rcv.wnd = 0;

	tcb->iss = lp->iss;

	tcb->snd.una = seg->ack;
	tcb->snd.ptr = seg->ack;
	tcb->snd.nxt = seg->ack;
	tcb->snd.rxt = seg->ack;

	tcb->flgcnt = 0;
	tcb->flags |= SYNACK;

	tcb->rttime = lp->lastsend;
	tcpsynackrtt(tcb);

	/* the same as what we sent in SYN,ACK */
	rp = v6lookup(s->p->f, src, dst, new);
	tcb->mss = tcpmtu(rp, &tcb->scale, version);

	tcpsetmss(new, lp->mss);
	tcpsetscale(new, lp->ws);

	free(lp);

	tcb->snd.wnd = seg->wnd << tcb->snd.scale;
	tcb->snd.wl2 = seg->ack;
	tcb->snd.wl1 = seg->seq;

	/* set up proto header */
	switch(version){
	case V4:
		h4 = &tcb->protohdr.tcp4hdr;
		memset(h4, 0, sizeof(*h4));
		h4->vihl = IP_VER4;
		h4->proto = IP_TCPPROTO;
		hnputs(h4->tcpsport, new->lport);
		hnputs(h4->tcpdport, new->rport);
		v6tov4(h4->tcpsrc, dst);
		v6tov4(h4->tcpdst, src);
		break;
	case V6:
		h6 = &tcb->protohdr.tcp6hdr;
		memset(h6, 0, sizeof(*h6));
		h6->proto = IP_TCPPROTO;
		hnputs(h6->tcpsport, new->lport);
		hnputs(h6->tcpdport, new->rport);
		ipmove(h6->tcpsrc, dst);
		ipmove(h6->tcpdst, src);
		break;
	default:
		panic("tcpincoming: version %d", new->ipversion);
	}

	qlock(new);
	tcpsetstate(new, Established);
	iphtadd(&tpriv->ht, new);
	if(rp != nil && (rp->type & Runi) != 0){
		/* see if we can find the local conversation and splice them together */
		Iphash *iph = iphtlook(&tpriv->ht, new->laddr, new->lport, new->raddr, new->rport);
		if(iph != nil && iph->trans == 0){
			Conv *old = iphconv(iph);
			if(old != new){
				qlock(old);
				tcpsplice(new, old);
				qunlock(old);
			}
		}
	}
	qunlock(new);

	return new;
}

static void
update(Conv *s, Tcp *seg)
{
	Tcpctl *tcb;
	Tcppriv *tpriv;
	int rtt, delta, acked;

	if(seg->update)
		return;
	seg->update = 1;
	if((seg->flags & ACK) == 0)
		return;

	tcb = (Tcpctl*)s->ptcl;
	tpriv = (Tcppriv*)s->p->priv;

	/* ghost acks should be ignored */
	if(seq_gt(seg->ack, tcb->snd.nxt))
		return;

	/*
	 *  update window
	 */
	if(seq_gt(seg->seq, tcb->snd.wl1)
	|| seg->seq == tcb->snd.wl1
		&& (seq_gt(seg->ack, tcb->snd.wl2)
		|| seg->ack == tcb->snd.wl2 && seg->wnd > tcb->snd.wnd)){
		/* clear dupack if we advance wl2 */
		if(tcb->snd.wl2 != seg->ack)
			tcb->snd.dupacks = 0;
		tcb->snd.wl2 = seg->ack;
		tcb->snd.wl1 = seg->seq;
		if(tcb->snd.wnd == 0 && seg->wnd > 0){
			tcb->snd.wnd = seg->wnd;
			goto recovery;
		}
		tcb->snd.wnd = seg->wnd;
	}

	/* newreno fast retransmit */
	if(tcb->snd.una != tcb->snd.nxt)
	if(seg->ack == tcb->snd.una)
	if(seg->len == 0 && (seg->flags & (SYN|FIN)) == 0)
	if(++tcb->snd.dupacks == 3){
recovery:
		if(tcb->snd.recovery){
			tpriv->stats[RecoveryCwind]++;
			tcb->cwind += tcb->mss;
		}else if(tcb->snd.wnd == 0){
			netlog(s->p->f, Logtcpwin, "!recov %lud %lud window shut\n",
				tcb->snd.rxt, seg->ack);
			/* force window probe */
			tcprxmit(s);
		}else if(seq_ge(seg->ack, tcb->snd.rxt)){
			tpriv->stats[Recovery]++;
			tcb->snd.recovery = 1;
			tcb->snd.partialack = 0;
			tcb->snd.rxt = tcb->snd.nxt;
			tcpcongestion(tcb);
			tcb->abcbytes = 0;
			tcb->cwind = tcb->ssthresh + 3*tcb->mss;
			netlog(s->p->f, Logtcpwin, "recovery inflate %ld ss %ld @%lud\n",
				tcb->cwind, tcb->ssthresh, tcb->snd.rxt);
			/* initial fast-retransmit, preserve send pointer */
			tcb->snd.ptr = tcprxmit(s);
		}else{
			tpriv->stats[RecoveryNoSeq]++;
			netlog(s->p->f, Logtcpwin, "!recov %lud not ≤ %lud %ld\n",
				tcb->snd.rxt, seg->ack, tcb->snd.rxt - seg->ack);
			/* do not enter fast retransmit */
			/* do not change ssthresh */
		}
	}else if(tcb->snd.recovery){
		tpriv->stats[RecoveryCwind]++;
		tcb->cwind += tcb->mss;
	}

	/* Compute the new send window size */
	acked = (int)(seg->ack - tcb->snd.una);
	if(acked <= 0){
		/*
		 *  don't let us hangup if sending into a closed window and
		 *  we're still getting acks
		 */
		if(tcb->snd.wnd == 0)
			tcb->backedoff = MAXBACKMS/4;
		return;
	}

	/* RTT measurement */
	if(tcb->rttime && seq_ge(seg->ack, tcb->rttseq)) {
		rtt = (int)(NOW - tcb->rttime);
		tcb->rttime = 0;

		delta = rtt - (tcb->srtt>>LOGAGAIN);
		tcb->srtt += delta;
		if(delta < 0) delta = -delta;
		tcb->mdev += delta - (tcb->mdev>>LOGDGAIN);
	}

	/*
	 *  update queue
	 */
	if((tcb->flags & SYNACK) == 0) {
		tcb->flags |= SYNACK;
		tcb->flgcnt--;
		acked--;
	}
	if(qdiscard(s->wq, acked) < acked)
		tcb->flgcnt--;
	tcb->snd.una = seg->ack;
	if(seq_gt(seg->ack, tcb->snd.ptr))
		tcb->snd.ptr = seg->ack;

	/*
	 *  congestion control
	 */
	if(tcb->snd.recovery){
		if(seq_ge(seg->ack, tcb->snd.rxt) || tcb->snd.wnd == 0){
			/* recovery finished; deflate window */
			tpriv->stats[RecoveryDone]++;
			tcb->snd.dupacks = 0;
			tcb->snd.recovery = 0;

			/* RFC 6582 2.3 (3) min(ssthresh, max(FlightSize, SMSS) + SMSS) */
			tcb->cwind = tcb->snd.nxt - tcb->snd.una;
			if(tcb->cwind < tcb->mss)
				tcb->cwind = tcb->mss;
			tcb->cwind += tcb->mss;
			if(tcb->cwind > tcb->ssthresh)
				tcb->cwind = tcb->ssthresh;
			netlog(s->p->f, Logtcpwin, "recovery deflate %ld %ld\n",
				tcb->cwind, tcb->ssthresh);
		} else {
			/* partial ack; we lost more than one segment */
			tpriv->stats[RecoveryPA]++;
			if(tcb->cwind > acked)
				tcb->cwind -= acked;
			else{
				netlog(s->p->f, Logtcpwin, "partial ack neg\n");
				tcb->cwind = tcb->mss;
			}
			if(acked >= tcb->mss)
				tcb->cwind += tcb->mss;
			tcb->snd.partialack++;
			netlog(s->p->f, Logtcpwin, "partial ack %d left %ld cwind %ld\n",
				acked, tcb->snd.rxt - seg->ack, tcb->cwind);
			/* retransmit, preserve send pointer */
			tcb->snd.ptr = tcprxmit(s);
		}
	} else {
		tcpabcincr(tcb, acked);
	}

	tcb->backoff = 0;
	tcb->backedoff = 0;

	if(tcb->snd.una != tcb->snd.nxt){
		/* “impatient” variant */
		if(!tcb->snd.recovery || tcb->snd.partialack == 1)
			tcpsettimer(s);
	}
	else
		tcphalt(tpriv, &tcb->timer);
}

static void
tcpiput(Proto *tcp, Ipifc *ifc, Block *bp)
{
	Tcp seg;
	Tcp4hdr *h4;
	Tcp6hdr *h6;
	Tcpctl *tcb;
	int hdrlen;
	ushort length;
	uchar source[IPaddrlen], dest[IPaddrlen];
	Iphash *iph;
	Conv *s;
	Fs *f;
	Tcppriv *tpriv;
	char *reason;
	int version;

	f = tcp->f;
	tpriv = (Tcppriv*)tcp->priv;
	tpriv->stats[InSegs]++;

	h4 = (Tcp4hdr*)(bp->rp);
	h6 = (Tcp6hdr*)(bp->rp);

	if((h4->vihl&0xF0)==IP_VER4) {
		int ttl = h4->ttl;

		version = V4;
		length = nhgets(h4->length);
		if(length < TCP4_PKT){
			tpriv->stats[HlenErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp, "bad tcp len\n");
			freeblist(bp);
			return;
		}
		length -= TCP4_PKT;
		v4tov6(dest, h4->tcpdst);
		v4tov6(source, h4->tcpsrc);

		h4->ttl = 0;
		hnputs(h4->tcplen, length);
		if(!(bp->flag & Btcpck) && (h4->tcpcksum[0] || h4->tcpcksum[1])
		&& ptclcsum(bp, TCP4_IPLEN, length + TCP4_PKT - TCP4_IPLEN)) {
			tpriv->stats[CsumErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp, "bad tcp proto cksum\n");
			freeblist(bp);
			return;
		}
		h4->ttl = ttl;

		hdrlen = ntohtcp4(&seg, &bp);
		if(hdrlen < 0){
			tpriv->stats[HlenErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp, "bad tcp hdr len\n");
			return;
		}
		length -= hdrlen;
		hdrlen += TCP4_PKT;
	}
	else {
		int ttl = h6->ttl;
		int proto = h6->proto;

		version = V6;
		length = nhgets(h6->ploadlen);
		ipmove(dest, h6->tcpdst);
		ipmove(source, h6->tcpsrc);

		h6->ploadlen[0] = h6->ploadlen[1] = h6->proto = 0;
		h6->ttl = proto;
		hnputl(h6->vcf, length);
		if((h6->tcpcksum[0] || h6->tcpcksum[1])
		&& ptclcsum(bp, TCP6_IPLEN, length+TCP6_PHDRSIZE) != 0) {
			tpriv->stats[CsumErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp,
			    "bad tcpv6 proto cksum: got %#ux\n",
				h6->tcpcksum[0]<<8 | h6->tcpcksum[1]);
			freeblist(bp);
			return;
		}
		h6->ttl = ttl;
		h6->proto = proto;
		hnputs(h6->ploadlen, length);

		hdrlen = ntohtcp6(&seg, &bp);
		if(hdrlen < 0){
			tpriv->stats[HlenErrs]++;
			tpriv->stats[InErrs]++;
			netlog(f, Logtcp, "bad tcpv6 hdr len\n");
			return;
		}
		length -= hdrlen;
		hdrlen += TCP6_PKT;
	}

	/* lock protocol while searching for a conversation */
	qlock(tcp);

	/* Look for a matching conversation */
	iph = iphtlook(&tpriv->ht, source, seg.source, dest, seg.dest);
	if(iph == nil){
		netlog(f, Logtcp, "iphtlook(src %I!%d, dst %I!%d) failed\n",
			source, seg.source, dest, seg.dest);
reset:
		qunlock(tcp);
		freeblist(bp);
		sndrst(tcp, source, dest, length, &seg, version, "no conversation", nil);
		return;
	}
	if(iph->trans){
		Translation *q;
		int hop = h4->ttl;

		if(hop <= 1 || (q = transbackward(tcp, iph)) == nil)
			goto reset;
		hnputs_csum(h4->tcpdst+0, nhgets(q->forward.raddr+IPv4off+0), h4->tcpcksum);
		hnputs_csum(h4->tcpdst+2, nhgets(q->forward.raddr+IPv4off+2), h4->tcpcksum);
		hnputs_csum(h4->tcpdport, q->forward.rport, h4->tcpcksum);
		qunlock(tcp);
		ipoput4(f, bp, ifc, hop - 1, h4->tos, q);
		return;
	}
	s = iphconv(iph);

	/* trim off ip and tcp headers */
	bp = trimblock(bp, hdrlen, length);
	if(bp == nil){
		tpriv->stats[LenErrs]++;
		tpriv->stats[InErrs]++;
		netlog(f, Logtcp, "tcp bad length after header trim off\n");
		qunlock(tcp);
		return;
	}

	/* if it's a listener, look for the right flags and get a new conv */
	tcb = (Tcpctl*)s->ptcl;
	if(tcb->state == Listen){
		if(seg.flags & RST){
			limborst(s, &seg, source, dest, version);
			qunlock(tcp);
			freeblist(bp);
			return;
		}

		/* if this is a new SYN, put the call into limbo */
		if((seg.flags & SYN) && (seg.flags & ACK) == 0){
			limbo(s, &seg, source, dest, version);
			qunlock(tcp);
			freeblist(bp);
			return;
		}

		/*
		 *  if there's a matching call in limbo, tcpincoming will
		 *  return it in state Established
		 */
		s = tcpincoming(s, &seg, source, dest, version);
		if(s == nil)
			goto reset;
	}

	/* The rest of the input state machine is run with the control block
	 * locked and implements the state machine directly out of the RFC.
	 * Out-of-band data is ignored - it was always a bad idea.
	 */
	tcb = (Tcpctl*)s->ptcl;
	qlock(s);
	qunlock(tcp);

	/* fix up window */
	seg.wnd <<= tcb->snd.scale;

	/* every input packet in puts off the keep alive time out */
	tcb->kato = 0;

	switch(tcb->state) {
	case Closed:
	closed:
		reason = "sending to Closed";
	reset2:
		qunlock(s);
		freeblist(bp);
		sndrst(tcp, source, dest, length, &seg, version, reason, s);
		return;
	case Syn_sent:
		if(seg.flags & ACK) {
			if(!seq_in(seg.ack, tcb->snd.una, tcb->snd.nxt+1)) {
				if(seg.flags & RST)
					goto raise;
				reason = "bad seq in Syn_sent";
				goto reset2;
			}
		}
		if(seg.flags & RST) {
			if(seg.flags & ACK)
				localclose(s, Econrefused);
			goto raise;
		}
		if(seg.flags & SYN) {
			procsyn(s, &seg);
			if(seg.flags & ACK){
				tcb->snd.wnd = seg.wnd;	/* window in SYN,ACK must not be scaled */
				tcb->snd.wl2 = seg.ack;
				tcb->snd.wl1 = seg.seq;
				tcpsynackrtt(tcb);
				update(s, &seg);
				tcpsetstate(s, Established);
			}
			else {
				tcb->time = NOW;
				tcpsetstate(s, Syn_received);	/* DLP - shouldn't this be a reset? */
			}
			if(length != 0 || (seg.flags & FIN))
				break;
			freeblist(bp);
			goto output;
		}
		goto raise;
	case Syn_received:
		if(seg.flags & ACK)
			tcpsynackrtt(tcb);
		break;
	}

	/* Cut the data to fit the receive window */
	tcprcvwin(s);
	if(tcptrim(tcb, &seg, &bp, &length) < 0) {
		netlog(f, Logtcp, "tcp: trim: !inwind: seq %lud-%lud (%d) win %lud-%lud (%lud) from %I!%d -> %I!%d\n", 
			seg.seq, seg.seq+length, length,
			tcb->rcv.nxt, tcb->rcv.wptr, tcb->rcv.wnd,
			s->raddr, s->rport, s->laddr, s->lport);
		tcb->flags |= FORCE;
		update(s, &seg);
		if(tcb->state == Closing)
		if(qlen(s->wq)+tcb->flgcnt == 0) {
			tcphalt(tpriv, &tcb->acktimer);
			tcphalt(tpriv, &tcb->katimer);
			tcpsetstate(s, Time_wait);
			tcb->timer.start = MSL2*(1000 / MSPTICK);
			tcpgo(tpriv, &tcb->timer);
		}
		if(seg.flags & RST)
			goto raise;
		goto output;
	}

	/* Cannot accept so answer with a rst */
	if(length && tcb->state == Closed)
		goto closed;

	/* The segment is beyond the current receive pointer so
	 * queue the data in the resequence queue
	 */
	if(seg.seq != tcb->rcv.nxt)
	if(length != 0 || (seg.flags & (SYN|FIN))) {
		/*
		 *  force duplicate ack; RFC 5681 §3.2
		 *
		 *  FORCE must be set before update()
		 *  which can send fast-retransmits
		 *  (clearing FORCE flag) as we must
		 *  only produce one output segment
		 *  per input segment.
		 */
		tcb->flags |= FORCE;
		update(s, &seg);
		if(addreseq(f, tcb, tpriv, &seg, &bp, length) < 0)
			print("tcp: reseq: %I!%d -> %I!%d\n", s->raddr, s->rport, s->laddr, s->lport);
		goto output;
	}

	/*
	 *  keep looping till we've processed this packet plus any
	 *  adjacent packets in the resequence queue
	 */
	for(;;) {
		if(seg.flags & RST) {
			if(tcb->state == Established) {
				tpriv->stats[EstabResets]++;
				if(tcb->rcv.nxt != seg.seq)
					print("out of order RST rcvd: %I.%d -> %I.%d, rcv.nxt %lux seq %lux\n",
						s->raddr, s->rport, s->laddr, s->lport, tcb->rcv.nxt, seg.seq);
			}
			localclose(s, Econrefused);
			goto raise;
		}

		if((seg.flags&ACK) == 0)
			goto raise;

		switch(tcb->state) {
		case Syn_received:
			if(!seq_in(seg.ack, tcb->snd.una, tcb->snd.nxt+1)){
				reason = "bad seq in Syn_received";
				goto reset2;
			}
			tcb->snd.wnd = seg.wnd;	/* already scaled */
			tcb->snd.wl2 = seg.ack;
			tcb->snd.wl1 = seg.seq;
			update(s, &seg);
			tcpsetstate(s, Established);
			break;
		case Established:
			if(tcb->bypass != nil)
				goto raise;
			/* wet floor */
		case Close_wait:
		case Finwait2:
			update(s, &seg);
			break;
		case Finwait1:
			update(s, &seg);
			if(qlen(s->wq)+tcb->flgcnt == 0){
				tcphalt(tpriv, &tcb->acktimer);
				tcphalt(tpriv, &tcb->katimer);
				tcb->time = NOW;
				tcpsetstate(s, Finwait2);
				tcb->katimer.start = MSL2*(1000 / MSPTICK);
				tcpgo(tpriv, &tcb->katimer);
			}
			break;
		case Closing:
			update(s, &seg);
			if(qlen(s->wq)+tcb->flgcnt == 0) {
				tcphalt(tpriv, &tcb->acktimer);
				tcphalt(tpriv, &tcb->katimer);
				tcpsetstate(s, Time_wait);
				tcb->timer.start = MSL2*(1000 / MSPTICK);
				tcpgo(tpriv, &tcb->timer);
			}
			break;
		case Last_ack:
			update(s, &seg);
			if(qlen(s->wq)+tcb->flgcnt == 0) {
				localclose(s, nil);
				goto raise;
			}
			/* wet floor */
		case Time_wait:
			if(seg.flags & FIN)
				tcb->flags |= FORCE;
			if(tcb->timer.state != TcptimerON)
				tcpsettimer(s);
		}

		if(length == 0) {
			freeblist(bp);
			bp = nil;
		}
		else {
			switch(tcb->state){
			default:
				/* Ignore segment text */
				freeblist(bp);
				bp = nil;
				break;

			case Syn_received:
			case Established:
			case Finwait1:
				/* If we still have some data place on
				 * receive queue
				 */
				if(bp != nil) {
					qpassnolim(s->rq, packblock(bp));
					bp = nil;
				}
				tcb->rcv.nxt += length;
				break;
			case Finwait2:
				reason = "send to Finwait2";
				goto reset2;
			}
		}

		if(seg.flags & FIN) {
			tcb->flags |= FORCE;

			switch(tcb->state) {
			case Syn_received:
			case Established:
				tcb->rcv.nxt++;
				tcpsetstate(s, Close_wait);
				break;
			case Finwait1:
				tcb->rcv.nxt++;
				if(qlen(s->wq)+tcb->flgcnt == 0) {
					tcphalt(tpriv, &tcb->acktimer);
					tcphalt(tpriv, &tcb->katimer);
					tcpsetstate(s, Time_wait);
					tcb->timer.start = MSL2*(1000/MSPTICK);
					tcpgo(tpriv, &tcb->timer);
				}
				else
					tcpsetstate(s, Closing);
				break;
			case Finwait2:
				tcb->rcv.nxt++;
				tcphalt(tpriv, &tcb->acktimer);
				tcphalt(tpriv, &tcb->katimer);
				tcpsetstate(s, Time_wait);
				tcb->timer.start = MSL2*(1000/MSPTICK);
				tcpgo(tpriv, &tcb->timer);
				break;
			case Close_wait:
			case Closing:
			case Last_ack:
				break;
			case Time_wait:
				tcpsettimer(s);
				break;
			}
		}

		/*
		 *  get next adjacent segment from the resequence queue.
		 *  dump/trim any overlapping segments
		 */
		for(;;) {
			if(getreseq(tcb, &seg, &bp, &length) < 0)
				goto output;
			tcprcvwin(s);
			if(tcptrim(tcb, &seg, &bp, &length) == 0){
				/* produce an ack when hole filled */
				tcb->flags |= FORCE;
				break;
			}
		}
	}
output:
	tcpoutput(s);

	/*
	 *  turn on the acktimer if there's something
	 *  to ack and we delayed ack in tcpoutput().
	 */
	if(tcb->rcv.ackptr != tcb->rcv.nxt)
	if(tcb->acktimer.state != TcptimerON)
		tcpgo(tpriv, &tcb->acktimer);

	qunlock(s);
	return;
raise:
	qunlock(s);
	freeblist(bp);
}

/*
 *  always enters and exits with the s locked.  We drop
 *  the lock to ipoput the packet so some care has to be
 *  taken by callers.
 */
static void
tcpoutput(Conv *s)
{
	Fs *f = s->p->f;
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	Tcppriv *tpriv = (Tcppriv*)s->p->priv;
	int ssize, sndcnt, sent;
	Block *bp;
	Tcp seg;

	/* force ack every second mss */
	if((tcb->flags & FORCE) == 0)
	if(tcb->rcv.nxt - tcb->rcv.ackptr > tcb->mss){
		tpriv->stats[Delayack]++;
		tcb->flags |= FORCE;
	}

	for(;;) {
		switch(tcb->state) {
		case Listen:
		case Closed:
		case Finwait2:
			return;
		}

		/* Don't send anything else until our SYN has been acked */
		if((tcb->flags & SYNACK) == 0 && tcb->snd.ptr != tcb->iss)
			return;

		tcprcvwin(s);

		/* force an ack when a window has opened up */
		if((tcb->flags & FORCE) == 0)
		if(seq_in(tcb->rcv.nxt, tcb->rcv.wsnt, tcb->rcv.wptr)) {
			tpriv->stats[Wopenack]++;
			tcb->flags |= FORCE;
		}

		/* figure out yow much to send */
		ssize = sndcnt = qlen(s->wq)+tcb->flgcnt;
		if(tcb->snd.wnd == 0){
			/* zero window probe */
			seg.seq = tcb->snd.una;
			sent = tcb->snd.ptr - seg.seq;
			if(sent > 0)
				ssize = 0;
			else if(ssize > 0)
				ssize = 1;
		} else {
			/* calculate usable segment size */
			if(ssize > tcb->snd.wnd)
				ssize = tcb->snd.wnd;
			if(ssize > tcb->cwind)
				ssize = tcb->cwind;
			seg.seq = tcb->snd.ptr;
			sent = seg.seq - tcb->snd.una;
			ssize -= sent;
			if(ssize < 0)
				ssize = 0;
			else if(ssize > tcb->mss)
				ssize = tcb->mss;
		}

		if((tcb->flags & FORCE) == 0){
			if(ssize == 0)
				return;
			if(ssize < tcb->mss)
			if(seg.seq == tcb->snd.nxt)
			if(sent > TCPREXMTTHRESH*tcb->mss)
				return;
		}
		tcb->flags &= ~FORCE;

		/* By default we will generate an ack */
		seg.source = s->lport;
		seg.dest = s->rport;
		seg.flags = ACK;
		seg.mss = 0;
		seg.ws = 0;
		seg.update = 0;

		seg.ack = tcb->rcv.ackptr = tcb->rcv.nxt;
		seg.wnd = tcb->rcv.wnd >> tcb->rcv.scale;
		tcb->rcv.wsnt = tcb->rcv.wptr;

		bp = nil;
		if(ssize > 0){
			switch(tcb->state){
			case Syn_sent:
				seg.flags = 0;
				/* wet floor */
			case Syn_received:
				/*
				 *  don't send any data with a SYN packet
				 *  because Linux rejects the packet in its
				 *  attempt to solve the SYN attack problem
				 */
				seg.flags |= SYN;
				seg.mss = tcb->mss;
				seg.ws = tcb->scale;
				ssize = 1;
				break;
			default:
				/* Pull out data to send */
				bp = qcopy(s->wq, ssize, sent);
				if(BLEN(bp) != ssize)
					seg.flags |= FIN;
				else if(sent+ssize == sndcnt)
					seg.flags |= PSH;
				break;
			}
	
			/* Pull up the send pointer so we can accept acks
			 * for this window
			 */
			tcb->snd.ptr += ssize;
			if(seq_gt(tcb->snd.ptr, tcb->snd.nxt)){
				tcb->snd.nxt = tcb->snd.ptr;

				/* start RTT measurement */
				if(tcb->rttime == 0) {
					tcb->rttime = NOW;
					tcb->rttseq = tcb->snd.ptr;
				}
			}

			/* Start the transmission timers if there is new data and we
			 * expect acknowledges
			 */
			if(tcb->timer.state != TcptimerON)
				tcpsettimer(s);
		}

		if(tcb->acktimer.state == TcptimerON)
			tcphalt(tpriv, &tcb->acktimer);

		tpriv->stats[OutSegs]++;
		if(tcb->snd.ptr != tcb->snd.nxt)
			tpriv->stats[RetransSegsSent]++;

		switch(s->ipversion){
		case V4:
			bp = htontcp4(&seg, bp, &tcb->protohdr.tcp4hdr);
			qunlock(s);
			sent = ipoput4(f, bp, nil, s->ttl, s->tos, s);
			break;
		case V6:
			bp = htontcp6(&seg, bp, &tcb->protohdr.tcp6hdr);
			qunlock(s);
			sent = ipoput6(f, bp, nil, s->ttl, s->tos, s);
			break;
		default:
			panic("tcpoutput: version %d", s->ipversion);
		}
		if(sent < 0){
			qlock(s);
			localclose(s, Enoroute);
			return;
		}
		qlock(s);
	}
}

/*
 *  the BSD convention (hack?) for keep alives.  resend last uchar acked.
 */
static int
tcpsendka(Conv *s)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	Block *bp;
	Tcp seg;
	int ret;

	memset(&seg, 0, sizeof seg);
	seg.source = s->lport;
	seg.dest = s->rport;
	seg.flags = ACK|PSH;
	seg.mss = 0;
	seg.ws = 0;

	tcprcvwin(s);
	seg.seq = tcb->snd.una-1;
	seg.ack = tcb->rcv.ackptr = tcb->rcv.nxt;
	seg.wnd = tcb->rcv.wnd >> tcb->rcv.scale;
	tcb->rcv.wsnt = tcb->rcv.wptr;

	if(tcb->state == Finwait2){
		seg.flags |= FIN;
		bp = nil;
	} else {
		bp = allocb(1);
		bp->wp++;
	}
	switch(s->ipversion){
	case V4:
		bp = htontcp4(&seg, bp, &tcb->protohdr.tcp4hdr);
		qunlock(s);
		ret = ipoput4(s->p->f, bp, nil, s->ttl, s->tos, s);
		break;
	case V6:
		bp = htontcp6(&seg, bp, &tcb->protohdr.tcp6hdr);
		qunlock(s);
		ret = ipoput6(s->p->f, bp, nil, s->ttl, s->tos, s);
		break;
	default:
		panic("tcpsendka: version %d", s->ipversion);
	}
	qlock(s);
	return ret;
}

/*
 *  if we've timed out, close the connection
 *  otherwise, send a keepalive and restart the timer
 */
static void
tcpkeepalive(void *arg)
{
	Conv *s = (Conv*)arg;
	Tcpctl *tcb = (Tcpctl*)s->ptcl;

	qlock(s);
	if(tcb->state != Closed && tcb->bypass == nil){
		if(++(tcb->kato) > MAX_KAT) {
			localclose(s, Etimedout);
		} else if(tcpsendka(s) < 0) {
			localclose(s, Enoroute);
		} else {
			tcpgo(s->p->priv, &tcb->katimer);
		}
	}
	qunlock(s);
}

/*
 *  start keepalive timer
 */
static char*
tcpstartka(Conv *s, char **f, int n)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	int x;

	if(tcb->state != Established)
		return "connection must be in Establised state";

	if(tcb->bypass != nil)
		return nil;

	x = DEF_KAT;
	if(n > 1){
		x = atoi(f[1]);
		if(x <= 0)
			x = 0;
	}
	tcphalt(s->p->priv, &tcb->katimer);
	tcb->kato = 0;
	tcb->katimer.start = (x + MSPTICK-1) / MSPTICK;
	tcpgo(s->p->priv, &tcb->katimer);

	return nil;
}

/*
 *  retransmit (at most) one segment at snd.una.
 *  preserve cwind and return original snd.ptr
 */
static ulong
tcprxmit(Conv *s)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	Tcppriv *tpriv;
	ulong tcwind, tptr;

	tcb->flags |= FORCE;

	tptr = tcb->snd.ptr;
	tcb->snd.ptr = tcb->snd.una;

	tcwind = tcb->cwind;
	tcb->cwind = tcb->mss;

	tcpoutput(s);

	tcb->cwind = tcwind;

	tpriv = (Tcppriv*)s->p->priv;
	tpriv->stats[RetransSegs]++;

	/* in case tcpoutput() released the lock */
	if(seq_lt(tptr, tcb->snd.una))
		tptr = tcb->snd.una;

	return tptr;
}

/*
 *  todo: RFC 4138 F-RTO
 */
static void
tcptimeout(void *arg)
{
	Conv *s = (Conv*)arg;
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	Tcppriv *tpriv = (Tcppriv*)s->p->priv;
	int maxback;

	qlock(s);
	switch(tcb->state){
	default:
		if(tcb->backoff < 8)
			tcb->backoff++;
		if(tcb->state == Syn_sent)
			maxback = MAXBACKMS/2;
		else
			maxback = MAXBACKMS;
		tcb->backedoff += tcb->timer.start * MSPTICK;
		if(tcb->backedoff >= maxback) {
			localclose(s, Etimedout);
			break;
		}
		netlog(s->p->f, Logtcprxmt, "rxm %d/%d %ldms %lud rto %d %lud %s\n",
			tcb->srtt, tcb->mdev, NOW-tcb->time,
			tcb->snd.una-tcb->timeuna, tcb->snd.rto, tcb->snd.ptr,
			tcpstates[s->state]);
		tpriv->stats[RetransTimeouts]++;
		if(tcb->snd.rto++ == 0)
			tcpcongestion(tcb);
		tcb->abcbytes = 0;
		tcb->cwind = tcb->mss;
		if(tcb->snd.recovery){
			tcb->snd.recovery = 0;
			tcb->snd.dupacks = 0;			/* reno rto */
			tcb->snd.rxt = tcb->snd.nxt;
			netlog(s->p->f, Logtcpwin,
				"rto recovery rxt @%lud\n", tcb->snd.nxt);
			tpriv->stats[RecoveryRTO]++;
		}
		/* resets the send pointer, retransmits and restarts timer */
		tcprxmit(s);
		break;
	case Time_wait:
		localclose(s, nil);
		break;
	case Closed:
		break;
	}
	qunlock(s);
}

/*
 *  set up state for a received SYN (or SYN ACK) packet
 */
static void
procsyn(Conv *s, Tcp *seg)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;

	tcb->flags |= FORCE;

	tcb->rcv.nxt = tcb->rcv.ackptr = seg->seq + 1;
	tcb->rcv.wptr = tcb->rcv.wsnt = tcb->rcv.nxt;
	tcb->rcv.wnd = 0;

	tcpsetmss(s, seg->mss);
	tcpsetscale(s, seg->ws);
}

static int
dumpreseq(Tcpctl *tcb)
{
	Reseq *r, *next;

	for(r = tcb->reseq; r != nil; r = next){
		next = r->next;
		freeblist(r->bp);
		free(r);
	}
	tcb->reseq = nil;
	tcb->nreseq = 0;
	tcb->reseqlen = 0;
	return -1;
}

static void
logreseq(Fs *f, Reseq *r, ulong n)
{
	char *s;

	for(; r != nil; r = r->next){
		s = nil;
		if(r->next == nil && r->seg.seq != n)
			s = "hole/end";
		else if(r->next == nil)
			s = "end";
		else if(r->seg.seq != n)
			s = "hole";
		if(s != nil)
			netlog(f, Logtcp, "%s %lud-%lud (%ld) %#ux\n", s,
				n, r->seg.seq, r->seg.seq-n, r->seg.flags);
		n = r->seg.seq + r->seg.len;
	}
}

static int
addreseq(Fs *f, Tcpctl *tcb, Tcppriv *tpriv, Tcp *seg, Block **bpp, ushort length)
{
	Reseq *rp, **rr;
	int qmax;

	rp = malloc(sizeof(Reseq));
	if(rp == nil){
		freeblist(*bpp);	/* *bpp always consumed by addreseq */
		*bpp = nil;
		return 0;
	}

	rp->seg = *seg;
	rp->bp = *bpp;
	rp->length = length;
	*bpp = nil;

	tcb->reseqlen += length;
	tcb->nreseq++;

	/* Place on reassembly list sorting by starting seq number */
	for(rr = &tcb->reseq;; rr = &(*rr)->next)
		if(*rr == nil || seq_lt(seg->seq, (*rr)->seg.seq)){
			rp->next = *rr;
			*rr = rp;
			tpriv->stats[Resequenced]++;
			if(rp->next != nil)
				tpriv->stats[OutOfOrder]++;
			break;
		}

	qmax = tcb->window;
	if(tcb->reseqlen > qmax){
		netlog(f, Logtcp, "tcp: reseq: queue > window: %d > %d; %d packets\n", tcb->reseqlen, qmax, tcb->nreseq);
		logreseq(f, tcb->reseq, tcb->rcv.nxt);
		tpriv->stats[ReseqBytelim]++;
		return dumpreseq(tcb);
	}
	qmax = (tcb->window + tcb->mss-1) / tcb->mss;
	if(tcb->nreseq > qmax){
		netlog(f, Logtcp, "resequence queue > packets: %d %d; %d bytes\n", tcb->nreseq, qmax, tcb->reseqlen);
		logreseq(f, tcb->reseq, tcb->rcv.nxt);
		tpriv->stats[ReseqPktlim]++;
		return dumpreseq(tcb);
	}

	return 0;
}

static int
getreseq(Tcpctl *tcb, Tcp *seg, Block **bp, ushort *length)
{
	Reseq *rp;

	rp = tcb->reseq;
	if(rp == nil || seq_gt(rp->seg.seq, tcb->rcv.nxt))
		return -1;

	*seg = rp->seg;
	*bp = rp->bp;
	*length = rp->length;

	tcb->reseq = rp->next;
	tcb->reseqlen -= rp->length;
	tcb->nreseq--;

	free(rp);

	return 0;
}

static int
tcptrim(Tcpctl *tcb, Tcp *seg, Block **bp, ushort *length)
{
	int dupcnt, excess;

	dupcnt = (int)(tcb->rcv.nxt - seg->seq);
	if(dupcnt > 0){
		if(seg->flags & SYN){
			seg->flags &= ~SYN;
			seg->seq++;
			dupcnt--;
		}
		if(dupcnt >= *length){
			tcb->overlap += *length;
			freeblist(*bp);
			*bp = nil;
			return -1;
		}
		tcb->overlap += dupcnt;
		pullblock(bp, dupcnt);
		seg->seq += dupcnt;
		*length -= dupcnt;
	}
	excess = (int)(seg->seq + *length - tcb->rcv.wptr);
	if(excess > 0) {
		if(excess >= *length){
			tcb->overlap += *length;
			freeblist(*bp);
			*bp = nil;
			return -1;
		}
		seg->flags &= ~FIN;
		tcb->overlap += excess;
		*length -= excess;
		*bp = trimblock(*bp, 0, *length);
	}
	if(*bp == nil)
		return -1;
	return 0;
}

static void
tcpadvise(Proto *tcp, Block *bp, Ipifc *ifc, char *msg)
{
	uchar source[IPaddrlen];
	uchar dest[IPaddrlen];
	ushort psource, pdest;
	Tcp4hdr *h4;
	Tcp6hdr *h6;
	Iphash *iph;
	Tcpctl *tcb;
	Conv *s;

	h4 = (Tcp4hdr*)(bp->rp);
	h6 = (Tcp6hdr*)(bp->rp);

	if((h4->vihl&0xF0)==IP_VER4) {
		v4tov6(dest, h4->tcpdst);
		v4tov6(source, h4->tcpsrc);
		psource = nhgets(h4->tcpsport);
		pdest = nhgets(h4->tcpdport);
	} else {
		ipmove(dest, h6->tcpdst);
		ipmove(source, h6->tcpsrc);
		psource = nhgets(h6->tcpsport);
		pdest = nhgets(h6->tcpdport);
	}

	/* Look for a connection (source/dest reversed; this is the original packet we sent) */
	qlock(tcp);
	iph = iphtlook(tcp->ht, dest, pdest, source, psource);
	if(iph == nil || iph->match != IPmatchexact)
		goto raise;
	if(iph->trans){
		Translation *q;

		if((q = transbackward(tcp, iph)) == nil)
			goto raise;

		/* h4->tcplen is the ip header checksum */
		hnputs_csum(h4->tcpsrc+0, nhgets(q->forward.raddr+IPv4off+0), h4->tcplen);
		hnputs_csum(h4->tcpsrc+2, nhgets(q->forward.raddr+IPv4off+2), h4->tcplen);

		/* dont bother fixing tcp checksum, packet is most likely truncated */
		hnputs(h4->tcpsport, q->forward.rport);
		qunlock(tcp);

		icmpproxyadvice(tcp->f, ifc, bp, h4->tcpsrc);
		return;
	}
	s = iphconv(iph);
	if(s->ignoreadvice || s->state == Announced || s->state == Closed)
		goto raise;
	tcb = (Tcpctl*)s->ptcl;
	qlock(s);
	qunlock(tcp);
	if(tcb->state == Syn_sent)
		localclose(s, msg);
	qunlock(s);
	freeblist(bp);
	return;
raise:
	qunlock(tcp);
	freeblist(bp);
}

static Block*
tcpforward(Proto *tcp, Block *bp, Route *r)
{
	uchar da[IPaddrlen], sa[IPaddrlen];
	ushort dp, sp;
	Tcp4hdr *h4;
	Translation *q;

	h4 = (Tcp4hdr*)(bp->rp);
	v4tov6(da, h4->tcpdst);
	v4tov6(sa, h4->tcpsrc);
	dp = nhgets(h4->tcpdport);
	sp = nhgets(h4->tcpsport);

	/* don't make new translation when not syn packet */
	if((h4->tcpflag[1] & (ACK|RST|SYN|FIN)) != SYN)
		r = nil;

	qlock(tcp);
	q = transforward(tcp, sa, sp, da, dp, r);
	if(q == nil){
		qunlock(tcp);
		freeblist(bp);
		return nil;
	}
	hnputs_csum(h4->tcpsrc+0, nhgets(q->backward.laddr+IPv4off+0), h4->tcpcksum);
	hnputs_csum(h4->tcpsrc+2, nhgets(q->backward.laddr+IPv4off+2), h4->tcpcksum);
	hnputs_csum(h4->tcpsport, q->backward.lport, h4->tcpcksum);
	qunlock(tcp);

	return bp;
}

void
tcpmssclamp(uchar *p, int n, int mtu)
{
	Tcp4hdr *h4;
	Tcp6hdr *h6;
	uchar *pcksum;
	int hdrlen, optlen, newmss, oldmss;

	if(n < TCP4_PKT)
		return;
	h4 = (Tcp4hdr*)p;
	h6 = (Tcp6hdr*)p;
	if((h4->vihl&0xF0)==IP_VER4) {
		if(h4->proto != IP_TCPPROTO)
			return;
		if((h4->frag[0] & (IP_FO>>8)) | (h4->frag[1] & IP_FO))
			return;
		if(!(h4->tcpflag[1] & SYN))
			return;
		hdrlen = (h4->tcpflag[0] >> 2) & ~3;
		if(hdrlen > (n - TCP4_PKT))
			return;
		n = hdrlen - TCP4_HDRSIZE;
		p = h4->tcpopt;
		pcksum = h4->tcpcksum;
		newmss = mtu - (TCP4_PKT + TCP4_HDRSIZE);
	} else {
		if(n < TCP6_PKT)
			return;
		if(h6->proto != IP_TCPPROTO)
			return;
		if(!(h6->tcpflag[1] & SYN))
			return;
		hdrlen = (h6->tcpflag[0] >> 2) & ~3;
		if(hdrlen > (n - TCP6_PKT))
			return;
		n = hdrlen - TCP6_HDRSIZE;
		p = h6->tcpopt;
		pcksum = h6->tcpcksum;
		newmss = mtu - (TCP6_PKT + TCP6_HDRSIZE);
	}
	while(n > 0 && *p != EOLOPT) {
		if(*p == NOOPOPT) {
			n--;
			p++;
			continue;
		}
		optlen = p[1];
		if(optlen < 2 || optlen > n)
			break;
		if(*p == MSSOPT){
			if(optlen != MSS_LENGTH)
				break;
			oldmss = nhgets(p+2);
			if(newmss >= oldmss)
				break;
			hnputs_csum(p+2, newmss, pcksum);
			break;
		}
		n -= optlen;
		p += optlen;
	}
}

/* called with c qlocked */
static char*
tcpctl(Conv* c, char** f, int n)
{
	if(n == 1 && strcmp(f[0], "close") == 0)
		return tcpclose(c), nil;
	if(n == 1 && strcmp(f[0], "hangup") == 0)
		return tcphangup(c);
	if(n >= 1 && strcmp(f[0], "keepalive") == 0)
		return tcpstartka(c, f, n);
	return "unknown control request";
}

static int
tcpstats(Proto *tcp, char *buf, int len)
{
	Tcppriv *tpriv = (Tcppriv*)tcp->priv;
	char *p, *e;
	int i;

	tpriv->stats[InLimbo] = tpriv->nlimbo;

	p = buf;
	e = p+len;
	for(i = 0; i < Nstats; i++)
		p = seprint(p, e, "%s: %llud\n", statnames[i], tpriv->stats[i]);
	return p - buf;
}

/*
 *  garbage collect any stale conversations:
 *	- SYN received but no SYN-ACK after 5 seconds (could be the SYN attack)
 *	- Finwait2 after 5 minutes
 *
 *  this is called whenever we run out of channels.  Both checks are
 *  of questionable validity so we try to use them only when we're
 *  up against the wall.
 */
static int
tcpgc(Proto *tcp)
{
	int n;
	ulong now;
	Conv *c, **pp, **ep;
	Tcpctl *tcb;

	n = 0;
	now = NOW;
	ep = &tcp->conv[tcp->nc];
	for(pp = tcp->conv; pp < ep; pp++) {
		c = *pp;
		if(c == nil)
			break;
		if(!canqlock(c))
			continue;
		tcb = (Tcpctl*)c->ptcl;
		switch(tcb->state){
		case Syn_received:
			if(now - tcb->time > 5000){
				localclose(c, Etimedout);
				n++;
			}
			break;
		case Finwait2:
			if(now - tcb->time > 5*60*1000){
				localclose(c, Etimedout);
				n++;
			}
			break;
		}
		qunlock(c);
	}
	return n;
}

static void
tcpsettimer(Conv *s)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	int x;

	tcb->time = NOW;
	tcb->timeuna = tcb->snd.una;

	/* round trip dependency */
	x = tcb->mdev;
	x += tcb->srtt >> LOGAGAIN;
	x <<= tcb->backoff;

	/* bounded twixt 0.3 and 64 seconds */
	if(x < 300)
		x = 300;
	else if(x > 64000)
		x = 64000;

	/* reset the timer */
	tcb->timer.start = (x + MSPTICK-1) / MSPTICK;
	if(tcb->timer.state == TcptimerON){
		tcb->timer.count = tcb->timer.start;
	} else {
		Tcppriv *tpriv = (Tcppriv*)s->p->priv;
		tcpgo(tpriv, &tcb->timer);
	}
}

void
tcpinit(Fs *fs)
{
	Proto *tcp;
	Tcppriv *tpriv;

	tcp = smalloc(sizeof(Proto));
	tcp->priv = tpriv = smalloc(sizeof(Tcppriv));
	tcp->ht = &tpriv->ht;
	tcp->name = "tcp";
	tcp->connect = tcpconnect;
	tcp->announce = tcpannounce;
	tcp->ctl = tcpctl;
	tcp->state = tcpstate;
	tcp->create = tcpcreate;
	tcp->close = tcpclose;
	tcp->rcv = tcpiput;
	tcp->advise = tcpadvise;
	tcp->forward = tcpforward;
	tcp->stats = tcpstats;
	tcp->inuse = tcpinuse;
	tcp->gc = tcpgc;
	tcp->ipproto = IP_TCPPROTO;
	tcp->nc = scalednconv();
	tcp->ptclsize = sizeof(Tcpctl);

	tpriv->stats[MaxConn] = tcp->nc;

	Fsproto(fs, tcp);
}

static void
tcpsetmss(Conv *s, ushort mss)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;

	/* our sending max segment size cannot be bigger than what he asked for */
	if(mss != 0 && mss < tcb->mss)
		tcb->mss = mss;
	
	/* RFC 3390 initial window */
	if(tcb->mss < 1095)
		tcb->cwind = 4*tcb->mss;
	else if(tcb->mss < 2190)
		tcb->cwind = 4380;
	else
		tcb->cwind = 2*tcb->mss;
}

static void
tcpsetscale(Conv *s, uchar scale)
{
	Tcpctl *tcb = (Tcpctl*)s->ptcl;
	ulong window;

	if(scale == 0)
		tcb->scale = 0;
	else if(scale > 14)
		scale = 14;	/* RFC 7323 2.3 */

	tcb->rcv.scale = tcb->scale;
	tcb->window = QMAX<<tcb->rcv.scale;
	qsetlimit(s->rq, tcb->window);

	tcb->snd.scale = scale;
	window = QMAX<<tcb->snd.scale;
	if(window > tcb->window)
		window = tcb->window;
	tcb->ssthresh = window;
	qsetlimit(s->wq, window);
}
