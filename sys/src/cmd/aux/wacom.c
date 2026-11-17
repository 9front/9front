#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

typedef struct Tablet Tablet;
typedef struct Message Message;
typedef struct QItem QItem;
typedef struct Queue Queue;
typedef struct Reader Reader;


enum { MAX = 1000 };

struct Tablet {
	int ser;
	int xmax, ymax, pmax, version;
	int sx, sy;
};

struct Message {
	Ref;
	int b, x, y, p;
	char *msg;
};

int
inittablet(int serctl, Tablet* t, int baud)
{
	uchar buf[11];

	if(fprint(serctl, "b%d\nl8\nq0\nq1024\n", baud) < 0) return -1;
	if(fprint(serctl, "f\n") < 0) return -1;
	sleep(250);
	if(write(t->ser, "*", 1) != 1) return -1;
	if(fprint(serctl, "f\n") < 0) return -1;
	do {
		if(read(t->ser, buf, 1) < 1) return -1;
	} while(buf[0] != 0xC0);
	if(readn(t->ser, buf+1, 10) < 10) return -1;
	t->xmax = (buf[1] << 9) | (buf[2] << 2) | ((buf[6] >> 5) & 3);
	t->ymax = (buf[3] << 9) | (buf[4] << 2) | ((buf[6] >> 3) & 3);
	t->pmax = buf[5] | (buf[6] & 7);
	t->version = (buf[9] << 7) | buf[10];
	if(write(t->ser, "1", 1) < 1) return -1;
	return 0;
}

Tablet*
newtablet(char* dev, int baud)
{
	int serctl;
	char* ctl;
	Tablet* t;

	ctl = smprint("%sctl", dev);
	t = calloc(sizeof(Tablet), 1);
	t->ser = open(dev, ORDWR);
	if(t->ser < 0) {
		free(t);
		return 0;
	}
	serctl = open(ctl, OWRITE);
	free(ctl);
	if(serctl < 0) {
		free(t);
		close(t->ser);
		return 0;
	}
	if (inittablet(serctl, t, baud) < 0) {
		free(t);
		close(t->ser);
		close(serctl);
		return 0;
	}
	close(serctl);
	return t;
}

int
screensize(Tablet* t)
{
	int fd;
	char buf[189], buf2[12], *p;
	
	fd = open("/dev/draw/new", OREAD);
	if(fd < 0) return -1;
	read(fd, buf, 189);
	memcpy(buf2, buf + 72, 11);
	buf2[11] = 0;
	for(p = buf2; *p == ' '; p++);
	t->sx = atoi(p);
	memcpy(buf2, buf + 84, 11);
	for(p = buf2; *p == ' '; p++);
	t->sy = atoi(p);
	if(t->sx == 0 || t->sy == 0) {
		close(fd);
		werrstr("invalid resolution read from /dev/draw/new");
		return -1;
	}
	
	close(fd);
	return 0;
}

Message*
packetready(Tablet* t, uchar* buf, int ct)
{
	/*
	Touch event packets may be 5, 7, or 13 bytes long depending on the tablet,
    with 13 being multitouch.

	Stylus events are 9 bytes long.

	The touch query response is 10 bytes long.

    The stylus query response is 11 bytes long.
	*/

	if (ct == 9) {
		Message *m;
		m = calloc(sizeof(Message), 1);
		incref(m);
	
		m->b = buf[0] & 7;
		m->x = (buf[1] << 9) | (buf[2] << 2) | ((buf[6] >> 5) & 3);
		m->y = (buf[3] << 9) | (buf[4] << 2) | ((buf[6] >> 3) & 3);
		m->p = ((buf[6] & 7) << 7) | buf[5];
	
		m->p *= MAX;
		m->p /= t->pmax;
		m->x *= t->sx;
		m->x /= t->xmax;
		m->y *= t->sy;
		m->y /= t->ymax;
	
		m->msg = smprint("m %d %d %d %d\n", m->x, m->y, m->b, m->p);
		return m;
	}
	else {
		return 0;
	}
}

void
msgdecref(Message *m)
{
	if(decref(m) == 0) {
		free(m->msg);
		free(m);
	}
}

struct QItem {
	Message *m;
	QItem *next;
};

struct Queue {
	Lock;
	QItem *first, *last;
};

void
qput(Queue* q, Message* m)
{
	QItem *i;
	
	lock(q);
	i = malloc(sizeof(QItem));
	i->m = m;
	i->next = 0;
	if(q->last == nil) {
		q->last = q->first = i;
	} else {
		q->last->next = i;
		q->last = i;
	}
	unlock(q);
}

Message*
qget(Queue* q)
{
	QItem *i;
	Message *m;
	
	if(q->first == nil) return nil;
	lock(q);
	i = q->first;
	if(q->first == q->last) {
		q->first = q->last = nil;
	} else {
		q->first = i->next;
	}
	m = i->m;
	free(i);
	unlock(q);
	return m;
}

void
freequeue(Queue *q)
{
	Message *m;
	
	while(m = qget(q))
		msgdecref(m);
	free(q);
}

struct Reader {
	Queue *e;
	Reader *prev, *next;
	Req* req;
};

Lock readers;
Reader *rfirst, *rlast;

void
reply(Req *req, Message *m)
{
	req->ofcall.count = strlen(m->msg);
	if(req->ofcall.count > req->ifcall.count)
		req->ofcall.count = req->ifcall.count;
	memmove(req->ofcall.data, m->msg, req->ofcall.count);
	respond(req, nil);
}

void
sendout(Message *m)
{
	Reader *r;
	
	lock(&readers);
	for(r = rfirst; r; r = r->next) {
		if(r->req) {
			reply(r->req, m);
			r->req = nil;
		} else {
			incref(m);
			qput(r->e, m);
		}
	}
	unlock(&readers);
}

void
tabletopen(Req *req)
{
	Reader *r;
	
	lock(&readers);
	r = calloc(sizeof(Reader), 1);
	r->e = calloc(sizeof(Queue), 1);
	if(rlast) rlast->next = r;
	r->prev = rlast;
	rlast = r;
	if(rfirst == nil) rfirst = r;
	unlock(&readers);
	req->fid->aux = r;
	respond(req, nil);
}

void
tabletdestroyfid(Fid *fid)
{
	Reader *r;

	r = fid->aux;
	if(r == nil) return;
	lock(&readers);
	if(r->prev) r->prev->next = r->next;
	if(r->next) r->next->prev = r->prev;
	if(r == rfirst) rfirst = r->next;
	if(r == rlast) rlast = r->prev;
	freequeue(r->e);
	free(r);
	unlock(&readers);
}

void
tabletdestroyreq(Req *req)
{
	Reader *r;
	
	if(req->fid == nil) return;
	r = req->fid->aux;
	if(r == nil) return;
	if(req == r->req) {
		r->req = nil;
	}
}

void
tabletread(Req* req)
{
	Reader *r;
	Message *m;
	
	r = req->fid->aux;
	if(m = qget(r->e)) {
		reply(req, m);
		msgdecref(m);
	} else {
		lock(&readers);
		if(r->req) {
			respond(req, "no concurrent reads, please");
		} else {
			r->req = req;
		}
		unlock(&readers);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [ -b baud ]\n", argv0);
	exits("usage");
}

Srv tabletsrv = {
	.open = tabletopen,	
	.read = tabletread,
	.destroyfid = tabletdestroyfid,
	.destroyreq = tabletdestroyreq,
};

File *tfile;

void
main(int argc, char **argv)
{
	Tablet *t;
	Message *m;
	int fd[2];
	uchar buf[13];
	uint ct;
	uchar got;
	int baud;

	baud = 19200;
	ARGBEGIN {
	case 'b':
		baud = atoi(ARGF());
		break;
	default:
		usage();
	} ARGEND;

	pipe(fd);
	tabletsrv.infd = tabletsrv.outfd = fd[0];
	tabletsrv.tree = alloctree(getuser(), getuser(), 0555, 0);
	tfile = createfile(tabletsrv.tree->root, "tablet", getuser(), 0400, 0);
	if(rfork(RFPROC | RFMEM | RFNOWAIT | RFNOTEG) > 0) exits(nil);
	if(rfork(RFPROC | RFMEM) == 0) {
		srv(&tabletsrv);
		exits(nil);
	}
	mount(fd[1], -1, "/dev", MAFTER, "");

	t = newtablet("/dev/eia2", baud);
	if(!t) sysfatal("%r");
	if(screensize(t) < 0) sysfatal("%r");

	m = 0;
	ct = 0;
	while(1) {
		got = 0;
		if(read(t->ser, &got, 1) != 1) sysfatal("%r");
		if((got & 0x80) == 0x80) {
			m = packetready(t, buf, ct);
			buf[0] = got;
			ct = 1;
		} else if (ct < 13) {
			buf[ct++] = got;
		} else {
			ct = -1;
		}
		if (m) {
			sendout(m);
			msgdecref(m);
			m = 0;
		}
	}
}
