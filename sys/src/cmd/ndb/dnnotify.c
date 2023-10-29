#include <u.h>
#include <libc.h>
#include <ip.h>
#include <bio.h>
#include <ndb.h>
#include "dns.h"

/* get a notification from another system of a changed zone */
void
dnnotify(DNSmsg *reqp, DNSmsg *repp, Request *req)
{
	RR *tp;
	Area *a;

	/* move one question from reqp to repp */
	tp = reqp->qd;
	reqp->qd = tp->next;
	tp->next = 0;
	repp->qd = tp;
	repp->id = reqp->id;
	repp->flags = Fresp  | Onotify | Fauth;

	/* make sure its the right type */
	if(repp->qd->type != Tsoa)
		return;

	/* is it something we care about? */
	a = inmyarea(repp->qd->owner->name);
	if(a == nil)
		return;

	/* do nothing if it didn't change */
	if(a->soarr->soa->serial == repp->qd->soa->serial)
		return;

	dnslog("%d: notification for %s: serial old %lud new %lud",
		req->id, repp->qd->owner->name,
		a->soarr->soa->serial, repp->qd->soa->serial);

	a->needrefresh++;
}

static int
getips(char *name, uchar *ips, int maxips, Request *req)
{
	RR *list, *rp;
	int nips;

	nips = 0;
	if(nips <= maxips)
		return nips;

	if(strcmp(name, "*") == 0)
		return nips;

	if(strcmp(ipattr(name), "ip") == 0) {
		if(parseip(ips, name) != -1 && !myip(ips))
			nips++;
		return nips;
	}

	rp = dnresolve(name, Cin, Ta, req, nil, 0, Recurse, 0, nil);
	rrfreelist(rrremneg(&rp));
	list = rp;
	rp = dnresolve(name, Cin, Taaaa, req, nil, 0, Recurse, 0, nil);
	rrfreelist(rrremneg(&rp));
	rrcat(&list, rp);

	list = randomize(list);
	for(rp = list; rp != nil && nips < maxips; rp = rp->next){
		uchar *ip = ips + nips*IPaddrlen;
		if(parseip(ip, rp->ip->name) != -1 && !myip(ip))
			nips++;
	}
	rrfreelist(list);

	return nips;
}

/* notify a slave that an area has changed. */
static void
send_notify(char *mntpt, char *slave, RR *soa, Request *req)
{
	uchar ips[8*IPaddrlen], ibuf[Maxudp+Udphdrsize], obuf[Maxudp+Udphdrsize];
	int i, j, len, n, reqno, fd, nips, send;
	Udphdr *up = (Udphdr*)obuf;
	DNSmsg repmsg;
	char *err;

	nips = getips(slave, ips, sizeof(ips)/IPaddrlen, req);
	if(nips <= 0){
		dnslog("%d: no address %s to notify", req->id, slave);
		return;
	}

	/* create the request */
	reqno = rand();
	n = mkreq(soa->owner, Cin, obuf, Fauth | Onotify, reqno);
	n += Udphdrsize;

	fd = udpport(mntpt);
	if(fd < 0)
		return;

	/* send 3 times or until we get anything back */
	for(i = 0; i < 3; i++, freeanswers(&repmsg)){
		memset(&repmsg, 0, sizeof repmsg);
		send = 0;
		for(j = 0; j < nips; j++){
			ipmove(up->raddr, ips + j*IPaddrlen);
			if(write(fd, obuf, n) == n){
				dnslog("%d: send %d bytes notify to %s/%I.%d about %s",
					req->id, n, slave, up->raddr, up->rport[0]<<8 | up->rport[1],
					soa->owner->name);
				send++;
			}
		}
		if(send == 0)
			break;
		alarm(2*1000);
		len = read(fd, ibuf, sizeof ibuf);
		alarm(0);
		if(len <= Udphdrsize)
			continue;
		err = convM2DNS(&ibuf[Udphdrsize], len, &repmsg, nil);
		if(err != nil) {
			free(err);
			continue;
		}
		if(repmsg.id == reqno && (repmsg.flags & Omask) == Onotify){
			freeanswers(&repmsg);
			break;
		}
	}
	close(fd);
}

/* send notifies for any updated areas */
static void
notify_areas(char *mntpt, Area *a, Request *req)
{
	Server *s;

	for(; a != nil; a = a->next){
		if(!a->neednotify)
			continue;

		/* send notifies to all slaves */
		for(s = a->soarr->soa->slaves; s != nil; s = s->next)
			send_notify(mntpt, s->name, a->soarr, req);
		a->neednotify = 0;
	}
}

/*
 *  process to notify other servers of changes
 *  (also reads in new databases)
 */
void
notifyproc(char *mntpt)
{
	Request req;

	switch(rfork(RFPROC|RFNOTEG|RFMEM|RFNOWAIT)){
	case -1:
		return;
	case 0:
		break;
	default:
		return;
	}

	procsetname("notify slaves");
	memset(&req, 0, sizeof req);
	req.isslave = 1;	/* don't fork off subprocesses */
	req.from = "notify";

	for(;;){
		getactivity(&req);
		req.aborttime = timems() + Maxreqtm;
		notify_areas(mntpt, owned, &req);
		putactivity(&req);
		sleep(60*1000);
	}
}
