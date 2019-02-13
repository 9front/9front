/*
 * ipconfig - configure parameters of an ip stack
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ip.h>
#include <ndb.h>
#include "ipconfig.h"
#include "../dhcp.h"

enum
{
	Taddr,
	Taddrs,
	Tstr,
	Tbyte,
	Tulong,
	Tvec,
	Tnames,
};

typedef struct Option Option;
struct Option
{
	char	*name;
	int	type;
};

/*
 * I was too lazy to look up the types for each of these
 * options.  If someone feels like it, please mail me a
 * corrected array -- presotto
 */
static Option option[256] =
{
[OBmask]		{ "ipmask",		Taddr },
[OBtimeoff]		{ "timeoff",		Tulong },
[OBrouter]		{ "ipgw",		Taddrs },
[OBtimeserver]		{ "time",		Taddrs },
[OBnameserver]		{ "name",		Taddrs },
[OBdnserver]		{ "dns",		Taddrs },
[OBlogserver]		{ "log",		Taddrs },
[OBcookieserver]	{ "cookie",		Taddrs },
[OBlprserver]		{ "lpr",		Taddrs },
[OBimpressserver]	{ "impress",		Taddrs },
[OBrlserver]		{ "rl",			Taddrs },
[OBhostname]		{ "sys",		Tstr },
[OBbflen]		{ "bflen",		Tulong },
[OBdumpfile]		{ "dumpfile",		Tstr },
[OBdomainname]		{ "dom",		Tstr },
[OBrootserver]		{ "rootserver",		Taddrs },
[OBrootpath]		{ "rootpath",		Tstr },
[OBextpath]		{ "extpath",		Tstr },
[OBipforward]		{ "ipforward",		Taddrs },
[OBnonlocal]		{ "nonlocal",		Taddrs },
[OBpolicyfilter]	{ "policyfilter",	Taddrs },
[OBmaxdatagram]		{ "maxdatagram",	Tulong },
[OBttl]			{ "ttl",		Tulong },
[OBpathtimeout]		{ "pathtimeout",	Taddrs },
[OBpathplateau]		{ "pathplateau",	Taddrs },
[OBmtu]			{ "mtu",		Tulong },
[OBsubnetslocal]	{ "subnetslocal",	Taddrs },
[OBbaddr]		{ "baddr",		Taddrs },
[OBdiscovermask]	{ "discovermask",	Taddrs },
[OBsupplymask]		{ "supplymask",		Taddrs },
[OBdiscoverrouter]	{ "discoverrouter",	Taddrs },
[OBrsserver]		{ "rs",			Taddrs },
[OBstaticroutes]	{ "staticroutes",	Taddrs },
[OBtrailerencap]	{ "trailerencap",	Taddrs },
[OBarptimeout]		{ "arptimeout",		Tulong },
[OBetherencap]		{ "etherencap",		Taddrs },
[OBtcpttl]		{ "tcpttl",		Tulong },
[OBtcpka]		{ "tcpka",		Tulong },
[OBtcpkag]		{ "tcpkag",		Tulong },
[OBnisdomain]		{ "nisdomain",		Tstr },
[OBniserver]		{ "ni",			Taddrs },
[OBntpserver]		{ "ntp",		Taddrs },
[OBnetbiosns]		{ "netbiosns",		Taddrs },
[OBnetbiosdds]		{ "netbiosdds",		Taddrs },
[OBnetbiostype]		{ "netbiostype",	Taddrs },
[OBnetbiosscope]	{ "netbiosscope",	Taddrs },
[OBxfontserver]		{ "xfont",		Taddrs },
[OBxdispmanager]	{ "xdispmanager",	Taddrs },
[OBnisplusdomain]	{ "nisplusdomain",	Tstr },
[OBnisplusserver]	{ "nisplus",		Taddrs },
[OBhomeagent]		{ "homeagent",		Taddrs },
[OBsmtpserver]		{ "smtp",		Taddrs },
[OBpop3server]		{ "pop3",		Taddrs },
[OBnntpserver]		{ "nntp",		Taddrs },
[OBwwwserver]		{ "www",		Taddrs },
[OBfingerserver]	{ "finger",		Taddrs },
[OBircserver]		{ "irc",		Taddrs },
[OBstserver]		{ "st",			Taddrs },
[OBstdaserver]		{ "stdar",		Taddrs },

[ODipaddr]		{ "ipaddr",		Taddr },
[ODlease]		{ "lease",		Tulong },
[ODoverload]		{ "overload",		Taddr },
[ODtype]		{ "type",		Tbyte },
[ODserverid]		{ "serverid",		Taddr },
[ODparams]		{ "params",		Tvec },
[ODmessage]		{ "message",		Tstr },
[ODmaxmsg]		{ "maxmsg",		Tulong },
[ODrenewaltime]		{ "renewaltime",	Tulong },
[ODrebindingtime]	{ "rebindingtime",	Tulong },
[ODvendorclass]		{ "vendorclass",	Tvec },
[ODclientid]		{ "clientid",		Tvec },
[ODtftpserver]		{ "tftp",		Taddr },
[ODbootfile]		{ "bootfile",		Tstr },
[ODdnsdomain]		{ "dnsdomain",		Tnames },
};

static uchar defrequested[] = {
	OBmask, OBrouter, OBdnserver, OBhostname, OBdomainname, ODdnsdomain, OBntpserver,
};

static uchar	requested[256];
static int	nrequested;

static char 	optmagic[4] = { 0x63, 0x82, 0x53, 0x63 };

static int	openlisten(void);

static void	dhcprecv(void);
static void	dhcpsend(int);
static void	dhcptimer(void);

static uchar*	optaddaddr(uchar*, int, uchar*);
static uchar*	optaddbyte(uchar*, int, int);
static uchar*	optaddstr(uchar*, int, char*);
static uchar*	optadd(uchar*, int, void*, int);
static uchar*	optaddulong(uchar*, int, ulong);
static uchar*	optaddvec(uchar*, int, uchar*, int);
static int	optgetaddrs(uchar*, int, uchar*, int);
static int	optgetp9addrs(uchar*, int, uchar*, int);
static int	optgetaddr(uchar*, int, uchar*);
static int	optgetbyte(uchar*, int);
static int	optgetstr(uchar*, int, char*, int);
static uchar*	optget(uchar*, int, int*);
static ulong	optgetulong(uchar*, int);
static int	optgetvec(uchar*, int, uchar*, int);
static char*	optgetx(uchar*, uchar);
static int	optgetnames(uchar*, int, char*, int);

static void	getoptions(uchar*);
static int	parseoptions(uchar *p, int n);
static Bootp*	parsebootp(uchar*, int);

void
dhcpinit(void)
{
	/* init set of requested dhcp parameters with the default */
	nrequested = sizeof defrequested;
	memcpy(requested, defrequested, nrequested);
}

void
dhcpquery(int needconfig, int startstate)
{
	if(needconfig)
		fprint(conf.cfd, "add %I %M", IPnoaddr, IPnoaddr);

	conf.fd = openlisten();
	if(conf.fd < 0){
		conf.state = Sinit;
		return;
	}
	notify(catch);

	conf.xid = lrand();
	conf.starttime = time(0);
	conf.state = startstate;
	switch(startstate){
	case Sselecting:
		conf.offered = 0;
		dhcpsend(Discover);
		break;
	case Srenewing:
		dhcpsend(Request);
		break;
	default:
		sysfatal("internal error 0");
	}
	conf.resend = 0;
	conf.timeout = time(0) + 4;

	while(conf.state != Sbound && conf.state != Sinit){
		dhcprecv();
		dhcptimer();
	}
	close(conf.fd);

	if(needconfig)
		fprint(conf.cfd, "remove %I %M", IPnoaddr, IPnoaddr);

}

enum {
	/*
	 * was an hour, needs to be less for the ARM/GS1 until the timer
	 * code has been cleaned up (pb).
	 */
	Maxsleep = 450,
};

void
dhcpwatch(int needconfig)
{
	ulong secs, s, t;

	if(nodhcpwatch)
		return;

	switch(rfork(RFPROC|RFFDG|RFNOWAIT|RFNOTEG)){
	default:
		return;
	case 0:
		break;
	}

	dolog = 1;			/* log, don't print */
	procsetname("dhcpwatch on %s", conf.dev);
	/* keep trying to renew the lease */
	for(;;){
		secs = conf.lease/2;
		if(secs < 5)
			secs = 5;

		/* avoid overflows */
		for(s = secs; s > 0; s -= t){
			if(s > Maxsleep)
				t = Maxsleep;
			else
				t = s;
			sleep(t*1000);
		}

		if(conf.lease > 0){
			/*
			 * during boot, the starttime can be bogus so avoid
			 * spurious ipunconfig's
			 */
			t = time(0) - conf.starttime;
			if(t > (3*secs)/2)
				t = secs;
			if(t >= conf.lease){
				conf.lease = 0;
				DEBUG("couldn't renew IP lease");
				if(!noconfig){
					ipunconfig();
					needconfig = 1;
				}
			} else
				conf.lease -= t;
		}
		dhcpquery(needconfig, needconfig? Sselecting: Srenewing);

		if(needconfig && conf.state == Sbound){
			if(ip4cfg() < 0)
				sysfatal("can't start ip: %r");
			needconfig = 0;
			/*
			 * leave everything we've learned somewhere that
			 * other procs can find it.
			 */
			if(beprimary)
				putndb();
			refresh();
		}
	}
}

static void
dhcptimer(void)
{
	ulong now;

	now = time(0);
	if(now < conf.timeout)
		return;

	switch(conf.state) {
	default:
		sysfatal("dhcptimer: unknown state %d", conf.state);
	case Sinit:
	case Sbound:
		break;
	case Sselecting:
	case Srequesting:
	case Srebinding:
		dhcpsend(conf.state == Sselecting? Discover: Request);
		conf.timeout = now + 4;
		if(++conf.resend > 5)
			conf.state = Sinit;
		break;
	case Srenewing:
		dhcpsend(Request);
		conf.timeout = now + 1;
		if(++conf.resend > 3) {
			conf.state = Srebinding;
			conf.resend = 0;
		}
		break;
	}
}

static void
dhcpsend(int type)
{
	Bootp bp;
	uchar *p;
	int n;
	uchar vendor[64];
	Udphdr *up = (Udphdr*)bp.udphdr;

	memset(&bp, 0, sizeof bp);

	hnputs(up->rport, 67);
	bp.op = Bootrequest;
	hnputl(bp.xid, conf.xid);
	hnputs(bp.secs, time(0)-conf.starttime);
	hnputs(bp.flags, 0);
	memmove(bp.optmagic, optmagic, 4);
	if(conf.hwatype >= 0 && conf.hwalen < sizeof bp.chaddr){
		memmove(bp.chaddr, conf.hwa, conf.hwalen);
		bp.hlen = conf.hwalen;
		bp.htype = conf.hwatype;
	}
	p = bp.optdata;
	p = optaddbyte(p, ODtype, type);
	p = optadd(p, ODclientid, conf.cid, conf.cidlen);
	switch(type) {
	default:
		sysfatal("dhcpsend: unknown message type: %d", type);
	case Discover:
		ipmove(up->raddr, IPv4bcast);	/* broadcast */
		if(*conf.hostname && sendhostname)
			p = optaddstr(p, OBhostname, conf.hostname);
		if(plan9){
			n = snprint((char*)vendor, sizeof vendor,
				"plan9_%s", conf.cputype);
			p = optaddvec(p, ODvendorclass, vendor, n);
		}
		p = optaddvec(p, ODparams, requested, nrequested);
		if(validip(conf.laddr))
			p = optaddaddr(p, ODipaddr, conf.laddr);
		break;
	case Request:
		switch(conf.state){
		case Srenewing:
			ipmove(up->raddr, conf.server);
			v6tov4(bp.ciaddr, conf.laddr);
			break;
		case Srebinding:
			ipmove(up->raddr, IPv4bcast);	/* broadcast */
			v6tov4(bp.ciaddr, conf.laddr);
			break;
		case Srequesting:
			ipmove(up->raddr, IPv4bcast);	/* broadcast */
			p = optaddaddr(p, ODipaddr, conf.laddr);
			p = optaddaddr(p, ODserverid, conf.server);
			break;
		}
		p = optaddulong(p, ODlease, conf.offered);
		if(plan9){
			n = snprint((char*)vendor, sizeof vendor,
				"plan9_%s", conf.cputype);
			p = optaddvec(p, ODvendorclass, vendor, n);
		}
		p = optaddvec(p, ODparams, requested, nrequested);
		if(*conf.hostname && sendhostname)
			p = optaddstr(p, OBhostname, conf.hostname);
		break;
	case Release:
		ipmove(up->raddr, conf.server);
		v6tov4(bp.ciaddr, conf.laddr);
		p = optaddaddr(p, ODipaddr, conf.laddr);
		p = optaddaddr(p, ODserverid, conf.server);
		break;
	}

	*p++ = OBend;

	n = p - (uchar*)&bp;
	USED(n);

	/*
	 *  We use a maximum size DHCP packet to survive the
	 *  All_Aboard NAT package from Internet Share.  It
	 *  always replies to DHCP requests with a packet of the
	 *  same size, so if the request is too short the reply
	 *  is truncated.
	 */
	if(write(conf.fd, &bp, sizeof bp) != sizeof bp)
		warning("dhcpsend: write failed: %r");
}

static void
dhcprecv(void)
{
	int i, n, type;
	ulong lease;
	char err[ERRMAX];
	uchar buf[8000], vopts[256], taddr[IPaddrlen];
	Bootp *bp;

	memset(buf, 0, sizeof buf);
	alarm(1000);
	n = read(conf.fd, buf, sizeof buf);
	alarm(0);

	if(n < 0){
		rerrstr(err, sizeof err);
		if(strstr(err, "interrupt") == nil)
			warning("dhcprecv: bad read: %s", err);
		else
			DEBUG("dhcprecv: read timed out");
		return;
	}

	bp = parsebootp(buf, n);
	if(bp == 0) {
		DEBUG("parsebootp failed: dropping packet");
		return;
	}

	type = optgetbyte(bp->optdata, ODtype);
	switch(type) {
	default:
		warning("dhcprecv: unknown type: %d", type);
		break;
	case Offer:
		DEBUG("got offer from %V ", bp->siaddr);
		if(conf.state != Sselecting)
			break;
		lease = optgetulong(bp->optdata, ODlease);
		if(lease == 0){
			/*
			 * The All_Aboard NAT package from Internet Share
			 * doesn't give a lease time, so we have to assume one.
			 */
			warning("Offer with %lud lease, using %d", lease, MinLease);
			lease = MinLease;
		}
		DEBUG("lease=%lud ", lease);
		if(!optgetaddr(bp->optdata, ODserverid, conf.server)) {
			warning("Offer from server with invalid serverid");
			break;
		}

		v4tov6(conf.laddr, bp->yiaddr);
		memmove(conf.sname, bp->sname, sizeof conf.sname);
		conf.sname[sizeof conf.sname-1] = 0;
		DEBUG("server=%I sname=%s", conf.server, conf.sname);
		conf.offered = lease;
		conf.state = Srequesting;
		dhcpsend(Request);
		conf.resend = 0;
		conf.timeout = time(0) + 4;
		break;
	case Ack:
		DEBUG("got ack from %V ", bp->siaddr);
		if (conf.state != Srequesting && conf.state != Srenewing &&
		    conf.state != Srebinding)
			break;

		/* ignore a bad lease */
		lease = optgetulong(bp->optdata, ODlease);
		if(lease == 0){
			/*
			 * The All_Aboard NAT package from Internet Share
			 * doesn't give a lease time, so we have to assume one.
			 */
			warning("Ack with %lud lease, using %d", lease, MinLease);
			lease = MinLease;
		}
		DEBUG("lease=%lud ", lease);

		/* address and mask */
		if(!validip(conf.laddr) || !Oflag)
			v4tov6(conf.laddr, bp->yiaddr);
		if(!validip(conf.mask) || !Oflag){
			if(!optgetaddr(bp->optdata, OBmask, conf.mask))
				ipmove(conf.mask, IPnoaddr);
			if(ipcmp(conf.mask, IPv4bcast) == 0)
				ipmove(conf.mask, IPnoaddr);
		}
		DEBUG("ipaddr=%I ipmask=%M ", conf.laddr, conf.mask);

		/*
		 * get a router address either from the router option
		 * or from the router that forwarded the dhcp packet
		 */
		if(validip(conf.gaddr) && Oflag) {
			DEBUG("ipgw=%I ", conf.gaddr);
		} else if(optgetaddr(bp->optdata, OBrouter, conf.gaddr)){
			DEBUG("ipgw=%I ", conf.gaddr);
		} else if(memcmp(bp->giaddr, IPnoaddr+IPv4off, IPv4addrlen)!=0){
			v4tov6(conf.gaddr, bp->giaddr);
			DEBUG("giaddr=%I ", conf.gaddr);
		}

		/* get dns servers */
		memset(conf.dns, 0, sizeof conf.dns);
		n = optgetaddrs(bp->optdata, OBdnserver, conf.dns,
			sizeof conf.dns/IPaddrlen);
		for(i = 0; i < n; i++)
			DEBUG("dns=%I ", conf.dns + i*IPaddrlen);

		/* get ntp servers */
		memset(conf.ntp, 0, sizeof conf.ntp);
		n = optgetaddrs(bp->optdata, OBntpserver, conf.ntp,
			sizeof conf.ntp/IPaddrlen);
		for(i = 0; i < n; i++)
			DEBUG("ntp=%I ", conf.ntp + i*IPaddrlen);

		/* get names */
		if(optgetstr(bp->optdata, OBhostname,
			conf.hostname, sizeof conf.hostname))
			DEBUG("hostname=%s ", conf.hostname);
		if(optgetstr(bp->optdata, OBdomainname,
			conf.domainname, sizeof conf.domainname))
			DEBUG("domainname=%s ", conf.domainname);
		if(optgetnames(bp->optdata, ODdnsdomain,
			conf.dnsdomain, sizeof conf.dnsdomain))
			DEBUG("dnsdomain=%s ", conf.dnsdomain);

		/* get anything else we asked for */
		getoptions(bp->optdata);

		/* get plan9-specific options */
		n = optgetvec(bp->optdata, OBvendorinfo, vopts, sizeof vopts-1);
		if(n > 0 && parseoptions(vopts, n) == 0){
			if(validip(conf.fs) && Oflag)
				n = 1;
			else {
				n = optgetp9addrs(vopts, OP9fs, conf.fs, 2);
				if (n == 0)
					n = optgetaddrs(vopts, OP9fsv4,
						conf.fs, 2);
			}
			for(i = 0; i < n; i++)
				DEBUG("fs=%I ", conf.fs + i*IPaddrlen);

			if(validip(conf.auth) && Oflag)
				n = 1;
			else {
				n = optgetp9addrs(vopts, OP9auth, conf.auth, 2);
				if (n == 0)
					n = optgetaddrs(vopts, OP9authv4,
						conf.auth, 2);
			}
			for(i = 0; i < n; i++)
				DEBUG("auth=%I ", conf.auth + i*IPaddrlen);

			n = optgetp9addrs(vopts, OP9ipaddr, taddr, 1);
			if (n > 0)
				ipmove(conf.laddr, taddr);
			n = optgetp9addrs(vopts, OP9ipmask, taddr, 1);
			if (n > 0)
				ipmove(conf.mask, taddr);
			n = optgetp9addrs(vopts, OP9ipgw, taddr, 1);
			if (n > 0)
				ipmove(conf.gaddr, taddr);
			DEBUG("new ipaddr=%I new ipmask=%M new ipgw=%I",
				conf.laddr, conf.mask, conf.gaddr);
		}
		conf.lease = lease;
		conf.state = Sbound;
		DEBUG("server=%I sname=%s", conf.server, conf.sname);
		break;
	case Nak:
		conf.state = Sinit;
		warning("recved dhcpnak on %s", conf.mpoint);
		break;
	}
}

static int
openlisten(void)
{
	int n, fd, cfd;
	char data[128], devdir[40];

	if (validip(conf.laddr) &&
	    (conf.state == Srenewing || conf.state == Srebinding))
		sprint(data, "%s/udp!%I!68", conf.mpoint, conf.laddr);
	else
		sprint(data, "%s/udp!*!68", conf.mpoint);
	for (n = 0; (cfd = announce(data, devdir)) < 0; n++) {
		if(!noconfig)
			sysfatal("can't announce for dhcp: %r");

		/* might be another client - wait and try again */
		warning("can't announce %s: %r", data);
		sleep(jitter());
		if(n > 10)
			return -1;
	}

	if(fprint(cfd, "headers") < 0)
		sysfatal("can't set header mode: %r");

	sprint(data, "%s/data", devdir);
	fd = open(data, ORDWR);
	if(fd < 0)
		sysfatal("open %s: %r", data);
	close(cfd);
	return fd;
}

static uchar*
optadd(uchar *p, int op, void *d, int n)
{
	p[0] = op;
	p[1] = n;
	memmove(p+2, d, n);
	return p+n+2;
}

static uchar*
optaddbyte(uchar *p, int op, int b)
{
	p[0] = op;
	p[1] = 1;
	p[2] = b;
	return p+3;
}

static uchar*
optaddulong(uchar *p, int op, ulong x)
{
	p[0] = op;
	p[1] = 4;
	hnputl(p+2, x);
	return p+6;
}

static uchar *
optaddaddr(uchar *p, int op, uchar *ip)
{
	p[0] = op;
	p[1] = 4;
	v6tov4(p+2, ip);
	return p+6;
}

/* add dhcp option op with value v of length n to dhcp option array p */
static uchar *
optaddvec(uchar *p, int op, uchar *v, int n)
{
	p[0] = op;
	p[1] = n;
	memmove(p+2, v, n);
	return p+2+n;
}

static uchar *
optaddstr(uchar *p, int op, char *v)
{
	int n;

	n = strlen(v);
	p[0] = op;
	p[1] = n;
	memmove(p+2, v, n);
	return p+2+n;
}

/*
 * parse p, looking for option `op'.  if non-nil, np points to minimum length.
 * return nil if option is too small, else ptr to opt, and
 * store actual length via np if non-nil.
 */
static uchar*
optget(uchar *p, int op, int *np)
{
	int len, code;

	while ((code = *p++) != OBend) {
		if(code == OBpad)
			continue;
		len = *p++;
		if(code != op) {
			p += len;
			continue;
		}
		if(np != nil){
			if(*np > len)
				return 0;
			*np = len;
		}
		return p;
	}
	return 0;
}

static int
optgetbyte(uchar *p, int op)
{
	int len;

	len = 1;
	p = optget(p, op, &len);
	if(p == nil)
		return 0;
	return *p;
}

static ulong
optgetulong(uchar *p, int op)
{
	int len;

	len = 4;
	p = optget(p, op, &len);
	if(p == nil)
		return 0;
	return nhgetl(p);
}

static int
optgetaddr(uchar *p, int op, uchar *ip)
{
	int len;

	len = 4;
	p = optget(p, op, &len);
	if(p == nil)
		return 0;
	v4tov6(ip, p);
	return 1;
}

/* expect at most n addresses; ip[] only has room for that many */
static int
optgetaddrs(uchar *p, int op, uchar *ip, int n)
{
	int len, i;

	len = 4;
	p = optget(p, op, &len);
	if(p == nil)
		return 0;
	len /= IPv4addrlen;
	if(len > n)
		len = n;
	for(i = 0; i < len; i++)
		v4tov6(&ip[i*IPaddrlen], &p[i*IPv4addrlen]);
	return i;
}

/* expect at most n addresses; ip[] only has room for that many */
static int
optgetp9addrs(uchar *ap, int op, uchar *ip, int n)
{
	int len, i, slen, addrs;
	char *p;

	len = 1;			/* minimum bytes needed */
	p = (char *)optget(ap, op, &len);
	if(p == nil)
		return 0;
	addrs = *p++;			/* first byte is address count */
	for (i = 0; i < n  && i < addrs && len > 0; i++) {
		slen = strlen(p) + 1;
		if (parseip(&ip[i*IPaddrlen], p) == -1)
			fprint(2, "%s: bad address %s\n", argv0, p);
		DEBUG("got plan 9 option %d addr %I (%s)",
			op, &ip[i*IPaddrlen], p);
		p += slen;
		len -= slen;
	}
	return addrs;
}

static int
optgetvec(uchar *p, int op, uchar *v, int n)
{
	int len;

	len = 1;
	p = optget(p, op, &len);
	if(p == nil)
		return 0;
	if(len > n)
		len = n;
	memmove(v, p, len);
	return len;
}

static int
optgetstr(uchar *p, int op, char *s, int n)
{
	int len;

	len = 1;
	p = optget(p, op, &len);
	if(p == nil)
		return 0;
	if(len >= n)
		len = n-1;
	memmove(s, p, len);
	s[len] = 0;
	return len;
}

static int
optgetnames(uchar *p, int op, char *s, int n)
{
	uchar buf[256];
	int nbuf, len;

	for(nbuf=0;;p+=len,nbuf+=len){
		len = 1;
		p = optget(p, op, &len);
		if(p == nil)
			break;
		if(nbuf+len > sizeof(buf))
			return 0;
		memmove(buf+nbuf, p, len);
	}
	if((len = gnames(s, n, buf, nbuf)) < 0){
		memset(s, 0, n);
		return 0;
	}
	return len;
}

int
addoption(char *opt)
{
	int i;
	Option *o;

	if(opt == nil)
		return -1;
	for(o = option; o < &option[nelem(option)]; o++)
		if(o->name && strcmp(opt, o->name) == 0){
			i = o - option;
			if(memchr(requested, i, nrequested) == 0 &&
			    nrequested < nelem(requested))
				requested[nrequested++] = i;
			return 0;
		}
	return -1;
}

static char*
optgetx(uchar *p, uchar opt)
{
	int i, n;
	ulong x;
	char *s, *ns;
	char str[256];
	uchar ip[IPaddrlen], ips[16*IPaddrlen], vec[256];
	Option *o;

	o = &option[opt];
	if(o->name == nil)
		return nil;

	s = nil;
	switch(o->type){
	case Taddr:
		if(optgetaddr(p, opt, ip))
			s = smprint("%s=%I", o->name, ip);
		break;
	case Taddrs:
		n = optgetaddrs(p, opt, ips, 16);
		if(n > 0)
			s = smprint("%s=%I", o->name, ips);
		for(i = 1; i < n; i++){
			ns = smprint("%s %s=%I", s, o->name, &ips[i*IPaddrlen]);
			free(s);
			s = ns;
		}
		break;
	case Tulong:
		x = optgetulong(p, opt);
		if(x != 0)
			s = smprint("%s=%lud", o->name, x);
		break;
	case Tbyte:
		x = optgetbyte(p, opt);
		if(x != 0)
			s = smprint("%s=%lud", o->name, x);
		break;
	case Tstr:
		if(optgetstr(p, opt, str, sizeof str))
			s = smprint("%s=%s", o->name, str);
		break;
	case Tvec:
		n = optgetvec(p, opt, vec, sizeof vec);
		if(n > 0)
			s = smprint("%s=%.*H", o->name, n, vec);
		break;
	}
	return s;
}

static void
getoptions(uchar *p)
{
	int i;
	char *s, *t;

	for(i = nelem(defrequested); i < nrequested; i++){
		s = optgetx(p, requested[i]);
		if(s != nil)
			DEBUG("%s ", s);
		if(ndboptions == nil)
			ndboptions = smprint("\t%s", s);
		else{
			t = ndboptions;
			ndboptions = smprint("\t%s%s", s, ndboptions);
			free(t);
		}
		free(s);
	}
}

/*
 * sanity check options area
 * 	- options don't overflow packet
 * 	- options end with an OBend
 */
static int
parseoptions(uchar *p, int n)
{
	int code, len, nin = n;

	while (n > 0) {
		code = *p++;
		n--;
		if(code == OBend)
			return 0;
		if(code == OBpad)
			continue;
		if(n == 0) {
			warning("parseoptions: bad option: 0x%ux: truncated: "
				"opt length = %d", code, nin);
			return -1;
		}

		len = *p++;
		n--;
		DEBUG("parseoptions: %s(%d) len %d, bytes left %d",
			option[code].name, code, len, n);
		if(len > n) {
			warning("parseoptions: bad option: 0x%ux: %d > %d: "
				"opt length = %d", code, len, n, nin);
			return -1;
		}
		p += len;
		n -= len;
	}

	/* make sure packet ends with an OBend after all the optget code */
	*p = OBend;
	return 0;
}

/*
 * sanity check received packet:
 * 	- magic is dhcp magic
 * 	- options don't overflow packet
 */
static Bootp*
parsebootp(uchar *p, int n)
{
	Bootp *bp;

	bp = (Bootp*)p;
	if(n < bp->optmagic - p) {
		warning("parsebootp: short bootp packet");
		return nil;
	}

	if(conf.xid != nhgetl(bp->xid))		/* not meant for us */
		return nil;

	if(bp->op != Bootreply) {
		warning("parsebootp: bad op %d", bp->op);
		return nil;
	}

	n -= bp->optmagic - p;
	p = bp->optmagic;

	if(n < 4) {
		warning("parsebootp: no option data");
		return nil;
	}
	if(memcmp(optmagic, p, 4) != 0) {
		warning("parsebootp: bad opt magic %ux %ux %ux %ux",
			p[0], p[1], p[2], p[3]);
		return nil;
	}
	p += 4;
	n -= 4;
	DEBUG("parsebootp: new packet");
	if(parseoptions(p, n) < 0)
		return nil;
	return bp;
}

