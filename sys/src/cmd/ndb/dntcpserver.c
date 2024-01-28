#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <ip.h>
#include <mp.h>
#include <libsec.h>
#include "dns.h"

enum {
	Maxprocs = 64,
};

static int	readmsg(int, uchar*, int);
static int	reply(int, uchar *, DNSmsg*, Request*, uchar*);
static int	dnzone(int, uchar *, DNSmsg*, DNSmsg*, Request*, uchar*);
static int	tcpannounce(char *mntpt, char *addr, char caller[128], char *cert);

void
dntcpserver(char *mntpt, char *addr, char *cert)
{
	volatile int fd, len, rcode, rv;
	volatile long ms;
	volatile char caller[128];
	volatile uchar pkt[Maxpkt], callip[IPaddrlen];
	volatile DNSmsg reqmsg, repmsg;
	volatile Request req;
	volatile RR *edns;
	char *volatile err;

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

	procsetname("%s: %s server %s",
		mntpt, cert == nil? "tcp": "tls", addr);
	if((fd = tcpannounce(mntpt, addr, caller, cert)) < 0){
		warning("can't announce %s on %s: %r", addr, mntpt);
		_exits(0);
	}
	parseip(callip, caller);
	procsetname("%s: %s server %s serving %s",
		mntpt, cert == nil? "tcp": "tls", addr, caller);

	memset(&req, 0, sizeof req);
	req.isslave = 1;
	req.from = caller;
	req.aborttime = timems() + Maxreqtm;

	/* loop on requests */
	for(;; putactivity(&req)){
		memset(&reqmsg, 0, sizeof reqmsg);
		edns = nil;

		ms = (long)(req.aborttime - nowms);
		if(ms < Minreqtm){
		hangup:
			close(fd);
			_exits(0);
		}
		alarm(ms);
		if(readn(fd, pkt, 2) != 2){
			alarm(0);
			goto hangup;
		}
		len = pkt[0]<<8 | pkt[1];
		if(len <= 0 || len > Maxtcp || readn(fd, pkt+2, len) != len){
			alarm(0);
			goto hangup;
		}
		alarm(0);

		getactivity(&req);
		if((long)(req.aborttime - timems()) < Minreqtm)
			break;

		stats.qrecvdtcp++;

		rcode = Rok;
		err = convM2DNS(pkt+2, len, &reqmsg, &rcode);
		if(err){
			dnslog("%d: server: input err, len %d: %s from %s",
				req.id, len, err, caller);
			free(err);
			break;
		}
		if(rcode == Rok)
			if(reqmsg.qdcount < 1){
				dnslog("%d: server: no questions from %s",
					req.id, caller);
				break;
			} else if(reqmsg.flags & Fresp){
				dnslog("%d: server: reply not request from %s",
					req.id, caller);
				break;
			} else if((reqmsg.flags & Omask) != Oquery){
				dnslog("%d: server: op %d from %s",
					req.id, reqmsg.flags & Omask, caller);
				break;
			}

		if(reqmsg.qd == nil){
			dnslog("%d: server: no question RR from %s",
				req.id, caller);
			break;
		}

		logrequest(req.id, 0, "rcvd", callip, caller,
			reqmsg.qd->owner->name, reqmsg.qd->type);

		if(rcode == Rok)
			if((reqmsg.edns = getednsopt(&reqmsg, &rcode)) != nil)
				edns = mkednsopt();

		/* loop through each question */
		while(reqmsg.qd){
			memset(&repmsg, 0, sizeof(repmsg));
			repmsg.edns = edns;
			if(rcode == Rok && reqmsg.qd->type == Taxfr)
				rv = dnzone(fd, pkt, &reqmsg, &repmsg, &req, callip);
			else {
				dnserver(&reqmsg, &repmsg, &req, callip, rcode);
				rv = reply(fd, pkt, &repmsg, &req, callip);
				freeanswers(&repmsg);
			}
			if(rv < 0)
				goto out;
		}
		rrfreelist(edns);
		rrfreelist(reqmsg.edns);
		freeanswers(&reqmsg);
	}
out:
	close(fd);
	rrfreelist(edns);
	rrfreelist(reqmsg.edns);
	freeanswers(&reqmsg);
	putactivity(&req);
	_exits(0);
}

static int
reply(int fd, uchar *pkt, DNSmsg *rep, Request *req, uchar *callip)
{
	int len, rv;
	long ms;

	/* taking too long */
	ms = (long)(req->aborttime - nowms);
	if(ms < 1)
		return -1;

	logreply(req->id, "send", callip, rep);

	len = convDNS2M(rep, pkt+2, Maxtcp);
	pkt[0] = len>>8;
	pkt[1] = len;
	len += 2;

	alarm(ms);
	rv = write(fd, pkt, len);
	alarm(0);
	if(rv != len){
		dnslog("%d: error sending reply to %I: %r",
			req->id, callip);
		rv = -1;
	}
	return rv;
}

static Server*
findserver(uchar *callip, Server *servers, Request *req)
{
	uchar ip[IPaddrlen];
	RR *list, *rp;

	for(; servers != nil; servers = servers->next){
		if(strcmp(servers->name, "*") == 0)
			return servers;
		if(strcmp(ipattr(servers->name), "ip") == 0){
			if(parseip(ip, servers->name) == -1)
				continue;
			if(ipcmp(callip, ip) == 0)
				return servers;
			continue;
		}
		list = dnresolve(servers->name, Cin,
			isv4(callip)? Ta: Taaaa,
			req, nil, 0, Recurse, 0, nil);
		rrfreelist(rrremneg(&list));
		for(rp = list; rp != nil; rp = rp->next){
			if(parseip(ip, rp->ip->name) == -1)
				continue;
			if(ipcmp(callip, ip) == 0)
				break;
		}
		rrfreelist(list);
		if(rp != nil)
			return servers;
	}
	return nil;
}

static int
dnzone(int fd, uchar *pkt, DNSmsg *reqp, DNSmsg *repp, Request *req, uchar *callip)
{
	DN *dp;
	RR *rp;
	int rv;

	repp->id = reqp->id;
	repp->qd = reqp->qd;
	reqp->qd = reqp->qd->next;
	repp->qd->next = nil;
	repp->flags = Fauth | Fresp | Oquery;
	setercode(repp, Rok);
	dp = repp->qd->owner;

	/* send the soa */
	repp->an = rrlookup(dp, Tsoa, NOneg);
	if(repp->an != nil && !myip(callip)
	&& findserver(callip, repp->an->soa->slaves, req) == nil){
		dnslog("%d: dnzone: %I axfr %s - not a dnsslave",
			req->id, callip, dp->name);
		rrfreelist(repp->an);
		repp->an = nil;
	}
	rv = reply(fd, pkt, repp, req, callip);
	if(rv < 0 || repp->an == nil)
		goto out;
	rrfreelist(repp->an);
	repp->an = nil;

	repp->an = rrgetzone(dp->name);
	while(repp->an != nil) {
		rp = repp->an->next;
		repp->an->next = nil;
		rv = reply(fd, pkt, repp, req, callip);
		rrfreelist(repp->an);
		repp->an = rp;
		if(rv < 0)
			goto out;
	}

	/* resend the soa */
	repp->an = rrlookup(dp, Tsoa, NOneg);
	rv = reply(fd, pkt, repp, req, callip);
out:
	rrfreelist(repp->an);
	repp->an = nil;
	rrfree(repp->qd);
	repp->qd = nil;
	return rv;
}

static int
tcpannounce(char *mntpt, char *addr, char caller[128], char *cert)
{
	char adir[NETPATHLEN], ldir[NETPATHLEN], buf[128];
	int acfd, lcfd, dfd, wfd, rfd, procs;
	PEMChain *chain = nil;

	if(cert != nil){
		chain = readcertchain(cert);
		if(chain == nil)
			return -1;
	}

	/* announce tcp dns port */
	snprint(buf, sizeof(buf), "%s/tcp!%s!%s", mntpt, addr, cert == nil ? "53" : "853");
	acfd = announce(buf, adir);
	if(acfd < 0)
		return -1;

	/* open wait file to maintain child process count */
	snprint(buf, sizeof(buf), "/proc/%d/wait", getpid());
	wfd = open(buf, OREAD|OCEXEC);
	if(wfd < 0){
		close(acfd);
		return -1;
	}
	procs = 0;
	for(;;) {
		if(procs >= Maxprocs || (procs % 8) == 0){
			while(procs > 0){
				if(procs < Maxprocs){
					Dir *d = dirfstat(wfd);
					if(d == nil || d->length == 0){
						free(d);
						break;
					}
					free(d);
				}
				if(read(wfd, buf, sizeof(buf)) <= 0){
					procs = 0;
					break;
				}
				procs--;
			}
		}

		lcfd = listen(adir, ldir);
		if(lcfd < 0){
			close(wfd);
			close(acfd);
			return -1;
		}

		switch(rfork(RFPROC|RFMEM)){
		case -1:
			close(lcfd);
			break;
		case 0:
			dfd = accept(lcfd, ldir);
			close(lcfd);
			if(dfd < 0)
				_exits(0);
			if(chain != nil){
				TLSconn conn;
				int fd;

				memset(&conn, 0, sizeof conn);
				conn.cert = emalloc(conn.certlen = chain->pemlen);
				memmove(conn.cert, chain->pem, conn.certlen);
				conn.chain = chain->next;
				fd = tlsServer(dfd, &conn);
				free(conn.cert);
				free(conn.sessionID);
				if(fd < 0){
					close(dfd);
					_exits(0);
				}
				dfd = fd;
			}
			/* get the callers ip!port */
			memset(caller, 0, 128);
			snprint(buf, sizeof(buf), "%s/remote", ldir);
			if((rfd = open(buf, OREAD|OCEXEC)) >= 0){
				read(rfd, caller, 128-1);
				close(rfd);
			}
			/* child returns */
			return dfd;
		default:
			procs++;
		}
	}
}
