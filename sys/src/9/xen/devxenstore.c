/*
 * Driver for xenstore - database shared between domains, used by xenbus to
 * communicate configuration info.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../pc/io.h"

#define LOG(a)

typedef struct Aux Aux;

enum {
	Qtopdir,
	Qctl,
	Qwatch,
	WRITING = 0,
	READING,
	WATCHING,
	MAXIO = 8*1024,
};

Dirtab xsdir[] = {
	".",	{Qtopdir, 0, QTDIR},	0,	0555,
	"xenstore",	{Qctl, 0},	0,	0660,
	"xenwatch", {Qwatch, 0}, 0, 0440,
};

struct {
	struct xenstore_domain_interface	*intf;
	struct xsd_sockmsg	hdr;
	int	hdrvalid;
	int	evtchn;
	int	nextreqid;
	Aux *rhead;
	Aux *kernelaux;
	Queue *evq;
	Rendez wr;
	Rendez rr;
	QLock;
	Lock rlock;
} xenstore;

struct Aux {
	QLock;
	Rendez qr;
	Queue *ioq;
	Aux	*next;
	int state;
	int	reqid;
};

static char Ephase[] = "phase error";
static char Eproto[] = "protocol error";
static char NodeShutdown[] = "control/shutdown";

static void xenbusproc(void*);

static int
notfull(void*)
{
	struct xenstore_domain_interface *xs = xenstore.intf;

	return (xs->req_prod-xs->req_cons) < XENSTORE_RING_SIZE;
}

static int
notempty(void*)
{
	struct xenstore_domain_interface *xs = xenstore.intf;

	return xs->rsp_prod > xs->rsp_cons;
}

static int
ishead(void* a)
{
	return xenstore.rhead == a;
}

static void
xsintr(Ureg*, void*)
{
	LOG(dprint("xsintr\n");)
	wakeup(&xenstore.rr);
	wakeup(&xenstore.wr);
}

static void
xwrite(Queue *q, char *buf, int len)
{
	struct xenstore_domain_interface *xs;
	int m, n;
	XENSTORE_RING_IDX idx;

	xs = xenstore.intf;
	while (len > 0) {
		n = XENSTORE_RING_SIZE - (xs->req_prod - xs->req_cons);
		if (n == 0) {
			xenchannotify(xenstore.evtchn);
			sleep(&xenstore.wr, notfull, 0);
			continue;
		}
		if (n > len)
			n = len;
		idx = MASK_XENSTORE_IDX(xs->req_prod);
		m = XENSTORE_RING_SIZE - idx;
		if (m > n)
			m = n;
		if (q)
			qread(q, xs->req+idx, m);
		else
			memmove(xs->req+idx, buf, m);
		if (m < n) {
			if (q)
				qread(q, xs->req, n-m);
			else
				memmove(xs->req, buf+m, n-m);
		}
		coherence();
		xs->req_prod += n;
		xenchannotify(xenstore.evtchn);
		if (buf)
			buf += n;
		len -= n;
	}
}

static void
xread(Queue *q, char *buf, int len)
{
	struct xenstore_domain_interface *xs = xenstore.intf;
	int n, m;
	XENSTORE_RING_IDX idx;

	for (n = len; n > 0; n -= m) {
		while (xs->rsp_prod == xs->rsp_cons) {
			xenchannotify(xenstore.evtchn);
			if (up == 0)
				HYPERVISOR_yield();
			else
				sleep(&xenstore.rr, notempty, 0);
		}
		idx = MASK_XENSTORE_IDX(xs->rsp_cons);
		m = xs->rsp_prod - xs->rsp_cons;
		if (m > n)
			m = n;
		if (m > XENSTORE_RING_SIZE - idx)
			m = XENSTORE_RING_SIZE - idx;
		if (q)
			qwrite(q, xs->rsp+idx, m);
		else if (buf) {
			memmove(buf, xs->rsp+idx, m);
			buf += m;
		}
		coherence();
		xs->rsp_cons += m;
	}
	xenchannotify(xenstore.evtchn);
}

static void
xsrpc(Aux *aux)
{
	Queue *q;
	Aux *l, *r, **lp;
	struct xsd_sockmsg hdr;
	long n;

	q = aux->ioq;

	if (aux->state == WATCHING)
		aux->reqid = 0;
	else {
		/* get the request header and check validity */
		if (qlen(q) < sizeof hdr)
			error(Eproto);
		qread(q, &hdr, sizeof hdr);
		n = hdr.len;
		if (qlen(q) != n)
			error(Eproto);
		qlock(&xenstore);
		/* generate a unique request id */
		aux->reqid = ++xenstore.nextreqid;
		hdr.req_id = aux->reqid;
		hdr.tx_id = 0;
		/* send the request */
		xwrite(0, (char*)&hdr, sizeof hdr);
		xwrite(q, 0, n);
		qunlock(&xenstore);
	}

	/* join list of requests awaiting response */
	ilock(&xenstore.rlock);
	if (xenstore.rhead == 0) {
		aux->next = 0;
		xenstore.rhead = aux;
	} else {
		aux->next = xenstore.rhead->next;
		xenstore.rhead->next = aux;
	}
	iunlock(&xenstore.rlock);

	/* loop until matching response header has been received */
	if (waserror()) {
		ilock(&xenstore.rlock);
		for (lp = &xenstore.rhead; *lp && *lp != aux; lp = &(*lp)->next)
			;
		if (*lp != 0) {
			*lp = (*lp)->next;
			if (lp == &xenstore.rhead && *lp)
				wakeup(&(*lp)->qr);
		}
		iunlock(&xenstore.rlock);
		nexterror();
	}
	for (;;) {
		/* wait until this request reaches head of queue */
		if (xenstore.rhead != aux)
			sleep(&aux->qr, ishead, aux);
		/* wait until a response header (maybe for another request) has been read */
		if (!xenstore.hdrvalid) {
			xread(0, (char*)&xenstore.hdr, sizeof xenstore.hdr);
			xenstore.hdrvalid = 1;
		}
		if (xenstore.hdr.req_id == aux->reqid)
			break;
		/* response was for a different request: move matching request to head of queue */
		ilock(&xenstore.rlock);
		for (l = xenstore.rhead; r = l->next; l = r)
			if (xenstore.hdr.req_id == r->reqid) {
				l->next = r->next;
				r->next = xenstore.rhead;
				xenstore.rhead = r;
				break;
			}
		iunlock(&xenstore.rlock);
		if (r) {
			/* wake the matching request */
			wakeup(&r->qr);
		} else {
			/* response without a request: should be a watch event */
			xenstore.hdrvalid = 0;
			xread(0, 0, xenstore.hdr.len);
			continue;
		}
	}

	/* queue the response header, and data if any, for the caller to read */
	qwrite(q, &xenstore.hdr, sizeof xenstore.hdr);
	xenstore.hdrvalid = 0;
	/* read the data, if any */
	if (xenstore.hdr.len > 0)
		xread(q, 0, xenstore.hdr.len);

	/* remove finished request and wake the next request on the queue */
	ilock(&xenstore.rlock);
	xenstore.rhead = aux->next;
	iunlock(&xenstore.rlock);
	poperror();
	if (xenstore.rhead != 0)
		wakeup(&xenstore.rhead->qr);
}

static void
xsreset()
{
	LOG(dprint("xsreset\n");)
}

static void
xsinit()
{
	intrenable(xenstore.evtchn, xsintr, 0, BUSUNKNOWN, "Xen store");
	kproc("xenbus", xenbusproc, 0);
}

static Chan*
xsattach(char *spec)
{
	return devattach('x', spec);
}

static Walkqid*
xswalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, xsdir, nelem(xsdir), devgen);
}

static int
xsstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, xsdir, nelem(xsdir), devgen);
}

static Aux*
auxalloc(int initstate)
{
	Aux *aux;
	Queue *q;

	aux = mallocz(sizeof(Aux), 1);
	if (aux == 0)
		return 0;
	q = qopen(MAXIO, 0, 0, 0);
	if (q == 0) {
		free(aux);
		return 0;
	}
	qnoblock(q, 1);
	aux->state = initstate;
	aux->ioq = q;
	return aux;
}

static Chan*
xsopen(Chan *c, int omode)
{
	Aux *aux;
	int state;

	c = devopen(c, omode, xsdir, nelem(xsdir), devgen);
	state = WRITING;
	switch ((ulong)c->qid.path) {
	case Qwatch:
		state = WATCHING;
	/* fall through */
	case Qctl:
		aux = auxalloc(state);
		if (aux == 0) {
			c->flag &= ~COPEN;
			error(Enomem);
		}
		c->aux = aux;
		break;
	}
	return c;
}

static void
xsclose(Chan* c)
{
	Aux *aux;

	if ((c->flag&COPEN) == 0)
		return;

	switch ((ulong)c->qid.path) {
	case Qwatch:
	case Qctl:
		if ((aux = (Aux*)c->aux) != 0) {
			qfree(aux->ioq);
			free(aux);
			c->aux = 0;
		}
		break;
	}
}

static long
xsread(Chan *c, void *a, long n, vlong off)
{
	Aux *aux;
	Queue *q;
	long nr;

	USED(off);
	if (c->qid.type == QTDIR)
		return devdirread(c, a, n, xsdir, nelem(xsdir), devgen);

	aux = (Aux*)c->aux;
	qlock(aux);
	if (waserror()) {
		qunlock(aux);
		nexterror();
	}
	q = aux->ioq;
	switch (aux->state) {
	case WRITING:
		if (qlen(q) == 0)
			error(Ephase);
		xsrpc(aux);
		aux->state = READING;
		break;
	case WATCHING:
		if (qlen(q) == 0)
			xsrpc(aux);
		break;
	}
	if (!qcanread(q))
		nr = 0;
	else
		nr = qread(q, a, n);
	qunlock(aux);
	poperror();
	return nr;
}

static long
xswrite(Chan *c, void *a, long n, vlong off)
{
	Aux *aux;
	Queue *q;
	long nr;

	if (c->qid.type == QTDIR)
		error(Eperm);
	if ((ulong)c->qid.path == Qwatch)
		error(Ebadusefd);

	aux = (Aux*)c->aux;
	qlock(aux);
	if (waserror()) {
		qunlock(aux);
		nexterror();
	}
	q = aux->ioq;
	if ((off == 0 || aux->state == READING) && qlen(q) > 0)
		qflush(q);
	aux->state = WRITING;
	nr = qwrite(aux->ioq, a, n);
	qunlock(aux);
	poperror();
	return nr;
}

Dev xenstoredevtab = {
	'x',
	"xenstore",

	xsreset,
	xsinit,
	devshutdown,
	xsattach,
	xswalk,
	xsstat,
	xsopen,
	devcreate,
	xsclose,
	xsread,
	devbread,
	xswrite,
	devbwrite,
	devremove,
	devwstat,
};

static char*
xscmd(Aux *aux, char *buf, int cmd, char *s, char *val)
{
	struct xsd_sockmsg *msg;
	char *arg;
	long n;

	msg = (struct xsd_sockmsg*)buf;
	arg = buf + sizeof(*msg);
	msg->type = cmd;
	msg->len = strlen(s)+1;
	if (val) {
		msg->len += strlen(val);
		if (cmd == XS_WATCH)
			msg->len++;		/* stupid special case */
	}
	strcpy(arg, s);
	if (val)
		strcpy(arg+strlen(s)+1, val);
	n = sizeof(*msg)+msg->len;
	if (up == 0) {
		msg->req_id = 1;
		msg->tx_id = 0;
		xwrite(0, buf, n);
		xread(0, buf, sizeof(*msg));
		xread(0, arg, msg->len);
	} else {
		qlock(aux);
		if (qlen(aux->ioq) > 0)
			qflush(aux->ioq);
		qwrite(aux->ioq, buf, n);
		xsrpc(aux);
		qread(aux->ioq, buf, sizeof(*msg));
		LOG(dprint("xs: type %d req_id %d len %d\n", msg->type, msg->req_id, msg->len);)
		// XXX buffer overflow
		qread(aux->ioq, arg, msg->len);
		qunlock(aux);
	}
	arg[msg->len] = 0;
	if (msg->type == XS_ERROR) {
		return 0;
	}
	return arg;
}

static void
intfinit(void)
{
	if (xenstore.intf == 0) {
		xenstore.intf = (struct xenstore_domain_interface*)mmumapframe(XENBUS, xenstart->store_mfn);
		xenstore.evtchn = xenstart->store_evtchn;
		xenstore.kernelaux = auxalloc(WRITING);
	}
}

void
xenstore_write(char *s, char *val)
{
	char buf[512];

	intfinit();
	xscmd(xenstore.kernelaux, buf, XS_WRITE, s, val);
}

int
xenstore_read(char *s, char *val, int len)
{
	char buf[512];
	char *p;

	intfinit();
	p = xscmd(xenstore.kernelaux, buf, XS_READ, s, nil);
	if (p == 0)
		return -1;
	strecpy(val, val+len, p);
	return 1;
}

void
xenstore_setd(char *dir, char *node, int value)
{
	int off;
	char buf[12];

	off = strlen(dir);
	sprint(dir+off, "%s", node);
	sprint(buf, "%ud", value);
	xenstore_write(dir, buf);
	dir[off] = 0;
}

int
xenstore_gets(char *dir, char *node, char *buf, int buflen)
{
	int off;
	int n;

	off = strlen(dir);
	sprint(dir+off, "%s", node);
	n = xenstore_read(dir, buf, buflen);
	dir[off] = 0;
	return n;
}

static void
xenbusproc(void*)
{
	Chan *c;
	Aux *aux;
	char *p;
	struct xsd_sockmsg msg;
	char buf[512];
	int n, m;

	c = namec("#x/xenstore", Aopen, ORDWR, 0);
	aux = (Aux*)c->aux;
	c = namec("#x/xenwatch", Aopen, OREAD, 0);
	xscmd(aux, buf, XS_WATCH, NodeShutdown, "$");
	for (;;) {
		xsread(c, &msg, sizeof(msg), 0);
		for (n = msg.len; n > 0; n -= m)
			m = xsread(c, buf, msg.len, sizeof(msg));
		buf[msg.len] = 0;
		if (strcmp(buf, NodeShutdown) != 0)
			continue;
		p = xscmd(aux, buf, XS_READ, NodeShutdown, nil);
		if (p == nil)
			continue;
		if (strcmp(p, "poweroff") == 0)
			reboot(nil, nil, 0);
		else if (strcmp(p, "reboot") == 0)
			exit(0);
		else {
			print("xenbus: %s=%s\n", NodeShutdown, p);
			xscmd(aux, buf, XS_WRITE, NodeShutdown, "");
		}
	}
}
