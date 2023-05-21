#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ip.h>
#include <ndb.h>
#include "ipconfig.h"

enum {
	SOLICIT	= 1,
	ADVERTISE,
	REQUEST,
	CONFIRM,
	RENEW,
	REBIND,
	REPLY,
	RELEASE,
	DECLINE,
	RECONFIGURE,
	INFOREQ,
	RELAYFORW,
	RELAYREPL,
};

static uchar v6dhcpservers[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 1, 0, 2,
};

static uchar sid[256];
static int sidlen;

static int
openlisten(void)
{
	int n, fd, cfd;
	char data[128], devdir[40];

	sprint(data, "%s/udp!%I!546", conf.mpoint, conf.lladdr);
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

	fprint(cfd, "ignoreadvice");

	sprint(data, "%s/data", devdir);
	fd = open(data, ORDWR);
	if(fd < 0)
		sysfatal("open %s: %r", data);
	close(cfd);
	return fd;
}

static int
transaction(int fd, int type, int timeout)
{
	union {
		Udphdr;
		uchar	buf[4096];
	} ipkt, opkt;

	uchar *p, *e, *x;
	int tra, opt, len, sleepfor;

	tra = lrand() & 0xFFFFFF;

	ipmove(opkt.laddr, conf.lladdr);
	ipmove(opkt.raddr, v6dhcpservers);
	ipmove(opkt.ifcaddr, conf.lladdr);
	hnputs(opkt.lport, 546);
	hnputs(opkt.rport, 547);

	p = opkt.buf + Udphdrsize;

	*p++ = type;
	*p++ = tra >> 16;
	*p++ = tra >> 8;
	*p++ = tra >> 0;

	/* client identifier */
	*p++ = 0x00; *p++ = 0x01;
	/* len */
	*p++ = conf.duidlen >> 8;
	*p++ = conf.duidlen;
	memmove(p, conf.duid, conf.duidlen);
	p += conf.duidlen;

	/* IA for non-temporary address */
	len = 12;
	if(validip(conf.laddr))
		len += 4 + IPaddrlen + 2*4;
	*p++ = 0x00; *p++ = 0x03;
	*p++ = len >> 8;
	*p++ = len;
	/* IAID */
	*p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x01;
	/* T1, T2 */
	*p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
	*p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
	if(len > 12){
		*p++ = 0x00; *p++ = 0x05;
		*p++ = 0x00; *p++ = IPaddrlen + 2*4;
		memmove(p, conf.laddr, IPaddrlen);
		p += IPaddrlen;
		memset(p, 0xFF, 2*4);
		p += 2*4;
	}

	/* Option Request */
	*p++ = 0x00; *p++ = 0x06;
	*p++ = 0x00; *p++ = 0x02;
	*p++ = 0x00; *p++ = 0x17;	/* DNS servers */

	if(sidlen > 0){
		*p++ = 0x00; *p++ = 0x02;
		/* len */
		*p++ = sidlen >> 8;
		*p++ = sidlen;;
		memmove(p, sid, sidlen);
		p += sidlen;
	}

	len = -1;
	for(sleepfor = 500; timeout > 0; sleepfor <<= 1){
		DEBUG("sending dhcpv6 request %x", opkt.buf[Udphdrsize]);

		alarm(sleepfor);
		if(len < 0)
			write(fd, opkt.buf, p - opkt.buf);

		len = read(fd, ipkt.buf, sizeof(ipkt.buf));
		timeout += alarm(0);
		timeout -= sleepfor;
		if(len == 0)
			break;

		if(len < Udphdrsize+4)
			continue;
		if(ipkt.buf[Udphdrsize+1] != ((tra>>16)&0xFF)
		|| ipkt.buf[Udphdrsize+2] != ((tra>>8)&0xFF)
		|| ipkt.buf[Udphdrsize+3] != ((tra>>0)&0xFF))
			continue;

		DEBUG("got dhcpv6 reply %x from %I on %I", ipkt.buf[Udphdrsize+0], ipkt.raddr, ipkt.ifcaddr);

		type |= (int)ipkt.buf[Udphdrsize+0]<<8;
		switch(type){
		case ADVERTISE << 8 | SOLICIT:
		case REPLY << 8 | REQUEST:
			goto Response;
		default:
			return -1;
		}
	}
	return -1;

Response:
	for(p = ipkt.buf + Udphdrsize + 4, e = ipkt.buf + len; p < e; p = x) {
		if (p+4 > e)
			return -1;

		opt = (int)p[0] << 8 | p[1];
		len = (int)p[2] << 8 | p[3];
		p += 4;
		x = p+len;
		if (x > e)
			return -1;

		DEBUG("got dhcpv6 option %x: [%d] %.*H", opt, len, len, p);

		switch(opt){
		case 0x01:		/* client identifier */
			continue;
		case 0x02:		/* server identifier */
			if(len < 1 || len > sizeof(sid))
				break;
			sidlen = len;
			memmove(sid, p, sidlen);
			continue;
		case 0x03:		/* IA for non-temporary address */
			if(p+12+4+IPaddrlen+2*4 > x)
				break;
			/* skip IAID, T1, T2 */
			p += 12;
			/* IA Addresss */
			if(p[0] != 0x00 || p[1] != 0x05
			|| p[2] != 0x00 || p[3] != IPaddrlen+2*4)
				break;
			p += 4;
			memset(conf.mask, 0xFF, IPaddrlen);
			memmove(conf.laddr, p, IPaddrlen);
			continue;
		case 0x17:	/* dns servers */
			if(len % IPaddrlen)
				break;
			addaddrs(conf.dns, sizeof(conf.dns), p, len);
			continue;
		default:
			DEBUG("unknown dhcpv6 option %x", opt);
			continue;
		}
		warning("dhcpv6: malformed option %x: [%d] %.*H", opt, len, len, x-len);
	}

	return 0;
}

void
dhcpv6query(void)
{
	int fd;

	fd = openlisten();
	if(transaction(fd, SOLICIT, 5000) < 0)
		goto out;
	if(!validip(conf.laddr))
		goto out;
	if(transaction(fd, REQUEST, 10000) < 0)
		goto out;
out:
	close(fd);
}
