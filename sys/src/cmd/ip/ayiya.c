/*
 * ayiya - tunnel client.
 */

#include <u.h>
#include <libc.h>
#include <ip.h>
#include <mp.h>
#include <libsec.h>

/*
 * IPv6 and related IP protocols & their numbers:
 *
 * ipv6		41      IPv6            # Internet Protocol, version 6
 * ipv6-route	43      IPv6-Route      # Routing Header for IPv6
 * ipv6-frag	44      IPv6-Frag       # Fragment Header for IPv6
 * esp		50      ESP             # Encapsulating Security Payload
 * ah		51      AH              # Authentication Header
 * ipv6-icmp	58      IPv6-ICMP icmp6 # ICMP version 6
 * ipv6-nonxt	59      IPv6-NoNxt      # No Next Header for IPv6
 * ipv6-opts	60      IPv6-Opts       # Destination Options for IPv6
 */
enum {
	IP_IPV6PROTO	= 41,		/* IPv4 protocol number for IPv6 */
 	IP_ESPPROTO	= 50,		/* IP v4 and v6 protocol number */
 	IP_AHPROTO	= 51,		/* IP v4 and v6 protocol number */
	IP_ICMPV6PROTO	= 58,
	V6to4pfx	= 0x2002,

	IP_MAXPAY	= 2*1024,
};

enum {
	AYIYAMAXID	= 1<<15,
	AYIYAMAXSIG	= 15*4,

	AYIYAMAXHDR	= 8+AYIYAMAXID+AYIYAMAXSIG,

	IdNone = 0,
	IdInteger,
	IdString,

	HashNone = 0,
	HashMD5,
	HashSHA1,

	AuthNone = 0,
	AuthSharedKey,
	AuthPubKey,

	OpNone = 0,
	OpForward,
	OpEchoRequest,
	OpEchoRequestAndForward,
	OpEchoResponse,
	OpMOTD,
	OpQueryRequest,
	OpQueryResponse,	
};

typedef struct AYIYA AYIYA;
struct AYIYA
{
	uint	idlen;
	uint	idtype;
	uint	siglen;
	uint	hashmeth;
	uint	authmeth;
	uint	opcode;
	uint	nexthdr;
	uint	epochtime;

	uchar	*identity;
	uchar	*signature;
};

AYIYA	conf;

int mtu = 1500-8;

int gateway;
int debug;

uchar local6[IPaddrlen];
uchar remote6[IPaddrlen];
uchar localmask[IPaddrlen];
uchar localnet[IPaddrlen];

uchar nullsig[AYIYAMAXSIG];

static char *secret = nil;

static char *outside = nil;	/* dial string of tunnel server */
static char *inside = "/net";

static int	badipv4(uchar*);
static int	badipv6(uchar*);
static void	ip2tunnel(int, int);
static void	tunnel2ip(int, int);

static void
ayiyadump(AYIYA *a)
{
	int i;

	fprint(2, "idlen=%ud idtype=%ux siglen=%ud hashmeth=%ud authmeth=%ud opcode=%ux nexthdr=%ux epochtime=%ux\n",
		a->idlen, a->idtype, a->siglen, a->hashmeth, a->authmeth, a->opcode, a->nexthdr, a->epochtime);
	fprint(2, "identity=[ ");
	for(i=0; i<a->idlen; i++)
		fprint(2, "%.2ux ", a->identity[i]);
	fprint(2, "] ");
	fprint(2, "signature=[ ");
	for(i=0; i<a->siglen; i++)
		fprint(2, "%.2ux ", a->signature[i]);
	fprint(2, "]\n");

}

static uint
lg2(uint a)
{
	uint n;

	for(n = 0; (a >>= 1) != 0; n++)
		;
	return n;
}

static int
ayiyapack(AYIYA *a, uchar *pay, int paylen)
{
	uchar *pkt;

	pkt = pay;
	if(a->siglen > 0){
		pkt -= a->siglen;
		memmove(pkt, a->signature, a->siglen);
	}
	if(a->idlen > 0){
		pkt -= a->idlen;
		memmove(pkt, a->identity, a->idlen);
	}

	pkt -= 4;
	pkt[0] = a->epochtime>>24;
	pkt[1] = a->epochtime>>16;
	pkt[2] = a->epochtime>>8;
	pkt[3] = a->epochtime;

	pkt -= 4;
	pkt[0] = (lg2(a->idlen)<<4) | a->idtype;
	pkt[1] = ((a->siglen/4)<<4) | a->hashmeth;
	pkt[2] = (a->authmeth<<4) | a->opcode;
	pkt[3] = a->nexthdr;

	USED(paylen);

	return pay - pkt;
}

static int
ayiyaunpack(AYIYA *a, uchar *pkt, int pktlen)
{
	int hdrlen;

	if(pktlen < 8)
		return -1;

	a->idlen = 1<<(pkt[0] >> 4);
	a->idtype = pkt[0] & 15;
	a->siglen = (pkt[1] >> 4) * 4;
	a->hashmeth = pkt[1] & 15;
	a->authmeth = pkt[2] >> 4;
	a->opcode = pkt[2] & 15;
	a->nexthdr = pkt[3];
	a->epochtime = pkt[7] | pkt[6]<<8 | pkt[5]<<16 | pkt[4]<<24;

	hdrlen = 8 + a->idlen + a->siglen;
	if(hdrlen > pktlen)
		return -1;

	a->identity = nil;
	if(a->idlen > 0)
		a->identity = pkt + 8;

	a->signature = nil;
	if(a->siglen > 0)
		a->signature = pkt + 8 + a->idlen;

	return hdrlen;
}

static int
ayiyahash(uint meth, uchar *pkt, int pktlen, uchar *dig)
{
	switch(meth){
	case HashMD5:
		if(dig != nil)
			md5(pkt, pktlen, dig, nil);
		return MD5dlen;
	case HashSHA1:
		if(dig != nil)
			sha1(pkt, pktlen, dig, nil);
		return SHA1dlen;
	}
	return 0;
}

static void
ayiyasign(AYIYA *a, uchar *pkt, int pktlen)
{
	uchar dig[AYIYAMAXSIG], *pktsig;

	if(a->hashmeth == HashNone && a->siglen == 0)
		return;

	assert(a->siglen <= sizeof(dig));
	assert(a->siglen <= pktlen - a->idlen - 8);
	pktsig = pkt + 8 + a->idlen;

	if(ayiyahash(a->hashmeth, pkt, pktlen, dig) != a->siglen){
		memset(pktsig, 0, a->siglen);
		return;
	}

	memmove(pktsig, dig, a->siglen);
}

static int
ayiyaverify(AYIYA *a, uchar *pkt, int pktlen)
{
	uchar dig[AYIYAMAXSIG], sig[AYIYAMAXSIG];

	if(conf.hashmeth == HashNone && a->siglen == 0)
		return 0;
	if(a->hashmeth != conf.hashmeth || a->authmeth != conf.authmeth || a->siglen != conf.siglen)
		return -1;
	memmove(sig, a->signature, a->siglen);
	memmove(a->signature, conf.signature, a->siglen);
	if(ayiyahash(a->hashmeth, pkt, pktlen, dig) != a->siglen)
		return -1;
	return memcmp(sig, dig, a->siglen) != 0;
}

static int
ayiyaout(int fd, AYIYA *a, uchar *p, int n)
{
	int m;

	a->idlen = conf.idlen;
	a->siglen = conf.siglen;
	a->idtype = conf.idtype;
	a->hashmeth = conf.hashmeth;
	a->authmeth = conf.authmeth;
	a->identity = conf.identity;
	a->signature = conf.signature;

	a->epochtime = time(nil);

	if (debug > 1) {
		fprint(2, "send: ");
		ayiyadump(a);
	}

	m = ayiyapack(a, p, n);
	n += m, p -= m;

	ayiyasign(a, p, n);

	if (write(fd, p, n) != n) {
		syslog(0, "ayiya", "error writing to tunnel (%r), giving up");
		return -1;
	}
	return 0;
}

static int
ayiyarquery(char *q)
{
	fprint(2, "ayiyarquery: %s\n", q);
	*q = '\0';
	return 0;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-g] [-m mtu] [-x mtpt] [-k secret] local6[/mask] remote4 remote6\n",
		argv0);
	exits("Usage");
}

/* process non-option arguments */
static void
procargs(int argc, char **argv)
{
	char *ipstr, *maskstr;

	if (argc < 3)
		usage();
	ipstr = *argv++, argc--;
	maskstr = strchr(ipstr, '/');
	if (maskstr == nil && **argv == '/')
		maskstr = *argv++, argc--;
	if (parseipandmask(local6, localmask, ipstr, maskstr) == -1 || isv4(local6))
		sysfatal("bad local v6 address/mask: %s", ipstr);
	if (debug)
		fprint(2, "local6 %I %M\n", local6, localmask);
	argc--;
	outside = netmkaddr(*argv++, "udp", "5072");
	if(outside == nil)
		usage();
	outside = strdup(outside);
	if (debug)
		fprint(2, "outside %s\n", outside);

	/* remote v6 address */
	if (parseip(remote6, *argv++) == -1)
		sysfatal("bad remote v6 address %s", argv[-1]);
	argc--;
	if (argc != 0)
		usage();

	maskip(local6, localmask, localnet);
	if (debug)
		fprint(2, "localnet %I remote6 %I\n", localnet, remote6);
}

static void
setup(int *v6net)
{
	int n, cfd;
	char *cl, *ir;
	char buf[128], path[64];

	/*
	 * open local IPv6 interface (as a packet interface)
	 */

	cl = smprint("%s/ipifc/clone", inside);
	cfd = open(cl, ORDWR);			/* allocate a conversation */
	n = 0;
	if (cfd < 0 || (n = read(cfd, buf, sizeof buf - 1)) <= 0)
		sysfatal("can't make packet interface %s: %r", cl);
	if (debug)
		fprint(2, "cloned %s as local v6 interface\n", cl);
	free(cl);
	buf[n] = 0;

	snprint(path, sizeof path, "%s/ipifc/%s/data", inside, buf);
	*v6net = open(path, ORDWR);
	if (*v6net < 0 || fprint(cfd, "bind pkt") < 0)
		sysfatal("can't bind packet interface: %r");
	if (fprint(cfd, "add %I %M %I %d", local6, localmask, remote6,
		mtu - (IPV4HDR_LEN+8) - (8+conf.idlen+conf.siglen)) <= 0)
		sysfatal("can't set local ipv6 address: %r");
	close(cfd);
	if (debug)
		fprint(2, "opened & bound %s as local v6 interface\n", path);

	if (gateway) {
		/* route global addresses through the tunnel to remote6 */
		ir = smprint("%s/iproute", inside);
		cfd = open(ir, OWRITE);
		if (cfd >= 0 && debug)
			fprint(2, "injected 2000::/3 %I into %s\n", remote6, ir);
		free(ir);
		if (cfd < 0 || fprint(cfd, "add 2000:: /3 %I", remote6) <= 0)
			sysfatal("can't set default global route: %r");
	}
}

static void
runtunnel(int v6net, int tunnel)
{
	/* run the tunnel copying in the background */
	switch (rfork(RFPROC|RFNOWAIT|RFMEM|RFNOTEG)) {
	case -1:
		sysfatal("rfork");
	default:
		exits(nil);
	case 0:
		break;
	}

	switch (rfork(RFPROC|RFNOWAIT|RFMEM)) {
	case -1:
		sysfatal("rfork");
	default:
		tunnel2ip(tunnel, v6net);
		break;
	case 0:
		ip2tunnel(v6net, tunnel);
		break;
	}
	exits("tunnel gone");
}

void
main(int argc, char **argv)
{
	int tunnel, v6net;

	fmtinstall('I', eipfmt);
	fmtinstall('V', eipfmt);
	fmtinstall('M', eipfmt);

	ARGBEGIN {
	case 'd':
		debug++;
		break;
	case 'g':
		gateway++;
		break;
	case 'm':
		mtu = atoi(EARGF(usage()));
		break;
	case 'x':
		inside = EARGF(usage());
		break;
	case 'k':
		secret = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND

	procargs(argc, argv);

	conf.idtype = IdInteger;
	conf.idlen = sizeof(local6);
	conf.identity = local6;

	conf.authmeth = AuthNone;
	conf.hashmeth = HashSHA1;
	conf.siglen = ayiyahash(conf.hashmeth, nil, 0, nil);
	conf.signature = nullsig;

	if(secret != nil){
		conf.authmeth = AuthSharedKey;
		conf.signature = malloc(conf.siglen);
		ayiyahash(conf.hashmeth, (uchar*)secret, strlen(secret), conf.signature);
		memset(secret, 0, strlen(secret));	/* prevent accidents */
	}

	tunnel = dial(outside, nil, nil, nil);
	if (tunnel < 0)
		sysfatal("can't dial tunnel: %r");

	setup(&v6net);
	runtunnel(v6net, tunnel);
	exits(0);
}

static int alarmed;

static void
catcher(void*, char *msg)
{
	if(strstr(msg, "alarm") != nil){
		alarmed = 1;
		noted(NCONT);
	}
	noted(NDFLT);
}

/*
 * encapsulate v6 packets from the packet interface
 * and send them into the tunnel.
 */
static void
ip2tunnel(int in, int out)
{
	uchar buf[AYIYAMAXHDR + IP_MAXPAY], *p;
	Ip6hdr *ip;
	AYIYA y[1];
	int n, m;

	procsetname("v6 %I -> tunnel %s %I", local6, outside, remote6);

	notify(catcher);

	/* get a V6 packet destined for the tunnel */
	for(;;) {
		alarmed = 0;
		alarm(60*1000);

		p = buf + AYIYAMAXHDR;
		if ((n = read(in, p, IP_MAXPAY)) <= 0) {
			if(!alarmed)
				break;

			/* send heartbeat */
			y->nexthdr = 59;
			y->opcode = OpNone;
			if(ayiyaout(out, y, p, 0) < 0)
				break;

			continue;
		}

		ip = (Ip6hdr*)p;

		/* if not IPV6, drop it */
		if ((ip->vcf[0] & 0xF0) != IP_VER6)
			continue;

		/* check length: drop if too short, trim if too long */
		m = nhgets(ip->ploadlen) + IPV6HDR_LEN;
		if (m > n)
			continue;
		if (m < n)
			n = m;

		/* drop if v6 source or destination address is naughty */
		if (badipv6(ip->src)) {
			syslog(0, "ayiya", "egress filtered %I -> %I; bad src",
				ip->src, ip->dst);
			continue;
		}
		if ((ipcmp(ip->dst, remote6) != 0 && badipv6(ip->dst))) {
			syslog(0, "ayiya", "egress filtered %I -> %I; "
				"bad dst not remote", ip->src, ip->dst);
			continue;
		}

		if (debug > 1)
			fprint(2, "v6 to tunnel %I -> %I\n", ip->src, ip->dst);

		/* pass packet to the other end of the tunnel */
		y->nexthdr = IP_IPV6PROTO;
		y->opcode = OpForward;
		if(ayiyaout(out, y, p, n) < 0 && !alarmed)
			break;
	}

	alarm(0);
}

/*
 * decapsulate v6 packets from the tunnel
 * and forward them to the packet interface
 */
static void
tunnel2ip(int in, int out)
{
	uchar buf[2*AYIYAMAXHDR + IP_MAXPAY + 5], *p;
	uchar a[IPaddrlen];
	Ip6hdr *op;
	AYIYA y[1];
	int n, m;

	procsetname("tunnel %s %I -> v6 %I", outside, remote6, local6);

	for (;;) {
		p = buf + AYIYAMAXHDR;	/* space for reply header */

		/* get a packet from the tunnel */
		if ((n = read(in, p, AYIYAMAXHDR + IP_MAXPAY)) <= 0)
			break;

		/* zero slackspace */
		memset(p+n, 0, 5);

		m = ayiyaunpack(y, p, n);
		if (m <= 0 || m > n)
			continue;

		if (debug > 1) {
			fprint(2, "recv: ");
			ayiyadump(y);
		}

		if (ayiyaverify(y, p, n) != 0) {
			fprint(2, "ayiya bad packet signature\n");
			continue;
		}
		n -= m, p += m;

		switch(y->opcode){
		case OpForward:
		case OpEchoRequest:
		case OpEchoRequestAndForward:
			break;
		case OpMOTD:
			fprint(2, "ayiya motd: %s\n", (char*)p);
			continue;
		case OpQueryRequest:
			if(n < 4)
				continue;
			if (ayiyarquery((char*)p + 4) < 0)
				continue;
			n = 4 + strlen((char*)p + 4);
			y->opcode = OpQueryResponse;
			if (ayiyaout(in, y, p, n) < 0)
				return;
			continue;
		case OpNone:
		case OpEchoResponse:
		case OpQueryResponse:
			continue;
		default:
			fprint(2, "ayiya unknown opcode: %x\n", y->opcode);
			continue;
		}

		switch(y->opcode){
		case OpForward:
		case OpEchoRequestAndForward:
			/* if not IPv6 nor ICMPv6, drop it */
			if (y->nexthdr != IP_IPV6PROTO && y->nexthdr != IP_ICMPV6PROTO) {
				syslog(0, "ayiya",
					"dropping pkt from tunnel with inner proto %d",
					y->nexthdr);
				break;
			}

			op = (Ip6hdr*)p;
			if(n < IPV6HDR_LEN)
				break;

			/*
			 * don't relay: just accept packets for local host/subnet
			 * (this blocks link-local and multicast addresses as well)
			 */
			maskip(op->dst, localmask, a);
			if (ipcmp(a, localnet) != 0) {
				syslog(0, "ayiya", "ingress filtered %I -> %I; "
					"dst not on local net", op->src, op->dst);
				break;
			}
			if (debug > 1)
				fprint(2, "tunnel to v6 %I -> %I\n", op->src, op->dst);

			/* pass V6 packet to the interface */
			if (write(out, p, n) != n) {
				syslog(0, "ayiya", "error writing to packet interface (%r), giving up");
				return;
			}
			break;
		}

		switch(y->opcode){
		case OpEchoRequest:
		case OpEchoRequestAndForward:
			y->opcode = OpEchoResponse;
			if (ayiyaout(in, y, p, n) < 0)
				return;
		}
	}
}

static int
badipv4(uchar *a)
{
	switch (a[0]) {
	case 0:				/* unassigned */
	case 10:			/* private */
	case 127:			/* loopback */
		return 1;
	case 172:
		return a[1] >= 16;	/* 172.16.0.0/12 private */
	case 192:
		return a[1] == 168;	/* 192.168.0.0/16 private */
	case 169:
		return a[1] == 254;	/* 169.254.0.0/16 DHCP link-local */
	}
	/* 224.0.0.0/4 multicast, 240.0.0.0/4 reserved, broadcast */
	return a[0] >= 240;
}

/*
 * 0x0000/16 prefix = v4 compatible, v4 mapped, loopback, unspecified...
 * site-local is now deprecated, rfc3879
 */
static int
badipv6(uchar *a)
{
	int h = a[0]<<8 | a[1];

	return h == 0 || ISIPV6MCAST(a) || ISIPV6LINKLOCAL(a) ||
	    h == V6to4pfx && badipv4(a+2);
}
