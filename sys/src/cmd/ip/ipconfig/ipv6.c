/*
 * ipconfig for IPv6
 *	RS means Router Solicitation
 *	RA means Router Advertisement
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ip.h>
#include "ipconfig.h"
#include "../icmp.h"

#pragma varargck argpos ralog 1

#define RALOG "v6routeradv"

#define NetS(x) (((uchar*)x)[0]<< 8 | ((uchar*)x)[1])
#define NetL(x) (((uchar*)x)[0]<<24 | ((uchar*)x)[1]<<16 | \
		 ((uchar*)x)[2]<< 8 | ((uchar*)x)[3])

enum {
	ICMP6LEN=	4,
};

typedef struct Hdr Hdr;
struct Hdr			/* ICMP v4 & v6 header */
{
	uchar	type;
	uchar	code;
	uchar	cksum[2];	/* Checksum */
	uchar	data[];
};

char *icmpmsg6[Maxtype6+1] =
{
[EchoReply]		"EchoReply",
[UnreachableV6]		"UnreachableV6",
[PacketTooBigV6]	"PacketTooBigV6",
[TimeExceedV6]		"TimeExceedV6",
[Redirect]		"Redirect",
[EchoRequest]		"EchoRequest",
[TimeExceed]		"TimeExceed",
[InParmProblem]		"InParmProblem",
[Timestamp]		"Timestamp",
[TimestampReply]	"TimestampReply",
[InfoRequest]		"InfoRequest",
[InfoReply]		"InfoReply",
[AddrMaskRequest]	"AddrMaskRequest",
[AddrMaskReply]		"AddrMaskReply",
[EchoRequestV6]		"EchoRequestV6",
[EchoReplyV6]		"EchoReplyV6",
[RouterSolicit]		"RouterSolicit",
[RouterAdvert]		"RouterAdvert",
[NbrSolicit]		"NbrSolicit",
[NbrAdvert]		"NbrAdvert",
[RedirectV6]		"RedirectV6",
};

static char *icmp6opts[] =
{
[0]			"unknown option",
[V6nd_srclladdr]	"srcll_addr",
[V6nd_targlladdr]	"targll_addr",
[V6nd_pfxinfo]		"prefix",
[V6nd_redirhdr]		"redirect",
[V6nd_mtu]		"mtu",
[V6nd_home]		"home",
[V6nd_srcaddrs]		"src_addrs",
[V6nd_ip]		"ip",
[V6nd_rdns]		"rdns",
[V6nd_9fs]		"9fs",
[V6nd_9auth]		"9auth",
};

uchar v6allroutersL[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x02
};

uchar v6allnodesL[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01
};

uchar v6Unspecified[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};

uchar v6loopback[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 1
};

uchar v6glunicast[IPaddrlen] = {
	0x08, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};

uchar v6linklocal[IPaddrlen] = {
	0xfe, 0x80, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};

uchar v6solpfx[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 1,
	/* last 3 bytes filled with low-order bytes of addr being solicited */
	0xff, 0, 0, 0,
};

uchar v6defmask[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0, 0, 0, 0,
	0, 0, 0, 0
};

enum
{
	Vadd,
	Vremove,
	Vunbind,
	Vaddpref6,
	Vra6,
};

static void
ralog(char *fmt, ...)
{
	char msg[512];
	va_list arg;

	va_start(arg, fmt);
	vseprint(msg, msg+sizeof msg, fmt, arg);
	va_end(arg);
	syslog(debug, RALOG, msg);
}

void
ea2lla(uchar *lla, uchar *ea)
{
	assert(IPaddrlen == 16);
	memset(lla, 0, IPaddrlen);
	lla[0]  = 0xFE;
	lla[1]  = 0x80;
	lla[8]  = ea[0] ^ 0x2;
	lla[9]  = ea[1];
	lla[10] = ea[2];
	lla[11] = 0xFF;
	lla[12] = 0xFE;
	lla[13] = ea[3];
	lla[14] = ea[4];
	lla[15] = ea[5];
}

void
ipv62smcast(uchar *smcast, uchar *a)
{
	assert(IPaddrlen == 16);
	memset(smcast, 0, IPaddrlen);
	smcast[0]  = 0xFF;
	smcast[1]  = 0x02;
	smcast[11] = 0x1;
	smcast[12] = 0xFF;
	smcast[13] = a[13];
	smcast[14] = a[14];
	smcast[15] = a[15];
}

void
v6paraminit(Conf *cf)
{
	cf->sendra = cf->recvra = 0;
	cf->mflag = 0;
	cf->oflag = 0;
	cf->maxraint = Maxv6initraintvl;
	cf->minraint = Maxv6initraintvl / 4;
	cf->linkmtu = 1500;
	cf->reachtime = V6reachabletime;
	cf->rxmitra = V6retranstimer;
	cf->ttl = MAXTTL;

	cf->routerlt = 0;

	cf->prefixlen = 64;
	cf->onlink = 0;
	cf->autoflag = 0;
	cf->validlt = cf->preflt = ~0L;
}

static char *
optname(unsigned opt)
{
	static char buf[32];

	if(opt >= nelem(icmp6opts) || icmp6opts[opt] == nil) {
		snprint(buf, sizeof buf, "unknown option %d", opt);
		return buf;
	} else
		return icmp6opts[opt];
}

static char*
opt_seprint(uchar *ps, uchar *pe, char *sps, char *spe)
{
	int otype, osz, pktlen;
	uchar *a;
	char *p = sps, *e = spe;

	a = ps;
	for (pktlen = pe - ps; pktlen > 0; pktlen -= osz) {
		otype = a[0];
		osz = a[1] * 8;

		switch (otype) {
		default:
			return seprint(p, e, " option=%s ", optname(otype));
		case V6nd_srclladdr:
		case V6nd_targlladdr:
			if(pktlen < osz || osz != 8)
				return seprint(p, e, " option=%s bad size=%d",
					optname(otype), osz);
			p = seprint(p, e, " option=%s maddr=%E", optname(otype),
				a+2);
			break;
		case V6nd_pfxinfo:
			if(pktlen < osz || osz != 32)
				return seprint(p, e, " option=%s: bad size=%d",
					optname(otype), osz);

			p = seprint(p, e, " option=%s pref=%I preflen=%3.3d"
				" lflag=%1.1d aflag=%1.1d unused1=%1.1d"
				" validlt=%ud preflt=%ud unused2=%1.1d",
				optname(otype), a+16, (int)(*(a+2)),
				(*(a+3) & (1 << 7)) != 0,
				(*(a+3) & (1 << 6)) != 0,
				(*(a+3) & 63) != 0,
				NetL(a+4), NetL(a+8), NetL(a+12)!=0);
			break;
		}
		a += osz;
	}
	return p;
}

static void
catch(void *a, char *msg)
{
	USED(a);
	if(strstr(msg, "alarm"))
		noted(NCONT);
	else
		noted(NDFLT);
}

static int
dialicmpv6(uchar *ip, int port)
{
	char addr[128], local[128];
	int fd, cfd;

	snprint(addr, sizeof(addr), "%s/icmpv6!%I!%d!r", conf.mpoint, ip, port);
	snprint(local, sizeof(local), "%I!%d", conf.laddr, port);
	if((fd = dial(addr, local, nil, &cfd)) < 0)
		sysfatal("dialicmp6: %r");
	fprint(cfd, "headers");
	fprint(cfd, "ignoreadvice");
	if(ISIPV6MCAST(ip))
		fprint(cfd, "addmulti %I", conf.laddr);
	close(cfd);
	return fd;
}

static int
arpenter(uchar *ip, uchar *mac)
{
	char buf[256];
	int fd, n;

	if(!validip(ip))
		return 0;
	snprint(buf, sizeof buf, "%s/arp", conf.mpoint);
	if((fd = open(buf, OWRITE)) < 0){
		warning("couldn't open %s: %r", buf);
		return -1;
	}
	n = snprint(buf, sizeof buf, "add %s %I %E %I\n", conf.type, ip, mac, conf.laddr);
	if(write(fd, buf, n) != n) {
		warning("arpenter: %s: %r", buf);
		close(fd);
		return 0;
	}
	close(fd);
	return 1;
}

static int
arpcheck(uchar *ip)
{
	char buf[256], *f[5], *p;
	uchar addr[IPaddrlen];
	Biobuf *bp;
	int rv;

	snprint(buf, sizeof buf, "%s/arp", conf.mpoint);
	bp = Bopen(buf, OREAD);
	if(bp == nil){
		warning("couldn't open %s: %r", buf);
		return -1;
	}
	rv = 0;
	while((p = Brdline(bp, '\n')) != nil){
		p[Blinelen(bp)-1] = 0;
		if(tokenize(p, f, nelem(f)) < 3)
			continue;
		if(parseip(addr, f[2]) != -1)
			continue;
		if(ipcmp(addr, ip) == 0){
			rv = 1;
			break;
		}
	}
	Bterm(bp);
	return rv;
}

static int
masklen(uchar *mask)
{
	int len;

	for(len=0; len < 128; len += 8){
		if(*mask != 255)
			break;
		mask++;
	}
	while(len < 128 && (*mask & (0x80 >> (len & 7))) != 0)
		len++;
	return len;
}

static void
genipmkask(uchar *mask, int len)
{
	memset(mask, 0, IPaddrlen);
	if(len < 0)
		len = 0;
	else if(len > 128)
		len = 128;
	for(; len >= 8; len -= 8)
		*mask++ = 255;
	if(len > 0)
		*mask = ~((1<<(8-len))-1);
}

/* add ipv6 addr to an interface */
int
ip6cfg(void)
{
	int tentative, n;
	char buf[256];

	if(!validip(conf.laddr) || isv4(conf.laddr))
		return -1;

	tentative = dupl_disc;

Again:
	if(tentative)
		n = sprint(buf, "try");
	else
		n = sprint(buf, "add");

	n += snprint(buf+n, sizeof buf-n, " %I", conf.laddr);
	if(!validip(conf.mask))
		ipmove(conf.mask, v6defmask);
	n += snprint(buf+n, sizeof buf-n, " %M", conf.mask);
	if(validip(conf.raddr)){
		n += snprint(buf+n, sizeof buf-n, " %I", conf.raddr);
		if(conf.mtu != 0)
			n += snprint(buf+n, sizeof buf-n, " %d", conf.mtu);
	}

	if(write(conf.cfd, buf, n) < 0){
		warning("write(%s): %r", buf);
		return -1;
	}

	if(!tentative){
		if(validip(conf.gaddr) && !isv4(conf.gaddr))
			adddefroute(conf.gaddr, conf.laddr, conf.laddr, conf.mask);
		return 0;
	}

	sleep(1000);

	if(arpcheck(conf.laddr) <= 0) {
		tentative = 0;
		goto Again;
	}

	warning("found dup entry in arp cache");
	doremove();
	return 0;
}

static int
recvra6on(Ipifc *ifc)
{
	if(ifc == nil)
		return 0;
	else if(ifc->sendra6 > 0)
		return IsRouter;
	else if(ifc->recvra6 > 0)
		return IsHostRecv;
	else
		return IsHostNoRecv;
}

static int
findllip(uchar *ip, Ipifc *ifc)
{
	Iplifc *lifc;

	for(lifc = ifc->lifc; lifc != nil; lifc = lifc->next){
		if(ISIPV6LINKLOCAL(lifc->ip)){
			ipmove(ip, lifc->ip);
			return 1;
		}
	}
	ipmove(ip, v6Unspecified);
	return 0;
}

static void
sendrs(int fd, uchar *dst)
{
	Routersol *rs;
	Lladdropt *llao;
	uchar buf[1024];
	int pktlen;

	memset(buf, 0, sizeof buf);

	rs = (Routersol *)buf;
	rs->type = ICMP6_RS;
	ipmove(rs->dst, dst);
	ipmove(rs->src, conf.laddr);
	pktlen = sizeof *rs;

	if(conf.hwalen > 0){
		llao = (Lladdropt *)&buf[pktlen];
		llao->type = V6nd_srclladdr;
		llao->len = (2+7+conf.hwalen)/8;
		memmove(llao->lladdr, conf.hwa, conf.hwalen);
		pktlen += 8 * llao->len;
	}

	if(write(fd, rs, pktlen) != pktlen)
		ralog("sendrs: write failed, pkt size %d", pktlen);
	else
		ralog("sendrs: sent solicitation to %I from %I on %s",
			rs->dst, rs->src, conf.dev);
}

/*
 * a router receiving a router adv from another
 * router calls this; it is basically supposed to
 * log the information in the ra and raise a flag
 * if any parameter value is different from its configured values.
 *
 * doing nothing for now since I don't know where to log this yet.
 */
static void
recvrarouter(uchar buf[], int pktlen)
{
	USED(buf, pktlen);
}

/* host receiving a router advertisement calls this */

static void
ewrite(int fd, char *str)
{
	int n;

	if(fd < 0)
		return;

	n = strlen(str);
	if(write(fd, str, n) != n)
		ralog("write(%s) failed: %r", str);
}

static void
issuebasera6(Conf *cf)
{
	char *cfg;

	cfg = smprint("ra6 mflag %d oflag %d reachtime %d rxmitra %d "
		"ttl %d routerlt %d",
		cf->mflag, cf->oflag, cf->reachtime, cf->rxmitra,
		cf->ttl, cf->routerlt);
	ewrite(cf->cfd, cfg);
	free(cfg);
}

static void
issuerara6(Conf *cf)
{
	char *cfg;

	cfg = smprint("ra6 sendra %d recvra %d maxraint %d minraint %d "
		"linkmtu %d",
		cf->sendra, cf->recvra, cf->maxraint, cf->minraint,
		cf->linkmtu);
	ewrite(cf->cfd, cfg);
	free(cfg);
}

static void
issueadd6(Conf *cf)
{
	char *cfg;

	cfg = smprint("add6 %I %d %d %d %lud %lud", cf->v6pref, cf->prefixlen,
		cf->onlink, cf->autoflag, cf->validlt, cf->preflt);
	ewrite(cf->cfd, cfg);
	free(cfg);
}

static void
recvrahost(uchar buf[], int pktlen)
{
	int m, n, optype;
	uchar src[IPaddrlen];
	Lladdropt *llao;
	Mtuopt *mtuo;
	Prefixopt *prfo;
	Routeradv *ra;
	static int first = 1;

	m = sizeof *ra;
	ra = (Routeradv*)buf;
	if(pktlen < m)
		return;

	if(!ISIPV6LINKLOCAL(ra->src))
		return;

	conf.ttl = ra->cttl;
	conf.mflag = (MFMASK & ra->mor);
	conf.oflag = (OCMASK & ra->mor);
	conf.routerlt =  nhgets(ra->routerlt);
	conf.reachtime = nhgetl(ra->rchbltime);
	conf.rxmitra =   nhgetl(ra->rxmtimer);
	issuebasera6(&conf);

	while(pktlen - m >= 8) {
		n = m;
		optype = buf[n];
		m += 8 * buf[n+1];
		if(pktlen < m)
			return;

		switch (optype) {
		case V6nd_srclladdr:
			llao = (Lladdropt *)&buf[n];
			if(llao->len == 1 && conf.hwalen == 6)
				arpenter(ra->src, llao->lladdr);
			break;
		case V6nd_mtu:
			mtuo = (Mtuopt*)&buf[n];
			conf.linkmtu = nhgetl(mtuo->mtu);
			break;
		case V6nd_pfxinfo:
			prfo = (Prefixopt*)&buf[n];
			if(prfo->len != 4) {
				ralog("illegal len (%d) for prefix option", prfo->len);
				return;
			}
			if((prfo->plen & 127) == 0
			|| !validip(prfo->pref)
			|| isv4(prfo->pref)
			|| ipcmp(prfo->pref, v6loopback) == 0
			|| ISIPV6MCAST(prfo->pref)
			|| ISIPV6LINKLOCAL(prfo->pref)){
				ralog("igoring bogus prefix from %I on %s; pfx %I /%d",
					ra->src, conf.dev, prfo->pref, prfo->plen);
				break;
			}
			if(first) {
				first = 0;
				ralog("got initial RA from %I on %s; pfx %I /%d",
					ra->src, conf.dev, prfo->pref, prfo->plen);
			}
			conf.prefixlen = prfo->plen & 127;
			genipmkask(conf.mask, conf.prefixlen);
			maskip(prfo->pref, conf.mask, conf.v6pref);
			conf.onlink =   ((prfo->lar & OLMASK) != 0);
			conf.autoflag = ((prfo->lar & AFMASK) != 0);
			conf.validlt = nhgetl(prfo->validlt);
			conf.preflt =  nhgetl(prfo->preflt);
			issueadd6(&conf);
			
			if(conf.routerlt == 0)
				break;
			if((prfo->lar & RFMASK) != 0)
				ipmove(conf.gaddr, prfo->pref);
			else
				ipmove(conf.gaddr, ra->src);

			memmove(src, conf.v6pref, 8);
			memmove(src+8, conf.laddr+8, 8);
			adddefroute(conf.gaddr, conf.laddr, src, conf.mask);
			break;
		}
	}
}

/*
 * daemon to receive router advertisements from routers
 */
void
recvra6(void)
{
	int fd, n, sendrscnt, recvracnt, sleepfor;
	uchar buf[4096];
	Ipifc *ifc;

	ifc = readipifc(conf.mpoint, nil, myifc);
	if(ifc == nil)
		sysfatal("can't read ipifc: %r");

	if(!findllip(conf.laddr, ifc))
		sysfatal("no link local address");

	fd = dialicmpv6(v6allnodesL, ICMP6_RA);
	if(fd < 0)
		sysfatal("can't open icmp_ra connection: %r");

	notify(catch);
	sendrscnt = Maxv6rss;
	recvracnt = 0;

	switch(rfork(RFPROC|RFMEM|RFFDG|RFNOWAIT|RFNOTEG)){
	case -1:
		sysfatal("can't fork: %r");
	default:
		close(fd);
		return;
	case 0:
		break;
	}

	procsetname("recvra6 on %s %I", conf.dev, conf.laddr);
	ralog("recvra6 on %s", conf.dev);
	sleepfor = Minv6interradelay;
	for (;;) {
		alarm(sleepfor);
		n = read(fd, buf, sizeof buf);
		sleepfor = alarm(0);

		/* wait for alarm to expire */
		if(sendrscnt < 0 && sleepfor > 100)
			continue;

		ifc = readipifc(conf.mpoint, ifc, myifc);
		if(ifc == nil) {
			ralog("recvra6: can't read router params on %s, quitting on %s",
				conf.mpoint, conf.dev);
			exits(nil);
		}
		
		if(n <= 0) {
			if(sendrscnt > 0) {
				sendrscnt--;
				if(recvra6on(ifc) == IsHostRecv)
					sendrs(fd, v6allroutersL);
				sleepfor = V6rsintvl + nrand(100);
			}
			if(sendrscnt == 0) {
				sendrscnt--;
				sleepfor = 0;
				ralog("recvra6: no router advs after %d sols on %s",
					Maxv6rss, conf.dev);
			}
			continue;
		}

		/* got at least initial ra; no whining */
		sendrscnt = -1;
		sleepfor = 0;

		if(++recvracnt >= Maxv6initras){
			recvracnt = 0;
			sleepfor = Maxv6radelay;
		}

		switch (recvra6on(ifc)) {
		case IsRouter:
			recvrarouter(buf, n);
			break;
		case IsHostRecv:
			recvrahost(buf, n);
			break;
		case IsHostNoRecv:
			ralog("recvra6: recvra off, quitting on %s", conf.dev);
			exits(nil);
		}
	}
}

/*
 * return -1 -- error, reading/writing some file,
 *         0 -- no arp table updates
 *         1 -- successful arp table update
 */
int
recvrs(uchar *buf, int pktlen, uchar *sol)
{
	int n;
	Routersol *rs;
	Lladdropt *llao;

	n = sizeof *rs + sizeof *llao;
	rs = (Routersol *)buf;
	if(pktlen < n)
		return 0;

	llao = (Lladdropt *)&buf[sizeof *rs];
	if(llao->type != V6nd_srclladdr || llao->len != 1 || conf.hwalen != 6)
		return 0;

	if(!validip(rs->src)
	|| isv4(rs->src)
	|| ipcmp(rs->src, v6loopback) == 0
	|| ISIPV6MCAST(rs->src))
		return 0;

	if((n = arpenter(rs->src, llao->lladdr)) <= 0)
		return n;

	ipmove(sol, rs->src);
	return 1;
}

void
sendra(int fd, uchar *dst, int rlt, Ipifc *ifc)
{
	uchar buf[1024];
	Iplifc *lifc;
	Lladdropt *llao;
	Prefixopt *prfo;
	Routeradv *ra;
	int pktlen;

	memset(buf, 0, sizeof buf);

	ra = (Routeradv *)buf;
	ipmove(ra->dst, dst);
	ipmove(ra->src, conf.laddr);
	ra->type = ICMP6_RA;
	ra->cttl = conf.ttl;
	if(conf.mflag > 0)
		ra->mor |= MFMASK;
	if(conf.oflag > 0)
		ra->mor |= OCMASK;
	if(rlt > 0)
		hnputs(ra->routerlt, conf.routerlt);
	else
		hnputs(ra->routerlt, 0);
	hnputl(ra->rchbltime, conf.reachtime);
	hnputl(ra->rxmtimer, conf.rxmitra);
	pktlen = sizeof *ra;

	/*
	 * include link layer address (mac address for now) in
	 * link layer address option
	 */
	if(conf.hwalen > 0){
		llao = (Lladdropt *)&buf[pktlen];
		llao->type = V6nd_srclladdr;
		llao->len = (2+7+conf.hwalen)/8;
		memmove(llao->lladdr, conf.hwa, conf.hwalen);
		pktlen += 8 * llao->len;
	}

	/* include all global unicast prefixes on interface in prefix options */
	for (lifc = (ifc != nil? ifc->lifc: nil); lifc != nil; lifc = lifc->next) {
		if(pktlen > sizeof buf - 4*8)
			break;
		if(!validip(lifc->ip)
		|| isv4(lifc->ip)
		|| ipcmp(lifc->ip, v6loopback) == 0
		|| ISIPV6MCAST(lifc->ip)
		|| ISIPV6LINKLOCAL(lifc->ip))
			continue;
		prfo = (Prefixopt *)&buf[pktlen];
		prfo->type = V6nd_pfxinfo;
		prfo->len = 4;
		prfo->plen = masklen(lifc->mask) & 127;
		if(prfo->plen == 0)
			continue;
		ipmove(prfo->pref, lifc->net);
		prfo->lar = AFMASK|OLMASK;
		hnputl(prfo->validlt, lifc->validlt);
		hnputl(prfo->preflt, lifc->preflt);
		pktlen += 8 * prfo->len;
	}

	write(fd, buf, pktlen);
}

/*
 * daemon to send router advertisements to hosts
 */
void
sendra6(void)
{
	int fd, n, sleepfor, nquitmsgs;
	uchar buf[4096], dst[IPaddrlen];
	Ipifc *ifc;

	ifc = readipifc(conf.mpoint, nil, myifc);
	if(ifc == nil)
		sysfatal("can't read ipifc: %r");

	if(!findllip(conf.laddr, ifc))
		sysfatal("no link local address");

	fd = dialicmpv6(v6allroutersL, ICMP6_RS);
	if(fd < 0)
		sysfatal("can't open icmp_rs connection: %r");

	notify(catch);
	nquitmsgs = Maxv6finalras;

	switch(rfork(RFPROC|RFMEM|RFFDG|RFNOWAIT|RFNOTEG)){
	case -1:
		sysfatal("can't fork: %r");
	default:
		close(fd);
		return;
	case 0:
		break;
	}

	procsetname("sendra6 on %s %I", conf.dev, conf.laddr);
	ralog("sendra6 on %s", conf.dev);
	sleepfor = 100 + jitter();
	for (;;) {
		alarm(sleepfor);
		n = read(fd, buf, sizeof buf);
		sleepfor = alarm(0);

		if(n > 0 && recvrs(buf, n, dst) > 0)
			sendra(fd, dst, 1, ifc);

		/* wait for alarm to expire */
		if(sleepfor > 100)
			continue;
		sleepfor = Minv6interradelay;

		ifc = readipifc(conf.mpoint, ifc, myifc);
		if(ifc == nil) {
			ralog("sendra6: can't read router params on %s, quitting on %s",
				conf.mpoint, conf.dev);
			exits(nil);
		}
		if(ifc->sendra6 <= 0){
			if(nquitmsgs > 0) {
				nquitmsgs--;
				sendra(fd, v6allnodesL, 0, ifc);
				continue;
			} else {
				ralog("sendra6: sendra off, quitting on %s", conf.dev);
				exits(nil);
			}
		}
		sendra(fd, v6allnodesL, 1, ifc);
		sleepfor = randint(ifc->rp.minraint, ifc->rp.maxraint);
	}
}

void
startra6(void)
{
	static char routeon[] = "iprouting 1";

	mklladdr();

	if(conf.recvra > 0)
		recvra6();

	if(conf.sendra > 0) {
		if(write(conf.cfd, routeon, sizeof routeon - 1) < 0) {
			warning("write (iprouting 1) failed: %r");
			return;
		}
		sendra6();
		if(conf.recvra <= 0)
			recvra6();
	}
}

void
doipv6(int what)
{
	fprint(conf.rfd, "tag ra6");

	switch (what) {
	default:
		sysfatal("unknown IPv6 verb");
	case Vaddpref6:
		issueadd6(&conf);
		break;
	case Vra6:
		issuebasera6(&conf);
		issuerara6(&conf);
		dolog = 1;
		startra6();
		break;
	}
}
