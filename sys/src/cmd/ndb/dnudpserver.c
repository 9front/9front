#include <u.h>
#include <libc.h>
#include <ip.h>
#include "dns.h"

static int	udpannounce(char*, char*);
static void	reply(int, uchar*, int, DNSmsg*, Request*);

typedef struct Inprogress Inprogress;
struct Inprogress
{
	int	inuse;
	Udphdr	uh;
	DN	*owner;
	ushort	type;
	ushort	id;
};

static Inprogress inprog[Maxactive+2];

/*
 *  record client id and ignore retransmissions.
 *  we're still single thread at this point.
 */
static Inprogress*
clientrxmit(DNSmsg *mp, Udphdr *uh, Request *req)
{
	Inprogress *p, *empty;

	empty = nil;
	for(p = inprog; p < &inprog[Maxactive]; p++){
		if(p->inuse == 0){
			if(empty == nil)
				empty = p;
			continue;
		}
		if(mp->id == p->id)
		if(mp->qd->owner == p->owner)
		if(mp->qd->type == p->type)
		if(memcmp(uh, &p->uh, Udphdrsize) == 0)
			return nil;
	}
	if(empty == nil)
		return nil; /* shouldn't happen: see slave() & Maxactive def'n */

	empty->id = mp->id;
	empty->owner = mp->qd->owner;
	empty->type = mp->qd->type;
	if(empty->type != mp->qd->type)
		dnslog("%d: clientrxmit: bogus req->qd->type %d", req->id, mp->qd->type);
	memmove(&empty->uh, uh, Udphdrsize);
	empty->inuse = 1;
	return empty;
}

/*
 *  a process to act as a dns server for outside reqeusts
 */
void
dnudpserver(char *mntpt, char *addr)
{
	volatile int fd, len, op, rcode, served;
	char *volatile err;
	volatile char caller[64];
	volatile uchar pkt[Udphdrsize + Maxudp];
	volatile DNSmsg reqmsg, repmsg;
	Inprogress *volatile p;
	volatile RR *edns;
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
		memset(&reqmsg, 0, sizeof reqmsg);
		edns = nil;

		procsetname("%s: udp server %s: served %d", mntpt, addr, served);

		len = read(fd, pkt, sizeof pkt);
		if(len <= Udphdrsize){
			close(fd);
			goto restart;
		}

		uh = (Udphdr*)pkt;
		len -= Udphdrsize;

		snprint(caller, sizeof(caller), "%I", uh->raddr);
		getactivity(&req);
		req.aborttime = timems() + Maxreqtm;
		req.from = caller;

		served++;
		stats.qrecvdudp++;

		rcode = Rok;
		err = convM2DNS(&pkt[Udphdrsize], len, &reqmsg, &rcode);
		if(err){
			/* first bytes in buf are source IP addr */
			dnslog("%d: server: input err, len %d: %s from %s",
				req.id, len, err, caller);
			free(err);
			goto freereq;
		}
		if(rcode == Rok)
			if(reqmsg.qdcount < 1){
				dnslog("%d: server: no questions from %s",
					req.id, caller);
				goto freereq;
			} else if(reqmsg.flags & Fresp){
				dnslog("%d: server: reply not request from %s",
					req.id, caller);
				goto freereq;
			}
		op = reqmsg.flags & Omask;
		if(op != Oquery && op != Onotify){
			dnslog("%d: server: op %d from %s",
				req.id, reqmsg.flags & Omask, caller);
			goto freereq;
		}

		if(reqmsg.qd == nil){
			dnslog("%d: server: no question RR from %s",
				req.id, caller);
			goto freereq;
		}

		p = clientrxmit(&reqmsg, uh, &req);
		if(p == nil)
			goto freereq;

		logrequest(req.id, 0, "rcvd", uh->raddr, caller,
			reqmsg.qd->owner->name, reqmsg.qd->type);

		/* determine response size */
		len = 512;	/* default */
		if(rcode == Rok)
			if((reqmsg.edns = getednsopt(&reqmsg, &rcode)) != nil){
				edns = mkednsopt();
				len = Maxudp;
				if(edns->udpsize < len)
					len = edns->udpsize;
				if(reqmsg.edns->udpsize < len)
					len = reqmsg.edns->udpsize;
			}

		/* loop through each question */
		while(reqmsg.qd){
			memset(&repmsg, 0, sizeof repmsg);
			repmsg.edns = edns;
			if(rcode == Rok && op == Onotify)
				dnnotify(&reqmsg, &repmsg, &req);
			else
				dnserver(&reqmsg, &repmsg, &req, uh->raddr, rcode);
			reply(fd, pkt, len, &repmsg, &req);
			freeanswers(&repmsg);
		}
		rrfreelist(edns);

		p->inuse = 0;
freereq:
		rrfreelist(reqmsg.edns);
		freeanswers(&reqmsg);
		if(req.isslave){
			putactivity(&req);
			_exits(0);
		}
	}
}

static void
reply(int fd, uchar *pkt, int len, DNSmsg *rep, Request *req)
{
	logreply(req->id, "send", pkt, rep);

	len = convDNS2M(rep, &pkt[Udphdrsize], len);
	len += Udphdrsize;
	if(write(fd, pkt, len) != len)
		dnslog("%d: error sending reply to %I: %r", req->id, pkt);
}

/*
 *  announce on well-known dns udp port and set message style interface
 */
static int
udpannounce(char *mntpt, char *addr)
{
	static char hmsg[] = "headers";
	static char imsg[] = "ignoreadvice";

	char adir[NETPATHLEN], buf[NETPATHLEN];
	int data, ctl;

	snprint(buf, sizeof(buf), "%s/udp!%s!53", mntpt, addr);
	ctl = announce(buf, adir);
	if(ctl < 0)
		return -1;

	/* turn on header style interface */
	if(write(ctl, hmsg, sizeof(hmsg)-1) < 0){
		warning("can't enable %s on %s: %r", hmsg, adir);
		close(ctl);
		return -1;
	}

	/* ignore ICMP advice */
	write(ctl, imsg, sizeof(imsg)-1);

	snprint(buf, sizeof(buf), "%s/data", adir);
	data = open(buf, ORDWR|OCEXEC);
	if(data < 0)
		warning("can't open udp port %s: %r", adir);
	close(ctl);
	return data;
}
