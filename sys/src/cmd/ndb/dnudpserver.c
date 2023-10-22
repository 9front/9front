#include <u.h>
#include <libc.h>
#include <ip.h>
#include "dns.h"

enum {
	Logqueries = 0,
};

static int	udpannounce(char*, char*);
static void	reply(int, uchar*, DNSmsg*, Request*);

typedef struct Inprogress Inprogress;
struct Inprogress
{
	int	inuse;
	Udphdr	uh;
	DN	*owner;
	ushort	type;
	ushort	id;
};
Inprogress inprog[Maxactive+2];

typedef struct Forwtarg Forwtarg;
struct Forwtarg {
	char	*host;
	uchar	addr[IPaddrlen];
	int	fd;
	ulong	lastdial;
};
Forwtarg forwtarg[10];
int forwtcount;

/*
 *  record client id and ignore retransmissions.
 *  we're still single thread at this point.
 */
static Inprogress*
clientrxmit(DNSmsg *req, uchar *buf)
{
	Inprogress *p, *empty;
	Udphdr *uh;

	uh = (Udphdr *)buf;
	empty = nil;
	for(p = inprog; p < &inprog[Maxactive]; p++){
		if(p->inuse == 0){
			if(empty == nil)
				empty = p;
			continue;
		}
		if(req->id == p->id)
		if(req->qd->owner == p->owner)
		if(req->qd->type == p->type)
		if(memcmp(uh, &p->uh, Udphdrsize) == 0)
			return nil;
	}
	if(empty == nil)
		return nil; /* shouldn't happen: see slave() & Maxactive def'n */

	empty->id = req->id;
	empty->owner = req->qd->owner;
	empty->type = req->qd->type;
	if (empty->type != req->qd->type)
		dnslog("clientrxmit: bogus req->qd->type %d", req->qd->type);
	memmove(&empty->uh, uh, Udphdrsize);
	empty->inuse = 1;
	return empty;
}

int
addforwtarg(char *host)
{
	Forwtarg *tp;

	if (forwtcount >= nelem(forwtarg)) {
		dnslog("too many forwarding targets");
		return -1;
	}
	tp = forwtarg + forwtcount;
	if(parseip(tp->addr, host) == -1) {
		dnslog("can't parse ip %s", host);
		return -1;
	}
	tp->lastdial = time(nil);
	tp->fd = udpport(mntpt);
	if (tp->fd < 0)
		return -1;

	free(tp->host);
	tp->host = estrdup(host);
	forwtcount++;
	return 0;
}

/*
 * fast forwarding of incoming queries to other dns servers.
 * intended primarily for debugging.
 */
static void
redistrib(uchar *buf, int len)
{
	uchar save[Udphdrsize];
	Forwtarg *tp;
	Udphdr *uh;

	memmove(save, buf, Udphdrsize);

	uh = (Udphdr *)buf;
	for (tp = forwtarg; tp < forwtarg + forwtcount; tp++)
		if (tp->fd >= 0) {
			memmove(uh->raddr, tp->addr, sizeof tp->addr);
			hnputs(uh->rport, 53);		/* dns port */
			if (write(tp->fd, buf, len) != len) {
				close(tp->fd);
				tp->fd = -1;
			}
		} else if (tp->host && time(nil) - tp->lastdial > 60) {
			tp->lastdial = time(nil);
			tp->fd = udpport(mntpt);
		}

	memmove(buf, save, Udphdrsize);
}

/*
 *  a process to act as a dns server for outside reqeusts
 */
void
dnudpserver(char *mntpt, char *addr)
{
	volatile int fd, len, op, rcode, served;
	char *volatile err;
	volatile char tname[32], ipstr[64];
	volatile uchar buf[Udphdrsize + Maxudp + 1024];
	volatile DNSmsg reqmsg, repmsg;
	Inprogress *volatile p;
	volatile Request req;
	Udphdr *volatile uh;

	/*
	 * fork sharing text, data, and bss with parent.
	 * stay in the same note group.
	 */
	switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		break;
	case 0:
		break;
	default:
		return;
	}

	served = 0;
restart:
	procsetname("%s: udp server announcing %s", mntpt, addr);
	if((fd = udpannounce(mntpt, addr)) < 0){
		warning("can't announce %s on %s: %r", addr, mntpt);
		do {
			sleep(5000);
		} while((fd = udpannounce(mntpt, addr)) < 0);
	}

	memset(&req, 0, sizeof req);
	setjmp(req.mret);
	req.isslave = 0;

	/* loop on requests */
	for(;; putactivity(&req)){
		procsetname("%s: udp server %s: served %d", mntpt, addr, served);
		memset(&repmsg, 0, sizeof repmsg);
		memset(&reqmsg, 0, sizeof reqmsg);

		alarm(60*1000);
		len = read(fd, buf, sizeof buf);
		alarm(0);
		if(len <= Udphdrsize){
			close(fd);
			goto restart;
		}

		if(forwtcount > 0)
			redistrib(buf, len);

		uh = (Udphdr*)buf;
		len -= Udphdrsize;

		// dnslog("read received UDP from %I to %I", uh->raddr, uh->laddr);
		snprint(ipstr, sizeof(ipstr), "%I", uh->raddr);
		getactivity(&req);
		req.aborttime = timems() + Maxreqtm;
		req.from = ipstr;

		served++;
		stats.qrecvdudp++;

		rcode = 0;
		err = convM2DNS(&buf[Udphdrsize], len, &reqmsg, &rcode);
		if(err){
			/* first bytes in buf are source IP addr */
			dnslog("server: input error: %s from %I", err, buf);
			free(err);
			goto freereq;
		}
		if (rcode == 0)
			if(reqmsg.qdcount < 1){
				dnslog("server: no questions from %I", buf);
				goto freereq;
			} else if(reqmsg.flags & Fresp){
				dnslog("server: reply not request from %I", buf);
				goto freereq;
			}
		op = reqmsg.flags & Omask;
		if(op != Oquery && op != Onotify){
			dnslog("server: op %d from %I", reqmsg.flags & Omask, buf);
			goto freereq;
		}

		if(reqmsg.qd == nil){
			dnslog("server: no question RR from %I", buf);
			goto freereq;
		}

		if(debug || (trace && subsume(trace, reqmsg.qd->owner->name)))
			dnslog("%d: serve (%I/%d) %d %s %s",
				req.id, buf, uh->rport[0]<<8 | uh->rport[1],
				reqmsg.id, reqmsg.qd->owner->name,
				rrname(reqmsg.qd->type, tname, sizeof tname));

		p = clientrxmit(&reqmsg, buf);
		if(p == nil){
			if(debug)
				dnslog("%d: duplicate", req.id);
			goto freereq;
		}

		if (Logqueries) {
			RR *rr;

			for (rr = reqmsg.qd; rr; rr = rr->next)
				syslog(0, "dnsq", "id %d: (%I/%d) %d %s %s",
					req.id, buf, uh->rport[0]<<8 |
					uh->rport[1], reqmsg.id,
					reqmsg.qd->owner->name,
					rrname(reqmsg.qd->type, tname,
					sizeof tname));
		}
		/* loop through each question */
		while(reqmsg.qd){
			memset(&repmsg, 0, sizeof repmsg);
			switch(op){
			case Oquery:
				dnserver(&reqmsg, &repmsg, &req, buf, rcode);
				break;
			case Onotify:
				dnnotify(&reqmsg, &repmsg, &req);
				break;
			}
			/* send reply on fd to address in buf's udp hdr */
			reply(fd, buf, &repmsg, &req);
			freeanswers(&repmsg);
		}

		p->inuse = 0;
freereq:
		freeanswers(&reqmsg);
		if(req.isslave){
			putactivity(&req);
			_exits(0);
		}
	}
}

/*
 *  announce on well-known dns udp port and set message style interface
 */
static int
udpannounce(char *mntpt, char *addr)
{
	static char hmsg[] = "headers";
	static char imsg[] = "ignoreadvice";

	char dir[64], datafile[64+6];
	int data, ctl;

	snprint(datafile, sizeof(datafile), "%s/udp!%s!dns", mntpt, addr);
	ctl = announce(datafile, dir);
	if(ctl < 0)
		return -1;

	/* turn on header style interface */
	if(write(ctl, hmsg, sizeof(hmsg)-1) < 0){
		warning("can't enable %s on %s: %r", hmsg, datafile);
		close(ctl);
		return -1;
	}

	/* ignore ICMP advice */
	write(ctl, imsg, sizeof(imsg)-1);

	snprint(datafile, sizeof(datafile), "%s/data", dir);
	data = open(datafile, ORDWR);
	if(data < 0)
		warning("can't open udp port %s: %r", datafile);
	close(ctl);
	return data;
}

static void
reply(int fd, uchar *buf, DNSmsg *rep, Request *reqp)
{
	int len;
	char tname[32];

	if(debug || (trace && subsume(trace, rep->qd->owner->name)))
		dnslog("%d: reply (%I/%d) %d %s %s qd %R an %R ns %R ar %R",
			reqp->id, buf, buf[4]<<8 | buf[5],
			rep->id, rep->qd->owner->name,
			rrname(rep->qd->type, tname, sizeof tname),
			rep->qd, rep->an, rep->ns, rep->ar);

	len = convDNS2M(rep, &buf[Udphdrsize], Maxudp);
	len += Udphdrsize;
	if(write(fd, buf, len) != len)
		dnslog("error sending reply: %r");
}
