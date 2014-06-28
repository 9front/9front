/*
 * This part takes care of locking except for initialization and
 * other threads created by the hw dep. drivers.
 */

#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "usb.h"
#include "serial.h"
#include "prolific.h"
#include "ucons.h"
#include "ftdi.h"
#include "silabs.h"

int serialdebug;
static int sdebug;

Serialport **ports;
int nports;

static void
serialfatal(Serial *ser)
{
	Serialport *p;
	int i;

	dsprint(2, "serial: fatal error, detaching\n");
	devctl(ser->dev, "detach");

	for(i = 0; i < ser->nifcs; i++){
		p = &ser->p[i];
		if(p->w4data != nil)
			chanclose(p->w4data);
		if(p->gotdata != nil)
			chanclose(p->gotdata);
		if(p->readc)
			chanclose(p->readc);
	}
}

/* I sleep with the lock... only way to drain in general */
static void
serialdrain(Serialport *p)
{
	Serial *ser;
	uint baud, pipesize;

	ser = p->s;
	baud = p->baud;

	if(p->baud == ~0)
		return;
	if(ser->maxwtrans < 256)
		pipesize = 256;
	else
		pipesize = ser->maxwtrans;
	/* wait for the at least 256-byte pipe to clear */
	sleep(10 + pipesize/((1 + baud)*1000));
	if(ser->clearpipes != nil)
		ser->clearpipes(p);
}

int
serialreset(Serial *ser)
{
	Serialport *p;
	int i, res;

	res = 0;
	/* cmd for reset */
	for(i = 0; i < ser->nifcs; i++){
		p = &ser->p[i];
		serialdrain(p);
	}
	if(ser->reset != nil)
		res = ser->reset(ser, nil);
	return res;
}

/* call this if something goes wrong, must be qlocked */
int
serialrecover(Serial *ser, Serialport *p, Dev *ep, char *err)
{
	if(p != nil)
		dprint(2, "serial[%d], %s: %s, level %d\n", p->interfc,
			p->name, err, ser->recover);
	else
		dprint(2, "serial[%s], global error, level %d\n",
			ser->p[0].name, ser->recover);
	ser->recover++;
	if(strstr(err, "detached") != nil)
		return -1;
	if(ser->recover < 3){
		if(p != nil){	
			if(ep != nil){
				if(ep == p->epintr)
					unstall(ser->dev, p->epintr, Ein);
				if(ep == p->epin)
					unstall(ser->dev, p->epin, Ein);
				if(ep == p->epout)
					unstall(ser->dev, p->epout, Eout);
				return 0;
			}

			if(p->epintr != nil)
				unstall(ser->dev, p->epintr, Ein);
			if(p->epin != nil)
				unstall(ser->dev, p->epin, Ein);
			if(p->epout != nil)
				unstall(ser->dev, p->epout, Eout);
		}
		return 0;
	}
	if(ser->recover > 4 && ser->recover < 8)
		serialfatal(ser);
	if(ser->recover > 8){
		ser->reset(ser, p);
		return 0;
	}
	if(serialreset(ser) < 0)
		return -1;
	return 0;
}

static int
serialctl(Serialport *p, char *cmd)
{
	Serial *ser;
	int c, i, n, nf, nop, nw, par, drain, set, lines;
	char *f[16];
	uchar x;

	ser = p->s;
	drain = set = lines = 0;
	nf = tokenize(cmd, f, nelem(f));
	for(i = 0; i < nf; i++){
		if(strncmp(f[i], "break", 5) == 0){
			if(ser->setbreak != nil)
				ser->setbreak(p, 1);
			continue;
		}

		nop = 0;
		n = atoi(f[i]+1);
		c = *f[i];
		if (isascii(c) && isupper(c))
			c = tolower(c);
		switch(c){
		case 'b':
			drain++;
			p->baud = n;
			set++;
			break;
		case 'c':
			p->dcd = n;
			// lines++;
			++nop;
			break;
		case 'd':
			p->dtr = n;
			lines++;
			break;
		case 'e':
			p->dsr = n;
			// lines++;
			++nop;
			break;
		case 'f':		/* flush the pipes */
			drain++;
			break;
		case 'h':		/* hangup?? */
			p->rts = p->dtr = 0;
			lines++;
			fprint(2, "serial: %c, unsure ctl\n", c);
			break;
		case 'i':
			++nop;
			break;
		case 'k':
			drain++;
			ser->setbreak(p, 1);
			sleep(n);
			ser->setbreak(p, 0);
			break;
		case 'l':
			drain++;
			p->bits = n;
			set++;
			break;
		case 'm':
			drain++;
			if(ser->modemctl != nil)
				ser->modemctl(p, n);
			if(n == 0)
				p->cts = 0;
			break;
		case 'n':
			p->blocked = n;
			++nop;
			break;
		case 'p':		/* extended... */
			if(strlen(f[i]) != 2)
				return -1;
			drain++;
			par = f[i][1];
			if(par == 'n')
				p->parity = 0;
			else if(par == 'o')
				p->parity = 1;
			else if(par == 'e')
				p->parity = 2;
			else if(par == 'm')	/* mark parity */
				p->parity = 3;
			else if(par == 's')	/* space parity */
				p->parity = 4;
			else
				return -1;
			set++;
			break;
		case 'q':
			// drain++;
			p->limit = n;
			++nop;
			break;
		case 'r':
			drain++;
			p->rts = n;
			lines++;
			break;
		case 's':
			drain++;
			p->stop = n;
			set++;
			break;
		case 'w':
			/* ?? how do I put this */
			p->timer = n * 100000LL;
			++nop;
			break;
		case 'x':
			if(n == 0)
				x = CTLS;
			else
				x = CTLQ;
			if(ser->wait4write != nil)
				nw = ser->wait4write(p, &x, 1);
			else
				nw = write(p->epout->dfd, &x, 1);
			if(nw != 1){
				serialrecover(ser, p, p->epout, "");
				return -1;
			}
			break;
		}
		/*
		 * don't print.  the condition is harmless and the print
		 * splatters all over the display.
		 */
		USED(nop);
		if (0 && nop)
			fprint(2, "serial: %c, unsupported nop ctl\n", c);
	}
	if(drain)
		serialdrain(p);
	if(lines && !set){
		if(ser->sendlines != nil && ser->sendlines(p) < 0)
			return -1;
	} else if(set){
		if(ser->setparam != nil && ser->setparam(p) < 0)
			return -1;
	}
	ser->recover = 0;
	return 0;
}

char *pformat = "noems";

char *
serdumpst(Serialport *p, char *buf, int bufsz)
{
	char *e, *s;
	Serial *ser;

	ser = p->s;

	e = buf + bufsz;
	s = seprint(buf, e, "b%d ", p->baud);
	s = seprint(s, e, "c%d ", p->dcd);	/* unimplemented */
	s = seprint(s, e, "d%d ", p->dtr);
	s = seprint(s, e, "e%d ", p->dsr);	/* unimplemented */
	s = seprint(s, e, "l%d ", p->bits);
	s = seprint(s, e, "m%d ", p->mctl);
	if(p->parity >= 0 || p->parity < strlen(pformat))
		s = seprint(s, e, "p%c ", pformat[p->parity]);
	else
		s = seprint(s, e, "p%c ", '?');
	s = seprint(s, e, "r%d ", p->rts);
	s = seprint(s, e, "s%d ", p->stop);
	s = seprint(s, e, "i%d ", p->fifo);
	s = seprint(s, e, "\ndev(%d) ", 0);
	s = seprint(s, e, "type(%d)  ", ser->type);
	s = seprint(s, e, "framing(%d) ", p->nframeerr);
	s = seprint(s, e, "overruns(%d) ", p->novererr);
	s = seprint(s, e, "berr(%d) ", p->nbreakerr);
	s = seprint(s, e, " serr(%d)\n", p->nparityerr);
	return s;
}

static int
serinit(Serialport *p)
{
	int res;
	res = 0;
	Serial *ser;

	ser = p->s;

	if(ser->init != nil)
		res = ser->init(p);
	if(ser->getparam != nil)
		ser->getparam(p);
	p->nframeerr = p->nparityerr = p->nbreakerr = p->novererr = 0;

	return res;
}

static void
dattach(Req *req)
{
	req->fid->qid = (Qid) {0, 0, QTDIR};
	req->ofcall.qid = req->fid->qid;
	respond(req, nil);
}

static int
dirgen(int n, Dir *d, void *)
{
	if(n >= nports * 2)
		return -1;
	d->qid.path = n + 1;
	d->qid.vers = 0;
	if(n >= 0)
		d->qid.type = 0;
	else
		d->qid.type = QTDIR;
	d->uid = strdup("usb");
	d->gid = strdup(d->uid);
	d->muid = strdup(d->uid);
	if(n >= 0){
		d->name = smprint((n & 1) ? "%sctl" : "%s", ports[n/2]->name);
		d->mode = ((n & 1) ? 0664 : 0660);
	}else{
		d->name = strdup("");
		d->mode = 0555 | QTDIR;
	}
	d->atime = d->mtime = time(0);
	d->length = 0;
	return 0;
}

static char *
dwalk(Fid *fid, char *name, Qid *qidp)
{
	int i;
	int len;
	Qid qid;
	char *p;

	qid = fid->qid;
	if((qid.type & QTDIR) == 0){
		return "walk in non-directory";
	}

	if(strcmp(name, "..") == 0){
		fid->qid.path = 0;
		fid->qid.vers = 0;
		fid->qid.type = QTDIR;
		*qidp = fid->qid;
		return nil;
	}

	for(i = 0; i < nports; i++)
		if(strncmp(name, ports[i]->name, len = strlen(ports[i]->name)) == 0){
			p = name + len;
			if(*p == 0)
				fid->qid.path = 2 * i + 1;
			else if(strcmp(p, "ctl") == 0)
				fid->qid.path = 2 * i + 2;
			else
				continue;
			fid->qid.vers = 0;
			fid->qid.type = 0;
			*qidp = fid->qid;
			return nil;
		}
	return "does not exist";
}

static void
dstat(Req *req)
{
	if(dirgen(req->fid->qid.path - 1, &req->d, nil) < 0)
		respond(req, "the front fell off");
	else
		respond(req, nil);
}

enum {
	Serbufsize	= 255,
};

static void
readproc(void *aux)
{
	int dfd;
	Req *req;
	long count, rcount;
	void *data;
	Serial *ser;
	Serialport *p;
	static int errrun, good;
	char err[Serbufsize];

	p = aux;
	ser = p->s;
	for(;;){
		qlock(&p->readq);
		while(p->readfirst == nil)
			rsleep(&p->readrend);
		req = p->readfirst;
		p->readfirst = req->aux;
		if(p->readlast == req)
			p->readlast = nil;
		req->aux = nil;
		qunlock(&p->readq);

		count = req->ifcall.count;
		data = req->ofcall.data;
		qlock(ser);
		if(count > ser->maxread)
			count = ser->maxread;
		dsprint(2, "serial: reading from data\n");
		do {
			err[0] = 0;
			dfd = p->epin->dfd;
			if(usbdebug >= 3)
				dsprint(2, "serial: reading: %ld\n", count);
	
			assert(count > 0);
			if(ser->wait4data != nil)
				rcount = ser->wait4data(p, data, count);
			else{
				qunlock(ser);
				rcount = read(dfd, data, count);
				qlock(ser);
			}
			/*
			 * if we encounter a long run of continuous read
			 * errors, do something drastic so that our caller
			 * doesn't just spin its wheels forever.
			 */
			if(rcount < 0) {
				snprint(err, Serbufsize, "%r");
				++errrun;
				sleep(20);
				if (good > 0 && errrun > 10000) {
					/* the line has been dropped; give up */
					qunlock(ser);
					fprint(2, "%s: line %s is gone: %r\n",
						argv0, p->name);
					threadexitsall("serial line gone");
				}
			} else {
				errrun = 0;
				good++;
			}
			if(usbdebug >= 3)
				dsprint(2, "serial: read: %s %ld\n", err, rcount);
		} while(rcount < 0 && strstr(err, "timed out") != nil);
	
		dsprint(2, "serial: read from bulk %ld, %10.10s\n", rcount, err);
		if(rcount < 0){
			dsprint(2, "serial: need to recover, data read %ld %r\n",
				count);
			serialrecover(ser, p, p->epin, err);
		}
		dsprint(2, "serial: read from bulk %ld\n", rcount);
		if(rcount >= 0){
			req->ofcall.count = rcount;
			respond(req, nil);
		} else
			responderror(req);
		qunlock(ser);
	}
}

static void
dread(Req *req)
{
	char *e;	/* change */
	Qid q;
	Serial *ser;
	vlong offset;
	Serialport *p;
	static char buf[Serbufsize];
	
	q = req->fid->qid;
	
	if(q.path == 0){
		dirread9p(req, dirgen, nil);
		respond(req, nil);
		return;
	}

	p = ports[(q.path - 1) / 2];
	ser = p->s;
	offset = req->ifcall.offset;

	memset(buf, 0, sizeof buf);
	qlock(ser);
	switch((long)((q.path - 1) % 2)){
	case 0:
		qlock(&p->readq);
		if(p->readfirst == nil)
			p->readfirst = req;
		else
			p->readlast->aux = req;
		p->readlast = req;
		rwakeup(&p->readrend);
		qunlock(&p->readq);
		break;
	case 1:
		if(offset == 0) {
			if(!p->isjtag){
				e = serdumpst(p, buf, Serbufsize);
				readbuf(req, buf, e - buf);
			}
		}
		respond(req, nil);
		break;
	}
	qunlock(ser);
}

static long
altwrite(Serialport *p, uchar *buf, long count)
{
	int nw, dfd;
	char err[128];
	Serial *ser;

	ser = p->s;
	do{
		dsprint(2, "serial: write to bulk %ld\n", count);

		if(ser->wait4write != nil)
			/* unlocked inside later */
			nw = ser->wait4write(p, buf, count);
		else{
			dfd = p->epout->dfd;
			qunlock(ser);
			nw = write(dfd, buf, count);
			qlock(ser);
		}
		rerrstr(err, sizeof err);
		dsprint(2, "serial: written %s %d\n", err, nw);
	} while(nw < 0 && strstr(err, "timed out") != nil);

	if(nw != count){
		dsprint(2, "serial: need to recover, status in write %d %r\n",
			nw);
		snprint(err, sizeof err, "%r");
		serialrecover(p->s, p, p->epout, err);
	}
	return nw;
}

static void
dwrite(Req *req)
{
	ulong path;
	char *cmd;
	Serial *ser;
	long count;
	void *buf;
	Serialport *p;

	path = req->fid->qid.path;
	p = ports[(path-1)/2];
	ser = p->s;
	count = req->ifcall.count;
	buf = req->ifcall.data;

	qlock(ser);
	switch((long)((path-1)%2)){
	case 0:
		count = altwrite(p, (uchar *)buf, count);
		break;
	case 1:
		if(p->isjtag)
			break;
		cmd = emallocz(count+1, 1);
		memmove(cmd, buf, count);
		cmd[count] = 0;
		if(serialctl(p, cmd) < 0){
			qunlock(ser);
			free(cmd);
			respond(req, "bad control request");
			return;
		}
		free(cmd);
		break;
	}
	if(count >= 0)
		ser->recover = 0;
	else
		serialrecover(ser, p, p->epout, "writing");
	qunlock(ser);
	if(count >= 0){
		req->ofcall.count = count;
		respond(req, nil);
	} else
		responderror(req);
}

static int
openeps(Serialport *p, int epin, int epout, int epintr)
{
	Serial *ser;

	ser = p->s;
	p->epin = openep(ser->dev, epin);
	if(p->epin == nil){
		fprint(2, "serial: openep %d: %r\n", epin);
		return -1;
	}
	if(epin == epout){
		incref(p->epin);
		p->epout = p->epin;
	} else
		p->epout = openep(ser->dev, epout);
	if(p->epout == nil){
		fprint(2, "serial: openep %d: %r\n", epout);
		closedev(p->epin);
		return -1;
	}

	if(!p->isjtag){
		devctl(p->epin,  "timeout 1000");
		devctl(p->epout, "timeout 1000");
	}

	if(ser->hasepintr){
		p->epintr = openep(ser->dev, epintr);
		if(p->epintr == nil){
			fprint(2, "serial: openep %d: %r\n", epintr);
			closedev(p->epin);
			closedev(p->epout);
			return -1;
		}
		opendevdata(p->epintr, OREAD);
		devctl(p->epintr, "timeout 1000");
	}

	if(ser->seteps!= nil)
		ser->seteps(p);
	if(p->epin == p->epout)
		opendevdata(p->epin, ORDWR);
	else {
		opendevdata(p->epin, OREAD);
		opendevdata(p->epout, OWRITE);
	}
	if(p->epin->dfd < 0 || p->epout->dfd < 0 ||
	    (ser->hasepintr && p->epintr->dfd < 0)){
		fprint(2, "serial: open i/o ep data: %r\n");
		closedev(p->epin);
		closedev(p->epout);
		if(ser->hasepintr)
			closedev(p->epintr);
		return -1;
	}
	return 0;
}

static int
findendpoints(Serial *ser, int ifc)
{
	int i, epin, epout, epintr;
	Ep *ep, **eps;

	epintr = epin = epout = -1;

	/*
	 * interfc 0 means start from the start which is equiv to
	 * iterate through endpoints probably, could be done better
	 */
	eps = ser->dev->usb->conf[0]->iface[ifc]->ep;

	for(i = 0; i < Nep; i++){
		if((ep = eps[i]) == nil)
			continue;
		if(ser->hasepintr && ep->type == Eintr &&
		    ep->dir == Ein && epintr == -1)
			epintr = ep->id;
		if(ep->type == Ebulk){
			if((ep->dir == Ein || ep->dir == Eboth) && epin == -1)
				epin = ep->id;
			if((ep->dir == Eout || ep->dir == Eboth) && epout == -1)
				epout = ep->id;
		}
	}
	dprint(2, "serial[%d]: ep ids: in %d out %d intr %d\n", ifc, epin, epout, epintr);
	if(epin == -1 || epout == -1 || (ser->hasepintr && epintr == -1))
		return -1;

	if(openeps(&ser->p[ifc], epin, epout, epintr) < 0)
		return -1;

	dprint(2, "serial: ep in %s out %s\n", ser->p[ifc].epin->dir, ser->p[ifc].epout->dir);
	if(ser->hasepintr)
		dprint(2, "serial: ep intr %s\n", ser->p[ifc].epintr->dir);

	if(usbdebug > 1 || serialdebug > 2){
		devctl(ser->p[ifc].epin,  "debug 1");
		devctl(ser->p[ifc].epout, "debug 1");
		if(ser->hasepintr)
			devctl(ser->p[ifc].epintr, "debug 1");
		devctl(ser->dev, "debug 1");
	}
	return 0;
}

/* keep in sync with main.c */
static void
usage(void)
{
	fprint(2, "usage: %s [-d] devid\n", argv0);
	threadexitsall("usage");
}

static void
serdevfree(void *a)
{
	Serial *ser = a;
	Serialport *p;
	int i;

	if(ser == nil)
		return;

	for(i = 0; i < ser->nifcs; i++){
		p = &ser->p[i];

		if(ser->hasepintr)
			closedev(p->epintr);
		closedev(p->epin);
		closedev(p->epout);
		p->epintr = p->epin = p->epout = nil;
		if(p->w4data != nil)
			chanfree(p->w4data);
		if(p->gotdata != nil)
			chanfree(p->gotdata);
		if(p->readc)
			chanfree(p->readc);

	}
	free(ser);
}

static Srv serialfs = {
	.attach = dattach,
	.walk1 =	dwalk,
	.read =	dread,
	.write=	dwrite,
	.stat =	dstat,
};

/*
static void
serialfsend(void)
{
	if(p->w4data != nil)
		chanclose(p->w4data);
	if(p->gotdata != nil)
		chanclose(p->gotdata);
	if(p->readc)
		chanclose(p->readc);
}
*/

void
threadmain(int argc, char* argv[])
{
	Serial *ser;
	Dev *dev;
	char buf[50];
	Serialport *p;
	int i;

	ARGBEGIN{
	case 'd':
		serialdebug++;
		break;
	default:
		usage();
	}ARGEND
	if(argc != 1)
		usage();
	dev = getdev(*argv);
	if(dev == nil)
		sysfatal("getdev: %r");

	ser = dev->aux = emallocz(sizeof(Serial), 1);
	ser->maxrtrans = ser->maxwtrans = sizeof ser->p[0].data;
	ser->maxread = ser->maxwrite = sizeof ser->p[0].data;
	ser->dev = dev;
	dev->free = serdevfree;
	ser->jtag = -1;
	ser->nifcs = 1;

	snprint(buf, sizeof buf, "vid %#06x did %#06x",
		dev->usb->vid, dev->usb->did);
	if(plmatch(buf) == 0){
		ser->hasepintr = 1;
		ser->Serialops = plops;
	} else if(uconsmatch(buf) == 0)
		ser->Serialops = uconsops;
	else if(ftmatch(ser, buf) == 0)
		ser->Serialops = ftops;
	else if(slmatch(buf) == 0)
		ser->Serialops = slops;
	else {
		sysfatal("no serial devices found");
	}
	for(i = 0; i < ser->nifcs; i++){
		p = &ser->p[i];
		p->interfc = i;
		p->s = ser;
		if(i == ser->jtag){
			p->isjtag++;
		}
		if(findendpoints(ser, i) < 0)
			sysfatal("no endpoints found for ifc %d", i);
		p->w4data  = chancreate(sizeof(ulong), 0);
		p->gotdata = chancreate(sizeof(ulong), 0);
	}

	qlock(ser);
	serialreset(ser);
	for(i = 0; i < ser->nifcs; i++){
		p = &ser->p[i];
		dprint(2, "serial: valid interface, calling serinit\n");
		if(serinit(p) < 0){
			sysfatal("wserinit: %r");
		}

		dsprint(2, "serial: adding interface %d, %p\n", p->interfc, p);
		if(ser->nifcs == 1)
			snprint(p->name, sizeof p->name, "%s%s", p->isjtag ? "jtag" : "eiaU", dev->hname);
		else
			snprint(p->name, sizeof p->name, "%s%s.%d", p->isjtag ? "jtag" : "eiaU", dev->hname, i);
		fprint(2, "%s...", p->name);
		incref(dev);
		p->readrend.l = &p->readq;
		p->readpid = proccreate(readproc, p, mainstacksize);
		ports = realloc(ports, (nports + 1) * sizeof(Serialport*));
		ports[nports++] = p;
	}

	qunlock(ser);

	if(nports == 0)
		threadexits("no ports");

	snprint(buf, sizeof buf, "%d.serial", dev->id);
	threadpostsharesrv(&serialfs, nil, "usb", buf);
	threadexits(0);
}
