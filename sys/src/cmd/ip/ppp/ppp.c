/*
 * ppp - point-to-point protocol, rfc1331
 */
#include <u.h>
#include <libc.h>
#include <auth.h>
#include <bio.h>
#include <ip.h>
#include <libsec.h>
#include <ndb.h>
#include "ppp.h"

#define PATH 128

static	int	baud;
static	int	nocompress;
static 	int	pppframing = 1;
static	int	noipcompress;
static	int	server;
static	int	noauth;
static	int	dying;		/* flag to signal to all threads its time to go */
static	int	primary;
static	int	proxy;
static	char	*chatfile;

int	debug;
char*	LOG = "ppp";
char*	keyspec = "";

/*
 * Calculate FCS - rfc 1331
 */
ushort fcstab[256] =
{
      0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
      0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
      0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
      0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
      0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
      0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
      0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
      0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
      0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
      0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
      0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
      0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
      0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
      0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
      0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
      0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
      0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
      0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
      0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
      0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
      0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
      0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
      0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
      0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
      0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
      0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
      0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
      0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
      0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
      0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
      0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
      0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

static char *snames[] =
{
	"Sclosed",
	"Sclosing",
	"Sreqsent",
	"Sackrcvd",
	"Sacksent",
	"Sopened",
};

static	void		authtimer(PPP*);
static	void		chapinit(PPP*);
static	void		config(PPP*, Pstate*, int);
static	int		euitov6(Ipaddr, uchar*, int);
static	uchar*		escapeuchar(PPP*, ulong, uchar*, ushort*);
static	void		getchap(PPP*, Block*);
static	Block*		getframe(PPP*, int*);
static	void		getlqm(PPP*, Block*);
static	int		getopts(PPP*, Pstate*, Block*);
static	void		getpap(PPP*, Block*);
static	void		init(PPP*);
static	void		invalidate(Ipaddr);
static	void		ipinproc(PPP*);
static	char*		ipopen(PPP*);
static	void		mediainproc(PPP*);
static	void		newstate(PPP*, Pstate*, int);
static	void		papinit(PPP*);
static	void		pinit(PPP*, Pstate*);
static	void		ppptimer(PPP*);
static	void		printopts(Pstate*, Block*, int);
static	void		ptimer(PPP*, Pstate*);
static	int		putframe(PPP*, int, Block*);
static	void		putlqm(PPP*);
static	void		putpaprequest(PPP*);
static	void		rcv(PPP*, Pstate*, Block*);
static	void		rejopts(PPP*, Pstate*, Block*, int);
static	void		sendechoreq(PPP*, Pstate*);
static	void		sendtermreq(PPP*, Pstate*);
static	void		setphase(PPP*, int);
static	void		terminate(PPP*, char*, int);
static	int		validv4(Ipaddr);
static	int		validv6(Ipaddr);
static  void		dmppkt(char *s, uchar *a, int na);

void
pppopen(PPP *ppp, int mediain, int mediaout, int shellin, char *net, char *dev,
	Ipaddr ipaddr[2], Ipaddr remip[2],
	int mtu, int framing, char *duid)
{
	int i;

	ppp->ipfd = -1;

	invalidate(ppp->remote);
	invalidate(ppp->local);
	invalidate(ppp->curremote);
	invalidate(ppp->curlocal);
	invalidate(ppp->dns[0]);
	invalidate(ppp->dns[1]);
	invalidate(ppp->wins[0]);
	invalidate(ppp->wins[1]);

	invalidate(ppp->local6);
	invalidate(ppp->remote6);
	invalidate(ppp->curremote6);
	invalidate(ppp->curlocal6);

	ppp->dev = dev;
	ppp->duid = duid;

	ppp->mediain = mediain;
	ppp->mediaout = mediaout;

	ppp->shellin = shellin;

	for(i=0; i<2; i++){
		if(validv4(ipaddr[i])){
			ipmove(ppp->local, ipaddr[i]);
			ppp->localfrozen = 1;
		} else if(validv6(ipaddr[i])){
			ipmove(ppp->local6, ipaddr[i]);
			ppp->local6frozen = 1;
		}
		if(validv4(remip[i])){
			ipmove(ppp->remote, remip[i]);
			ppp->remotefrozen = 1;
		} else if(validv6(remip[i])){
			ipmove(ppp->remote6, remip[i]);
			ppp->remote6frozen = 1;
		}
	}

	ppp->mtu = mtu;
	ppp->mru = mtu;
	ppp->framing = framing;
	ppp->net = net;

	init(ppp);
	mediainproc(ppp);
	terminate(ppp, "mediainproc", 0);
}

static void
init(PPP* ppp)
{
	if(ppp->lcp == nil){
		/* prevent them from being free'd */
		ppp->inbuf[0] = *allocb(Buflen);
		ppp->outbuf[0] = *allocb(Buflen);

		ppp->lcp = mallocz(sizeof(*ppp->lcp), 1);
		ppp->lcp->proto = Plcp;
		ppp->lcp->state = Sclosed;

		ppp->ccp = mallocz(sizeof(*ppp->ccp), 1);
		ppp->ccp->proto = Pccp;
		ppp->ccp->state = Sclosed;

		ppp->ipcp = mallocz(sizeof(*ppp->ipcp), 1);
		ppp->ipcp->proto = Pipcp;
		ppp->ipcp->state = Sclosed;

		ppp->ipv6cp = mallocz(sizeof(*ppp->ipv6cp), 1);
		ppp->ipv6cp->proto = Pipv6cp;
		ppp->ipv6cp->state = Sclosed;

		ppp->chap = mallocz(sizeof(*ppp->chap), 1);
		ppp->chap->proto = APmschapv2;
		ppp->chap->state = Cunauth;
		auth_freechal(ppp->chap->cs);
		ppp->chap->cs = nil;

		switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
		case -1:
			terminate(ppp, "forking ppptimer", 1);
		case 0:
			ppptimer(ppp);
			exits(nil);
		}
	}

	ppp->ctcp = compress_init(ppp->ctcp);
	pinit(ppp, ppp->lcp);
	setphase(ppp, Plink);
}

static void
setphase(PPP *ppp, int phase)
{
	int oldphase;

	oldphase = ppp->phase;

	ppp->phase = phase;
	switch(phase){
	default:
		terminate(ppp, "phase error", 1);
	case Pdead:
		terminate(ppp, "protocol", 0);
		break;
	case Plink:
		/* link down */
		switch(oldphase) {
		case Pauth:
			auth_freechal(ppp->chap->cs);
			ppp->chap->cs = nil;
			ppp->chap->state = Cunauth;
			break;
		case Pnet:
			auth_freechal(ppp->chap->cs);
			ppp->chap->cs = nil;
			ppp->chap->state = Cunauth;
			newstate(ppp, ppp->ccp, Sclosed);
			newstate(ppp, ppp->ipcp, Sclosed);
			newstate(ppp, ppp->ipv6cp, Sclosed);
		}
		break;
	case Pauth:
		if(server) {
			chapinit(ppp);
		} else {
			switch (ppp->chap->proto) {
			case APpasswd:
				papinit(ppp);
				break;
			case APmd5:
			case APmschap:
			case APmschapv2:
				break;
			default:
				setphase(ppp, Pnet);
				break;
			}
		}
		break;
	case Pnet:
		pinit(ppp, ppp->ccp);
		pinit(ppp, ppp->ipcp);
		pinit(ppp, ppp->ipv6cp);
		break;
	}
}

static void
pinit(PPP *ppp, Pstate *p)
{
	p->timeout = 0;

	switch(p->proto){
	case Plcp:
		ppp->magic = truerand();
		ppp->xctlmap = 0xffffffff;
		ppp->period = 0;
		p->optmask = 0xffffffff;
		if(!server)
			p->optmask &=  ~Fauth;
		ppp->rctlmap = 0;
		ppp->ipcp->state = Sclosed;
		ppp->ipcp->optmask = 0xffffffff;
		if(noipcompress) {
			p->optmask &= ~Fac;
			ppp->ipcp->optmask &= ~Fipaddrs;
		}
		if(nocompress) {
			p->optmask &= ~Fpc;
			ppp->ipcp->optmask &= ~Fipcompress;
		}
		ppp->ipv6cp->state = Sclosed;
		ppp->ipv6cp->optmask = 0xffffffff;
		p->echoack = 0;
		p->echotimeout = 0;

		/* quality goo */
		ppp->timeout = 0;
		memset(&ppp->in, 0, sizeof(ppp->in));
		memset(&ppp->out, 0, sizeof(ppp->out));
		memset(&ppp->pin, 0, sizeof(ppp->pin));
		memset(&ppp->pout, 0, sizeof(ppp->pout));
		memset(&ppp->sin, 0, sizeof(ppp->sin));
		break;
	case Pccp:
		if(nocompress)
			p->optmask = 0;
		else
			p->optmask = Fcmppc;

		if(ppp->ctype != nil)
			(*ppp->ctype->fini)(ppp->cstate);
		ppp->ctype = nil;
		ppp->ctries = 0;
		ppp->cstate = nil;

		if(ppp->unctype)
			(*ppp->unctype->fini)(ppp->uncstate);
		ppp->unctype = nil;
		ppp->uncstate = nil;
		break;
	case Pipcp:
		p->optmask = 0xffffffff;
		ppp->ctcp = compress_init(ppp->ctcp);
		break;
	case Pipv6cp:
		p->optmask = 0xffffffff;
		break;
	}
	p->confid = p->rcvdconfid = -1;
	config(ppp, p, 1);
	newstate(ppp, p, Sreqsent);
}

/*
 *  change protocol to a new state.
 */
static void
newstate(PPP *ppp, Pstate *p, int state)
{
	char *err;

	netlog("ppp: %ux %s->%s ctlmap %lux/%lux flags %lux mtu %ld mru %ld\n",
		p->proto, snames[p->state], snames[state], ppp->rctlmap,
		ppp->xctlmap, p->flags,
		ppp->mtu, ppp->mru);
	syslog(0, LOG, "%ux %s->%s ctlmap %lux/%lux flags %lux mtu %ld mru %ld",
		p->proto, snames[p->state], snames[state], ppp->rctlmap,
		ppp->xctlmap, p->flags,
		ppp->mtu, ppp->mru);

	if(p->proto == Plcp) {
		if(state == Sopened)
			setphase(ppp, noauth? Pnet : Pauth);
		else if(state == Sclosed)
			setphase(ppp, Pdead);
		else if(p->state == Sopened)
			setphase(ppp, Plink);
	}

	if(p->proto == Pccp && state == Sopened) {
		if(ppp->unctype)
			(*ppp->unctype->fini)(ppp->uncstate);
		ppp->unctype = nil;
		ppp->uncstate = nil;
		if(p->optmask & Fcmppc) {
			ppp->unctype = &uncmppc;
			ppp->uncstate = (*uncmppc.init)(ppp);
		}
		if(p->optmask & Fcthwack){
			ppp->unctype = &uncthwack;
			ppp->uncstate = (*uncthwack.init)(ppp);
		}
	}

	if((p->proto == Pipcp || p->proto == Pipv6cp) && state == Sopened) {
		if(server && !noauth && ppp->chap->state != Cauthok)
			abort();

		err = ipopen(ppp);
		if(err != nil)
			terminate(ppp, err, 1);
	}

	p->state = state;
}

/*
 * getframe returns (protocol, information)
 */
static Block*
getframe(PPP *ppp, int *protop)
{
	uchar *p, *from, *to;
	int n, proto;
	ulong c;
	ushort fcs;
	Block *buf, *b;

	*protop = 0;
	if(ppp->framing == 0) {
		/* reuse inbuf to avoid allocation */
		b = resetb(ppp->inbuf);

		n = read(ppp->mediain, b->wptr, BALLOC(b));
		if(n <= 0){
			syslog(0, LOG, "medium read returns %d: %r", n);
			return nil;
		}
		dmppkt("RX", b->wptr, n);
		b->wptr += n;

		if(pppframing && b->rptr[0] == PPP_addr && b->rptr[1] == PPP_ctl)
			b->rptr += 2;
		proto = *b->rptr++;
		if((proto & 0x1) == 0)
			proto = (proto<<8) | *b->rptr++;
		if(b->rptr >= b->wptr)
			return nil;

		ppp->in.uchars += n;
		ppp->in.packets++;
		*protop = proto;
		return b;
	}

	buf = ppp->inbuf;
	for(;;){
		/* read till we hit a frame uchar or run out of room */
		for(p = buf->rptr; buf->wptr < buf->lim;){
			for(; p < buf->wptr; p++)
				if(*p == HDLC_frame)
					break;
			if(p != buf->wptr)
				break;
			n = read(ppp->mediain, buf->wptr, BALLOC(buf));
			if(n <= 0){
				syslog(0, LOG, "medium read returns %d: %r", n);
				buf->wptr = buf->rptr;
				return nil;
			}
 			dmppkt("RX", buf->wptr, n);
			buf->wptr += n;
		}

		/* copy into block, undoing escapes, and caculating fcs */
		fcs = PPP_initfcs;
		b = allocb(p - buf->rptr);
		to = b->wptr;
		for(from = buf->rptr; from != p;){
			c = *from++;
			if(c == HDLC_esc){
				if(from == p)
					break;
				c = *from++ ^ 0x20;
			} else if((c < 0x20) && (ppp->rctlmap & (1 << c)))
				continue;
			*to++ = c;
			fcs = (fcs >> 8) ^ fcstab[(fcs ^ c) & 0xff];
		}

		/* copy down what's left in buffer */
		p++;
		memmove(buf->rptr, p, buf->wptr - p);
		n = p - buf->rptr;
		buf->wptr -= n;
		b->wptr = to - 2;

		/* return to caller if checksum matches */
		if(fcs == PPP_goodfcs){
			if(b->rptr[0] == PPP_addr && b->rptr[1] == PPP_ctl)
				b->rptr += 2;
			proto = *b->rptr++;
			if((proto & 0x1) == 0)
				proto = (proto<<8) | *b->rptr++;
			if(b->rptr < b->wptr){
				ppp->in.uchars += n;
				ppp->in.packets++;
				*protop = proto;
				return b;
			}
		} else if(BLEN(b) > 0){
			if(ppp->ctcp)
				compress_error(ppp->ctcp);
			ppp->in.discards++;
			netlog("ppp: discard len %zd/%zd cksum %ux (%ux %ux %ux %ux)\n",
				BLEN(b), BLEN(buf), fcs, b->rptr[0],
				b->rptr[1], b->rptr[2], b->rptr[3]);
		}

		freeb(b);
	}
}

/* send a PPP frame */
static int
putframe(PPP *ppp, int proto, Block *b)
{
	uchar *to, *from;
	ushort fcs;
	ulong ctlmap;
	uchar c;

	ppp->out.packets++;

	if(proto == Plcp)
		ctlmap = 0xffffffff;
	else
		ctlmap = ppp->xctlmap;

	/* make sure we have head room */
	assert(b->rptr - b->base >= 4);

	netlog("ppp: putframe 0x%ux %zd\n", proto, BLEN(b));

	/* add in the protocol and address, we'd better have left room */
	from = b->rptr;
	*--from = proto;
	if(!(ppp->lcp->flags&Fpc) || proto > 0x100 || proto == Plcp)
		*--from = proto>>8;
	if(pppframing && (!(ppp->lcp->flags&Fac) || proto == Plcp)){
		*--from = PPP_ctl;
		*--from = PPP_addr;
	}

	qlock(&ppp->outlock);
	if(ppp->framing == 0)
		to = b->wptr;
	else {
		to = ppp->outbuf->rptr;

		/* escape and checksum the body */
		fcs = PPP_initfcs;
	
		/* add frame marker */
		*to++ = HDLC_frame;

		for(; from < b->wptr; from++){
			c = *from;
			if(c == HDLC_frame || c == HDLC_esc
			   || (c < 0x20 && ((1<<c) & ctlmap))){
				*to++ = HDLC_esc;
				*to++ = c ^ 0x20;
			} else 
				*to++ = c;
			fcs = (fcs >> 8) ^ fcstab[(fcs ^ c) & 0xff];
		}

		/* add on and escape the checksum */
		fcs = ~fcs;
		c = fcs;
		if(c == HDLC_frame || c == HDLC_esc
		   || (c < 0x20 && ((1<<c) & ctlmap))){
			*to++ = HDLC_esc;
			*to++ = c ^ 0x20;
		} else 
			*to++ = c;
		c = fcs>>8;
		if(c == HDLC_frame || c == HDLC_esc
		   || (c < 0x20 && ((1<<c) & ctlmap))){
			*to++ = HDLC_esc;
			*to++ = c ^ 0x20;
		} else 
			*to++ = c;
	
		/* add frame marker */
		*to++ = HDLC_frame;

		from = ppp->outbuf->rptr;
 	}

	dmppkt("TX", from, to - from);
	if(write(ppp->mediaout, from, to - from) < 0){
		qunlock(&ppp->outlock);
		return -1;
	}
	ppp->out.uchars += to - from;
	qunlock(&ppp->outlock);
	return 0;
}

Block*
alloclcp(int code, int id, int len, Lcpmsg **mp)
{
	Block *b;
	Lcpmsg *m;

	/*
	 *  leave room for header
	 */
	b = allocb(len);

	m = (Lcpmsg*)b->wptr;
	m->code = code;
	m->id = id;
	b->wptr += 4;

	*mp = m;
	return b;
}


static void
putlo(Block *b, int type, ulong val)
{
	*b->wptr++ = type;
	*b->wptr++ = 6;
	hnputl(b->wptr, val);
	b->wptr += 4;
}

static void
putv4o(Block *b, int type, Ipaddr val)
{
	*b->wptr++ = type;
	*b->wptr++ = 6;
	v6tov4(b->wptr, val);
	b->wptr += 4;
}

static void
putso(Block *b, int type, ulong val)
{
	*b->wptr++ = type;
	*b->wptr++ = 4;
	hnputs(b->wptr, val);
	b->wptr += 2;
}

static void
puto(Block *b, int type)
{
	*b->wptr++ = type;
	*b->wptr++ = 2;
}

static void
putoeui64(Block *b, int type, uchar data[8])
{
	*b->wptr++ = type;
	*b->wptr++ = 8+2;
	memmove(b->wptr, data, 8);
	b->wptr += 8;
}

/*
 *  send configuration request
 */
static void
config(PPP *ppp, Pstate *p, int newid)
{
	Block *b;
	Lcpmsg *m;
	int id;

	if(newid){
		id = p->id++;
		p->confid = id;
		p->timeout = Timeout;
	} else
		id = p->confid;
	b = alloclcp(Lconfreq, id, 256, &m);
	USED(m);

	switch(p->proto){
	case Plcp:
		if(p->optmask & Fctlmap)
			putlo(b, Octlmap, 0);	/* we don't want anything escaped */
		if(p->optmask & Fmagic)
			putlo(b, Omagic, ppp->magic);
		if(p->optmask & Fmtu)
			putso(b, Omtu, ppp->mru);
		if(p->optmask & Fauth) {
			*b->wptr++ = Oauth;
			*b->wptr++ = 5;
			hnputs(b->wptr, Pchap);
			b->wptr += 2;
			*b->wptr++ = ppp->chap->proto;
		}
		if(p->optmask & Fpc)
			puto(b, Opc);
		if(p->optmask & Fac)
			puto(b, Oac);
		break;
	case Pccp:
		if(p->optmask & Fcthwack)
			puto(b, Octhwack);
		else if(p->optmask & Fcmppc) {
			*b->wptr++ = Ocmppc;
			*b->wptr++ = 6;
			*b->wptr++ = 0;
			*b->wptr++ = 0;
			*b->wptr++ = 0;
			*b->wptr++ = 0x41;
		}
		break;
	case Pipcp:
		if(p->optmask & Fipaddr){
			syslog(0, LOG, "requesting IPv4 %I", ppp->local);
			putv4o(b, Oipaddr, ppp->local);
		}
		if(p->optmask & Fipdns)
			putv4o(b, Oipdns, ppp->dns[0]);
		if(p->optmask & Fipdns2)
			putv4o(b, Oipdns2, ppp->dns[1]);
		if(p->optmask & Fipwins)
			putv4o(b, Oipwins, ppp->wins[0]);
		if(p->optmask & Fipwins2)
			putv4o(b, Oipwins2, ppp->wins[1]);
		/*
		 * don't ask for header compression while data compression is still pending.
		 * perhaps we should restart ipcp negotiation if compression negotiation fails.
		 */
		if(!noipcompress && !ppp->ccp->optmask && (p->optmask & Fipcompress)) {
			*b->wptr++ = Oipcompress;
			*b->wptr++ = 6;
			hnputs(b->wptr, Pvjctcp);
			b->wptr += 2;
			*b->wptr++ = MAX_STATES-1;
			*b->wptr++ = 1;
		}
		break;
	case Pipv6cp:
		if(p->optmask & Fipv6eui){
			syslog(0, LOG, "requesting IPv6 %I", ppp->local6);
			putoeui64(b, Oipv6eui, &ppp->local6[8]);
		}
		break;
	}
	hnputs(m->len, BLEN(b));
	printopts(p, b, 1);
	putframe(ppp, p->proto, b);
	freeb(b);
}

static void
getipinfo(PPP *ppp)
{
	char buf[32];
	char *av[3];
	int ndns, nwins;
	Ndbtuple *t, *nt;
	Ipaddr ip;

	if(!validv4(ppp->local))
		return;

	av[0] = "dns";
	av[1] = "wins";
	snprint(buf, sizeof buf, "%I", ppp->local);
	t = csipinfo(ppp->net, "ip", buf, av, 2);
	ndns = nwins = 0;
	for(nt = t; nt != nil; nt = nt->entry){
		if (parseip(ip, nt->val) == -1 || !validv4(ip))
			continue;
		if(strcmp(nt->attr, "dns") == 0){
			if(ndns < 2)
				ipmove(ppp->dns[ndns++], ip);
		} else if(strcmp(nt->attr, "wins") == 0){
			if(nwins < 2)
				ipmove(ppp->wins[nwins++], ip);
		}
	}
	if(t != nil)
		ndbfree(t);
}

/*
 *  parse configuration request, sends an ack or reject packet
 *
 *	returns:	-1 if request was syntacticly incorrect
 *			 0 if packet was accepted
 *			 1 if packet was rejected
 */
static int
getopts(PPP *ppp, Pstate *p, Block *b)
{
	Lcpmsg *m, *repm;	
	Lcpopt *o;
	uchar *cp, *ap;
	ulong rejecting, nacking, flags, proto, chapproto;
	ulong mtu, ctlmap, period;
	ulong x;
	Block *repb;
	Comptype *ctype;
	Ipaddr ipaddr, ipaddr6;

	rejecting = 0;
	nacking = 0;
	flags = 0;

	/* defaults */
	invalidate(ipaddr);
	invalidate(ipaddr6);
	mtu = ppp->mtu;
	ctlmap = 0xffffffff;
	period = 0;
	ctype = nil;
	chapproto = 0;

	m = (Lcpmsg*)b->rptr;
	repb = alloclcp(Lconfack, m->id, BLEN(b), &repm);

	/* copy options into ack packet */
	memmove(repm->data, m->data, b->wptr - m->data);
	repb->wptr += b->wptr - m->data;

	/* look for options we don't recognize or like */
	for(cp = m->data; cp < b->wptr; cp += o->len){
		o = (Lcpopt*)cp;
		if(cp + o->len > b->wptr || o->len < 2){
			freeb(repb);
			netlog("ppp: bad option length %ux\n", o->type);
			return -1;
		}

		switch(p->proto){
		case Plcp:
			switch(o->type){
			case Oac:
				flags |= Fac;
				continue;
			case Opc:
				flags |= Fpc;
				continue;
			case Omtu:
				mtu = nhgets(o->data);
				continue;
			case Omagic:
				if(ppp->magic == nhgetl(o->data))
					netlog("ppp: possible loop\n");
				continue;
			case Octlmap:
				ctlmap = nhgetl(o->data);
				continue;
			case Oquality:
				proto = nhgets(o->data);
				if(proto != Plqm)
					break;
				x = nhgetl(o->data+2)*10;
				period = (x+Period-1)/Period;
				continue;
			case Oauth:
				proto = nhgets(o->data);
				if(proto == Ppasswd && !server){
					chapproto = APpasswd;
					continue;
				}
				if(proto != Pchap)
					break;
				if(o->data[2] != APmd5
				&& o->data[2] != APmschap
				&& o->data[2] != APmschapv2)
					break;
				chapproto = o->data[2];
				continue;
			}
			break;
		case Pccp:
			switch(o->type){
			case Octhwack:
				break;
			/*
				if(o->len == 2){
					ctype = &cthwack;
					continue;
				}
				if(!nacking){
					nacking = 1;
					repb->wptr = repm->data;
					repm->code = Lconfnak;
				}
				puto(repb, Octhwack);
				continue;
			*/
			case Ocmppc:
				x = nhgetl(o->data);

				/* stop ppp loops */
				if((x&0x41) == 0 || ppp->ctries++ > 5) {
					/*
					 * turn off requests as well - I don't think this
					 * is needed in the standard
					 */
					p->optmask &= ~Fcmppc;
					break;
				}
				if(rejecting)
					continue;
				if((x & 0x01000001) == 1){
					ctype = &cmppc;
					ppp->sendencrypted = (o->data[3]&0x40) == 0x40;
					continue;
				}
				if(!nacking){
					nacking = 1;
					repb->wptr = repm->data;
					repm->code = Lconfnak;
				}
				*repb->wptr++ = Ocmppc;
				*repb->wptr++ = 6;
				*repb->wptr++ = 0;
				*repb->wptr++ = 0;
				*repb->wptr++ = 0;
				*repb->wptr++ = 0x41;
				continue;
			}
			break;
		case Pipcp:
			switch(o->type){
			case Oipaddr:	
				v4tov6(ipaddr, o->data);
				if(!validv4(ppp->remote))
					continue;
				if(!validv4(ipaddr) && !rejecting){
					/* other side requesting an address */
					if(!nacking){
						nacking = 1;
						repb->wptr = repm->data;
						repm->code = Lconfnak;
					}
					putv4o(repb, Oipaddr, ppp->remote);
				}
				continue;
			case Oipdns:
				ap = ppp->dns[0];
				goto ipinfo;
			case Oipdns2:	
				ap = ppp->dns[1];
				goto ipinfo;
			case Oipwins:	
				ap = ppp->wins[0];
				goto ipinfo;
			case Oipwins2:
				ap = ppp->wins[1];
				goto ipinfo;
			ipinfo:
				if(!validv4(ap))
					getipinfo(ppp);
				if(!validv4(ap))
					break;
				v4tov6(ipaddr, o->data);
				if(!validv4(ipaddr) && !rejecting){
					/* other side requesting an address */
					if(!nacking){
						nacking = 1;
						repb->wptr = repm->data;
						repm->code = Lconfnak;
					}
					putv4o(repb, o->type, ap);
				}
				continue;
			case Oipcompress:
				/*
				 * don't compress tcp header if we've negotiated data compression.
				 * tcp header compression has very poor performance if there is an error.
				 */
				proto = nhgets(o->data);
				if(noipcompress || proto != Pvjctcp || ppp->ctype != nil)
					break;
				if(compress_negotiate(ppp->ctcp, o->data+2) < 0)
					break;
				flags |= Fipcompress;
				continue;
			}
			break;
		case Pipv6cp:
			switch(o->type){
			case Oipv6eui:	
				euitov6(ipaddr6, o->data, o->len-2);
				if(!validv6(ppp->remote6))
					continue;
				if(!validv6(ipaddr6) && !rejecting){
					/* other side requesting an address */
					if(!nacking){
						nacking = 1;
						repb->wptr = repm->data;
						repm->code = Lconfnak;
					}
					putoeui64(repb, Oipv6eui, &ppp->remote6[8]);
				}
				continue;
			}
			break;
		}

		/* come here if option is not recognized */
		if(!rejecting){
			rejecting = 1;
			repb->wptr = repm->data;
			repm->code = Lconfrej;
		}
		netlog("ppp: bad %ux option %d\n", p->proto, o->type);
		memmove(repb->wptr, o, o->len);
		repb->wptr += o->len;
	}

	/* permanent changes only after we know that we liked the packet */
	if(!rejecting && !nacking){
		switch(p->proto){
		case Plcp:
			ppp->period = period;
			ppp->xctlmap = ctlmap;
			if(mtu > Maxmtu)
				mtu = Maxmtu;
			if(mtu < Minmtu)
				mtu = Minmtu;
			ppp->mtu = mtu;
			if(chapproto)
				ppp->chap->proto = chapproto;
			
			break;
		case Pccp:
			if(ppp->ctype != nil){
				(*ppp->ctype->fini)(ppp->cstate);
				ppp->cstate = nil;
			}
			ppp->ctype = ctype;
			if(ctype)
				ppp->cstate = (*ctype->init)(ppp);
			break;
		case Pipcp:
			if(validv4(ipaddr) && ppp->remotefrozen == 0)
 				ipmove(ppp->remote, ipaddr);
			break;
		case Pipv6cp:
			if(validv6(ipaddr6) && ppp->remote6frozen == 0)
 				ipmove(ppp->remote6, ipaddr6);
			break;
		}
		p->flags = flags;
	}

	hnputs(repm->len, BLEN(repb));
	printopts(p, repb, 1);
	putframe(ppp, p->proto, repb);
	freeb(repb);

	return rejecting || nacking;
}
static void
dmppkt(char *s, uchar *a, int na)
{
	int i;

	if (debug < 3)
		return;

	fprint(2, "%s", s);
	for(i = 0; i < na; i++)
		fprint(2, " %.2ux", a[i]);
	fprint(2, "\n");
}

static void
dropoption(Pstate *p, Lcpopt *o)
{
	switch(p->proto){
	case Pipcp:
		switch(o->type){
		case Oipaddr:
			/* never drop it */
			return;
		case Oipdns:
			p->optmask &= ~Fipdns;
			return;
		case Oipwins:
			p->optmask &= ~Fipwins;
			return;
		case Oipdns2:
			p->optmask &= ~Fipdns2;
			return;
		case Oipwins2:
			p->optmask &= ~Fipwins2;
			return;
		}
		break;
	case Pipv6cp:
		switch(o->type){
		case Oipv6eui:
			/* never drop it */
			return;
		}
		break;
	}
	if(o->type < 8*sizeof(p->optmask))
		p->optmask &= ~(1<<o->type);
}

/*
 *  parse configuration rejection, just stop sending anything that they
 *  don't like (except for ipcp address nak).
 */
static void
rejopts(PPP *ppp, Pstate *p, Block *b, int code)
{
	Lcpmsg *m;
	Lcpopt *o;
	Ipaddr newip;

	/* just give up trying what the other side doesn't like */
	m = (Lcpmsg*)b->rptr;
	for(b->rptr = m->data; b->rptr < b->wptr; b->rptr += o->len){
		o = (Lcpopt*)b->rptr;
		if(b->rptr + o->len > b->wptr || o->len < 2){
			netlog("ppp: bad roption length %ux\n", o->type);
			return;
		}

		if(code == Lconfrej){
			dropoption(p, o);
			netlog("ppp: %ux rejecting %d\n",
					p->proto, o->type);
			continue;
		}

		switch(p->proto){
		case Plcp:
			switch(o->type){
			case Octlmap:
				ppp->rctlmap = nhgetl(o->data);
				break;
			case Oauth:
				/* don't allow client to request no auth */
				/* could try different auth protocol here */
				terminate(ppp, "chap rejected", 0);
				return;
			default:
				dropoption(p, o);
				break;
			};
			break;
		case Pccp:
			switch(o->type){
			default:
				dropoption(p, o);
				break;
			}
			break;
		case Pipcp:
			switch(o->type){
			case Oipaddr:
				v4tov6(newip, o->data);
				syslog(0, LOG, "rejected IPv4 addr %I with %I", ppp->local, newip);
				/* if we're a server, don't let other end change our addr */
				if(ppp->localfrozen){
					dropoption(p, o);
					break;
				}

				/* accept whatever server tells us */
				if(!validv4(ppp->local)){
					ipmove(ppp->local, newip);
					dropoption(p, o);
					break;
				}

				/* if he didn't like our addr, ask for a generic one */
				if(!validv4(newip)){
					invalidate(ppp->local);
					break;
				}

				/* if he gives us something different, use it anyways */
				ipmove(ppp->local, newip);
				dropoption(p, o);
				break;
			case Oipdns:
				if (!validv4(ppp->dns[0])){
					v4tov6(ppp->dns[0], o->data);
					dropoption(p, o);
					break;
				}
				v4tov6(newip, o->data);
				if(!validv4(newip)){
					invalidate(ppp->dns[0]);
					break;
				}
				v4tov6(ppp->dns[0], o->data);
				dropoption(p, o);
				break;
			case Oipwins:
				if (!validv4(ppp->wins[0])){
					v4tov6(ppp->wins[0], o->data);
					dropoption(p, o);
					break;
				}
				v4tov6(newip, o->data);
				if(!validv4(newip)){
					invalidate(ppp->wins[0]);
					break;
				}
				v4tov6(ppp->wins[0], o->data);
				dropoption(p, o);
				break;
			case Oipdns2:
				if (!validv4(ppp->dns[1])){
					v4tov6(ppp->dns[1], o->data);
					dropoption(p, o);
					break;
				}
				v4tov6(newip, o->data);
				if(!validv4(newip)){
					invalidate(ppp->dns[1]);
					break;
				}
				v4tov6(ppp->dns[1], o->data);
				dropoption(p, o);
				break;
			case Oipwins2:
				if (!validv4(ppp->wins[1])){
					v4tov6(ppp->wins[1], o->data);
					dropoption(p, o);
					break;
				}
				v4tov6(newip, o->data);
				if(!validv4(newip)){
					invalidate(ppp->wins[1]);
					break;
				}
				v4tov6(ppp->wins[1], o->data);
				dropoption(p, o);
				break;
			default:
				dropoption(p, o);
				break;
			}
			break;
		case Pipv6cp:
			switch(o->type){
			case Oipv6eui:
				euitov6(newip, o->data, o->len-2);
				syslog(0, LOG, "rejected IPv6 addr %I with %I", ppp->local6, newip);

				/* if we're a server, don't let other end change our addr */
				if(ppp->local6frozen){
					dropoption(p, o);
					break;
				}
				/* accept whatever server tells us */
				if(!validv6(ppp->local6)){
					ipmove(ppp->local6, newip);
					dropoption(p, o);
					break;
				}
				/* if he didn't like our addr, ask for a generic one */
				if(!validv6(newip)){
					invalidate(ppp->local6);
					break;
				}
				/* if he gives us something different, use it anyways */
				ipmove(ppp->local6, newip);
				dropoption(p, o);
				break;
			default:
				dropoption(p, o);
				break;
			}
		}
	}
}


/*
 *  put a messages through the lcp or ipcp state machine.  They are
 *  very similar.
 */
static void
rcv(PPP *ppp, Pstate *p, Block *b)
{
	ulong len;
	int err;
	Lcpmsg *m;
	int proto;

	if(BLEN(b) < 4){
		netlog("ppp: short lcp message\n");
		freeb(b);
		return;
	}
	m = (Lcpmsg*)b->rptr;
	len = nhgets(m->len);
	if(BLEN(b) < len){
		netlog("ppp: short lcp message\n");
		freeb(b);
		return;
	}

	netlog("ppp: %ux rcv %d len %ld id %d/%d/%d\n",
		p->proto, m->code, len, m->id, p->confid, p->id);

	if(p->proto != Plcp && ppp->lcp->state != Sopened){
		netlog("ppp: non-lcp with lcp not open\n");
		freeb(b);
		return;
	}

	qlock(ppp);
	switch(m->code){
	case Lconfreq:
		printopts(p, b, 0);
		err = getopts(ppp, p, b);
		if(err < 0)
			break;

		if(m->id == p->rcvdconfid)
			break;			/* don't change state for duplicates */

		switch(p->state){
		case Sackrcvd:
			if(err)
				break;
			newstate(ppp, p, Sopened);
			break;
		case Sclosed:
		case Sopened:
			config(ppp, p, 1);
			if(err == 0)
				newstate(ppp, p, Sacksent);
			else
				newstate(ppp, p, Sreqsent);
			break;
		case Sreqsent:
		case Sacksent:
			if(err == 0)
				newstate(ppp, p, Sacksent);
			else
				newstate(ppp, p, Sreqsent);
			break;
		}
		break;
	case Lconfack:
		if(p->confid != m->id){
			/* ignore if it isn't the message we're sending */
			netlog("ppp: dropping confack\n");
			break;
		}
		p->confid = -1;		/* ignore duplicates */
		p->id++;		/* avoid sending duplicates */

		netlog("ppp: recv confack\n");
		switch(p->state){
		case Sopened:
		case Sackrcvd:
			config(ppp, p, 1);
			newstate(ppp, p, Sreqsent);
			break;
		case Sreqsent:
			newstate(ppp, p, Sackrcvd);
			break;
		case Sacksent:
			newstate(ppp, p, Sopened);
			break;
		}
		break;
	case Lconfrej:
	case Lconfnak:
		if(p->confid != m->id) {
			/* ignore if it isn't the message we're sending */
			netlog("ppp: dropping confrej or confnak\n");
			break;
		}
		p->confid = -1;		/* ignore duplicates */
		p->id++;		/* avoid sending duplicates */

		switch(p->state){
		case Sopened:
		case Sackrcvd:
			config(ppp, p, 1);
			newstate(ppp, p, Sreqsent);
			break;
		case Sreqsent:
		case Sacksent:
			printopts(p, b, 0);
			rejopts(ppp, p, b, m->code);
			if(dying)
				break;
			config(ppp, p, 1);
			break;
		}
		break;
	case Ltermreq:
		m->code = Ltermack;
		putframe(ppp, p->proto, b);

		switch(p->state){
		case Sackrcvd:
		case Sacksent:
			newstate(ppp, p, Sreqsent);
			break;
		case Sopened:
			newstate(ppp, p, Sclosing);
			break;
		}
		break;
	case Ltermack:
		if(p->termid != m->id)	/* ignore if it isn't the message we're sending */
			break;

		if(p->proto == Plcp)
			ppp->ipcp->state = Sclosed;
		switch(p->state){
		case Sclosing:
			newstate(ppp, p, Sclosed);
			break;
		case Sackrcvd:
			newstate(ppp, p, Sreqsent);
			break;
		case Sopened:
			config(ppp, p, 0);
			newstate(ppp, p, Sreqsent);
			break;
		}
		break;
	case Lcoderej:
		//newstate(ppp, p, Sclosed);
		syslog(0, LOG, "code reject %d", m->data[0]);
		break;
	case Lprotorej:
		proto = nhgets(m->data);
		netlog("ppp: proto reject %ux\n", proto);
		if(proto == Pccp)
			newstate(ppp, ppp->ccp, Sclosed);
		break;
	case Lechoreq:
		if(BLEN(b) < 8){
			netlog("ppp: short lcp echo request\n");
			freeb(b);
			return;
		}
		m->code = Lechoack;
		hnputl(m->data, ppp->magic);
		putframe(ppp, p->proto, b);
		break;
	case Lechoack:
		p->echoack = 1;
		break;
	case Ldiscard:
		/* nothing to do */
		break;
	case Lresetreq:
		if(p->proto != Pccp)
			break;
		ppp->stat.compreset++;
		if(ppp->ctype != nil)
			b = (*ppp->ctype->resetreq)(ppp->cstate, b);
		if(b != nil) {
			m = (Lcpmsg*)b->rptr;
			m->code = Lresetack;
			putframe(ppp, p->proto, b);
		}
		break;
	case Lresetack:
		if(p->proto != Pccp)
			break;
		if(ppp->unctype != nil)
			(*ppp->unctype->resetack)(ppp->uncstate, b);
		break;
	}

	qunlock(ppp);
	freeb(b);
}

/*
 *  timer for protocol state machine
 */
static void
ptimer(PPP *ppp, Pstate *p)
{
	if(p->state == Sopened || p->state == Sclosed)
		return;

	p->timeout--;
	switch(p->state){
	case Sclosing:
		sendtermreq(ppp, p);
		break;
	case Sreqsent:
	case Sacksent:
		if(p->timeout <= 0)
			newstate(ppp, p, Sclosed);
		else {
			config(ppp, p, 0);
		}
		break;
	case Sackrcvd:
		if(p->timeout <= 0)
			newstate(ppp, p, Sclosed);
		else {
			config(ppp, p, 0);
			newstate(ppp, p, Sreqsent);
		}
		break;
	}
}

/* paptimer -- pap timer event handler
 *
 * If PAP authorization hasn't come through, resend an authreqst.  If
 * the maximum number of requests have been sent (~ 30 seconds), give
 * up.
 *
 */
static void
authtimer(PPP* ppp)
{
	if(ppp->chap->proto != APpasswd)
		return;

	if(ppp->chap->id < 21)
		putpaprequest(ppp);
	else {
		netlog("ppp: pap timed out--not authorized\n");
		terminate(ppp, "pap timeout", 0);
	}
}

static void
pingtimer(PPP* ppp)
{
	if(ppp->lcp->echotimeout == 0 || ppp->lcp->echoack)
		ppp->lcp->echotimeout = (3*4*1000+Period-1)/Period;
	else if(--(ppp->lcp->echotimeout) <= 0){
		netlog("ppp: echo request timeout\n");
		terminate(ppp, "echo timeout", 0);
		return;
	}
	ppp->lcp->echoack = 0;
	sendechoreq(ppp, ppp->lcp);
}


/*
 *  timer for ppp
 */
static void
ppptimer(PPP *ppp)
{
	while(!dying){
		sleep(Period);
		qlock(ppp);

		netlog("ppp: ppptimer\n");
		ptimer(ppp, ppp->lcp);
		if(ppp->lcp->state == Sopened) {
			switch(ppp->phase){
			case Pnet:
				ptimer(ppp, ppp->ccp);
				ptimer(ppp, ppp->ipcp);
				ptimer(ppp, ppp->ipv6cp);

				pingtimer(ppp);
				break;
			case Pauth:
				authtimer(ppp);
				break;
			}
		}

		/* link quality measurement */
		if(ppp->period && --(ppp->timeout) <= 0){
			ppp->timeout = ppp->period;
			putlqm(ppp);
		}

		qunlock(ppp);
	}
}

static void
ipconfig(int shell, char *net, char *dev, int mtu, int proxy, Ipaddr gate, Ipaddr dns[2], char *duid)
{
	fprint(shell, "ip/ipconfig -x %q ", net);
	if(!primary){
		/* don't write /net/ndb */
		fprint(shell, "-P ");
	} else {
		/* write dns servers in /net/ndb */
		if(dns != nil){
			int i;

			for(i = 0; i < 2; i++){
				if(validv4(dns[i]))
					fprint(shell, "-s %I ", dns[i]);
			}
		}
		/* set default gateway */
		if(gate != nil)
			fprint(shell, "-g %I ", gate);
	}
	/* allow dhcpv6 */
	if(duid != nil)
		fprint(shell, "-dU %q ", duid);
	if(mtu > 0)
		fprint(shell, "-m %d ", mtu);
	if(proxy)
		fprint(shell, "-y ");
	fprint(shell, "pkt %q ", dev);
}

static void
addip(int shell, char *net, char *dev, Ipaddr local, Ipaddr remote, int mtu, Ipaddr *dns)
{
	if(validv4(local) && validv4(remote)){
		ipconfig(shell, net, dev, mtu, proxy, remote, dns, nil);
		fprint(shell, "add %I 255.255.255.255 %I\n", local, remote);
	} else if(validv6(local)){
		ipconfig(shell, net, dev, mtu, 0, nil, nil, nil);
		fprint(shell, "add %I /64\n", local);
	}
}

static void
delip(int shell, char *net, char *dev, Ipaddr local, Ipaddr remote)
{
	if(validv4(local) && validv4(remote)){
		ipconfig(shell, net, dev, 0, 0, remote, nil, nil);
		fprint(shell, "del %I 255.255.255.255\n", local);
	} else if(validv6(local)){
		ipconfig(shell, net, dev, 0, 0, nil, nil, nil);
		fprint(shell, "del %I /64\n", local);
	}
}

static void
v6autoconfig(int shell, char *net, char *dev, Ipaddr remote, char *duid)
{
	if(server || !validv6(remote))
		return;
	ipconfig(shell, net, dev, 0, proxy, remote, nil, duid);
	fprint(shell, "ra6 recvra 1\n");
}

static char*
ipopen(PPP *ppp)
{
	static int ipinprocpid;
	int n, cfd, fd, pid;
	char path[128];
	char buf[128];

	if(ipinprocpid <= 0){
		snprint(path, sizeof path, "%s/ipifc/clone", ppp->net);
		cfd = open(path, ORDWR);
		if(cfd < 0)
			return "can't open ip interface";

		n = read(cfd, buf, sizeof(buf) - 1);
		if(n <= 0){
			close(cfd);
			return "can't open ip interface";
		}
		buf[n] = 0;
		snprint(path, sizeof path, "%s/ipifc/%s/data", ppp->net, buf);
		fd = open(path, ORDWR);
		if(fd < 0){
			close(cfd);
			return "can't open ip interface";
		}

		if(ppp->dev == nil)
			ppp->dev = smprint("%s/ipifc/%s", ppp->net, buf);

		if(fprint(cfd, "bind pkt %s", ppp->dev) < 0){
			close(fd);
			close(cfd);
			return "binding pkt to ip interface";
		}
		if(baud)
			fprint(cfd, "speed %d", baud);
		close(cfd);

		ppp->ipfd = fd;
		switch(pid = rfork(RFPROC|RFMEM|RFNOWAIT)){
		case -1:
			terminate(ppp, "forking ipinproc", 1);
		case 0:
			ipinproc(ppp);
			terminate(ppp, "ipinproc", 0);
			exits(nil);
		}
		ipinprocpid = pid;

		if(validv4(ppp->local)){
			if(!validv4(ppp->remote))
				ipmove(ppp->remote, ppp->local);
			syslog(0, LOG, "%I/%I", ppp->local, ppp->remote);
			addip(ppp->shellin, ppp->net, ppp->dev, ppp->local, ppp->remote, ppp->mtu, ppp->dns);
		}
		if(validv6(ppp->local6) && validv6(ppp->remote6)){
			syslog(0, LOG, "%I/%I", ppp->local6, ppp->remote6);
			addip(ppp->shellin, ppp->net, ppp->dev, ppp->local6, ppp->remote6, ppp->mtu, nil);
			v6autoconfig(ppp->shellin, ppp->net, ppp->dev, ppp->remote6, ppp->duid);
		}

		/* fork in background, main returns */
		fprint(ppp->shellin, "rc -I </fd/0 &; exit ''\n");
	} else {
		/* we may have changed addresses */
		if(ipcmp(ppp->local, ppp->curlocal) != 0 || ipcmp(ppp->remote, ppp->curremote) != 0){
			if(!validv4(ppp->remote))
				ipmove(ppp->remote, ppp->local);
			syslog(0, LOG, "%I/%I -> %I/%I", ppp->curlocal, ppp->curremote,
				ppp->local, ppp->remote);
			delip(ppp->shellin, ppp->net, ppp->dev, ppp->curlocal, ppp->curremote);
			addip(ppp->shellin, ppp->net, ppp->dev, ppp->local, ppp->remote, ppp->mtu, ppp->dns);
		}
		if(ipcmp(ppp->local6, ppp->curlocal6) != 0 || ipcmp(ppp->remote6, ppp->curremote6) != 0){
			syslog(0, LOG, "%I/%I -> %I/%I", ppp->curlocal6, ppp->curremote6,
				ppp->local6, ppp->remote6);
			delip(ppp->shellin, ppp->net, ppp->dev, ppp->curlocal6, ppp->curremote6);
			addip(ppp->shellin, ppp->net, ppp->dev, ppp->local6, ppp->remote6, ppp->mtu, nil);
			v6autoconfig(ppp->shellin, ppp->net, ppp->dev, ppp->remote6, ppp->duid);
		}
	}

	ipmove(ppp->curlocal, ppp->local);
	ipmove(ppp->curremote, ppp->remote);
	ipmove(ppp->curlocal6, ppp->local6);
	ipmove(ppp->curremote6, ppp->remote6);

	return nil;
}

/* return next input IP packet */
Block*
pppread(PPP *ppp)
{
	Block *b, *reply;
	int proto, len;
	Lcpmsg *m;

	while(!dying){
		b = getframe(ppp, &proto);
		if(b == nil)
			return nil;
		netlog("ppp: getframe 0x%ux %zd\n", proto, BLEN(b));
Again:
		switch(proto){
		case Pip:
			if(ppp->ipcp->state == Sopened)
				return b;
			netlog("ppp: IP recved: link not up\n");
			freeb(b);
			continue;

		case Pipv6:
			if(ppp->ipv6cp->state == Sopened)
				return b;
			netlog("ppp: IPv6 recved: link not up\n");
			freeb(b);
			continue;

		case Pvjctcp:
		case Pvjutcp:
			if(ppp->ipcp->state != Sopened){
				netlog("ppp: VJ tcp recved: link not up\n");
				freeb(b);
				continue;
			}
			ppp->stat.vjin++;
			b = tcpuncompress(ppp->ctcp, b, proto);
			if(b != nil)
				return b;
			ppp->stat.vjfail++;
			continue;

		case Pcdata:
			ppp->stat.uncomp++;
			if(ppp->ccp->state != Sopened){
				netlog("ppp: compressed data recved: link not up\n");
				freeb(b);
				continue;
			}
			if(ppp->unctype == nil) {
				netlog("ppp: compressed data recved: no compression\n");
				freeb(b);
				continue;
			}
			len = BLEN(b);
			reply = nil;
			b = (*ppp->unctype->uncompress)(ppp, b, &proto, &reply);
			if(reply != nil){
				/* send resetreq */
				ppp->stat.uncompreset++;
				putframe(ppp, Pccp, reply);
				freeb(reply);
			}
			if(b == nil)
				continue;
			ppp->stat.uncompin += len;
			ppp->stat.uncompout += BLEN(b);
			netlog("ppp: uncompressed frame %ux %d %d (%zd uchars)\n",
				proto, b->rptr[0], b->rptr[1], BLEN(b));
			goto Again;
		}

		switch(proto){
		case Plcp:
			rcv(ppp, ppp->lcp, b);
			break;
		case Pccp:
			rcv(ppp, ppp->ccp, b);
			break;
		case Pipcp:
			rcv(ppp, ppp->ipcp, b);
			break;
		case Pipv6cp:
			rcv(ppp, ppp->ipv6cp, b);
			break;
		case Plqm:
			getlqm(ppp, b);
			break;
		case Pchap:
			getchap(ppp, b);
			break;
		case Ppasswd:
			getpap(ppp, b);
			break;
		default:
			syslog(0, LOG, "unknown proto %ux", proto);
			if(ppp->lcp->state == Sopened){
				/* reject the protocol */
				b->rptr -= 6;
				m = (Lcpmsg*)b->rptr;
				m->code = Lprotorej;
				m->id = ++ppp->lcp->id;
				hnputs(m->data, proto);
				hnputs(m->len, BLEN(b));
				putframe(ppp, Plcp, b);
			}
			freeb(b);
			break;
		}
	}
	return nil;
}

typedef struct Iphdr Iphdr;
struct Iphdr
{
	uchar	vihl;		/* Version and header length */
	uchar	tos;		/* Type of service */
	uchar	length[2];	/* packet length */
	uchar	id[2];		/* Identification */
	uchar	frag[2];	/* Fragment information */
	uchar	ttl;		/* Time to live */
	uchar	proto;		/* Protocol */
	uchar	cksum[2];	/* Header checksum */
	uchar	src[4];		/* Ip source (uchar ordering unimportant) */
	uchar	dst[4];		/* Ip destination (uchar ordering unimportant) */
};

/* transmit an IP packet */
int
pppwrite(PPP *ppp, Block *b)
{
	int proto;
	int len, tot;
	Iphdr *ip;
	Ip6hdr *ip6;

	len = BLEN(b);
	ip = (Iphdr*)b->rptr;
	switch(ip->vihl & 0xF0){
	default:
	Badhdr:
		freeb(b);
		return len;
	case IP_VER4:
		if(len < IPV4HDR_LEN || (tot = nhgets(ip->length)) < IPV4HDR_LEN)
			goto Badhdr;
		if(b->wptr < (uchar*)ip + tot)
			goto Badhdr;
		b->wptr = (uchar*)ip + tot;

		qlock(ppp);
		if(ppp->ipcp->state != Sopened)
			goto Drop;
		proto = Pip;
		if(ppp->ipcp->flags & Fipcompress){
			b = compress(ppp->ctcp, b, &proto);
			if(b == nil)
				goto Drop;
			if(proto != Pip)
				ppp->stat.vjout++;
		}
		break;
	case IP_VER6:
		if(len < IPV6HDR_LEN)
			goto Badhdr;
		ip6 = (Ip6hdr*)ip;
		tot = IPV6HDR_LEN + nhgets(ip6->ploadlen);
		if(b->wptr < (uchar*)ip6 + tot)
			goto Badhdr;
		b->wptr = (uchar*)ip6 + tot;

		qlock(ppp);
		if(ppp->ipv6cp->state != Sopened)
			goto Drop;
		proto = Pipv6;
		break;
	}
	ppp->stat.ipsend++;

	if(ppp->ctype != nil) {
		b = (*ppp->ctype->compress)(ppp, proto, b, &proto);
		if(proto == Pcdata) {
			ppp->stat.comp++;
			ppp->stat.compin += len;
			ppp->stat.compout += BLEN(b);
		}
	} 

	if(putframe(ppp, proto, b) < 0)
		len = -1;
Drop:
	qunlock(ppp);
	freeb(b);
	return len;
}

static void
terminate(PPP *ppp, char *why, int fatal)
{
	if(dying++)
		return;

	syslog(0, LOG, "ppp: terminated: %s", why);

	if(ppp->dev != nil){
		delip(ppp->shellin, ppp->net, ppp->dev, ppp->curlocal, ppp->curremote);
		delip(ppp->shellin, ppp->net, ppp->dev, ppp->curlocal6, ppp->curremote6);
	}
	fprint(ppp->shellin, "exit %q\n", why);
	close(ppp->shellin);
	ppp->shellin = -1;

	close(ppp->ipfd);
	ppp->ipfd = -1;
	close(ppp->mediain);
	close(ppp->mediaout);
	ppp->mediain = -1;
	ppp->mediaout = -1;

	postnote(PNGROUP, getpid(), "die");

	if(fatal)
		sysfatal("%s", why);
}

static void
ipinproc(PPP *ppp)
{
	Block b[1] = *allocb(Buflen);
	int n;

	while(!dying){
		n = read(ppp->ipfd, b->wptr, BALLOC(b));
		if(n <= 0)
			break;
		b->wptr += n;
		if(pppwrite(ppp, b) < 0)
			break;
		resetb(b);
	}
}

static void
catchdie(void*, char *msg)
{
	if(strstr(msg, "die") != nil)
		noted(NCONT);
	else
		noted(NDFLT);
}

static void
hexdump(uchar *a, int na)
{
	int i;
	char buf[80];

	fprint(2, "dump %p %d\n", a, na);
	buf[0] = '\0';
	for(i=0; i<na; i++){
		sprint(buf+strlen(buf), " %.2ux", a[i]);
		if(i%16 == 7)
			sprint(buf+strlen(buf), " --");
		if(i%16==15){
			sprint(buf+strlen(buf), "\n");
			write(2, buf, strlen(buf));
			buf[0] = '\0';
		}
	}
	if(i%16){
		sprint(buf+strlen(buf), "\n");
		write(2, buf, strlen(buf));
	}
}

static void
mediainproc(PPP *ppp)
{
	Block *b;

	notify(catchdie);
	while(!dying){
		b = pppread(ppp);
		if(b == nil){
			syslog(0, LOG, "pppread return nil");
			break;
		}
		ppp->stat.iprecv++;
		if(debug > 1){
			netlog("ip write pkt %p %zd\n", b->rptr, BLEN(b));
			hexdump(b->rptr, BLEN(b));
		}
		if(write(ppp->ipfd, b->rptr, BLEN(b)) < 0) {
			syslog(0, LOG, "error writing to pktifc");
			break;
		}
		freeb(b);
	}

	netlog(": remote=%I/%I: ppp shutting down\n", ppp->remote, ppp->remote6);
	syslog(0, LOG, ": remote=%I/%I: ppp shutting down", ppp->remote, ppp->remote6);
	syslog(0, LOG, "\t\tppp send = %lud/%lud recv= %lud/%lud",
		ppp->out.packets, ppp->out.uchars,
		ppp->in.packets, ppp->in.uchars);
	syslog(0, LOG, "\t\tip send=%lud", ppp->stat.ipsend);
	syslog(0, LOG, "\t\tip recv=%lud notup=%lud badsrc=%lud",
		ppp->stat.iprecv, ppp->stat.iprecvnotup, ppp->stat.iprecvbadsrc);
	syslog(0, LOG, "\t\tcompress=%lud in=%lud out=%lud reset=%lud",
		ppp->stat.comp, ppp->stat.compin, ppp->stat.compout, ppp->stat.compreset);
	syslog(0, LOG, "\t\tuncompress=%lud in=%lud out=%lud reset=%lud",
		ppp->stat.uncomp, ppp->stat.uncompin, ppp->stat.uncompout,
		ppp->stat.uncompreset);
	syslog(0, LOG, "\t\tvjin=%lud vjout=%lud vjfail=%lud", 
		ppp->stat.vjin, ppp->stat.vjout, ppp->stat.vjfail);
}

/*
 *  link quality management
 */
static void
getlqm(PPP *ppp, Block *b)
{
	Qualpkt *p;

	p = (Qualpkt*)b->rptr;
	if(BLEN(b) == sizeof(Qualpkt)){
		ppp->in.reports++;
		ppp->pout.reports = nhgetl(p->peeroutreports);
		ppp->pout.packets = nhgetl(p->peeroutpackets);
		ppp->pout.uchars = nhgetl(p->peeroutuchars);
		ppp->pin.reports = nhgetl(p->peerinreports);
		ppp->pin.packets = nhgetl(p->peerinpackets);
		ppp->pin.discards = nhgetl(p->peerindiscards);
		ppp->pin.errors = nhgetl(p->peerinerrors);
		ppp->pin.uchars = nhgetl(p->peerinuchars);

		/* save our numbers at time of reception */
		memmove(&ppp->sin, &ppp->in, sizeof(Qualstats));

	}
	freeb(b);
	if(ppp->period == 0)
		putlqm(ppp);

}

static void
putlqm(PPP *ppp)
{
	Qualpkt *p;
	Block *b;

	b = allocb(sizeof(Qualpkt));
	b->wptr += sizeof(Qualpkt);
	p = (Qualpkt*)b->rptr;
	hnputl(p->magic, 0);

	/* heresay (what he last told us) */
	hnputl(p->lastoutreports, ppp->pout.reports);
	hnputl(p->lastoutpackets, ppp->pout.packets);
	hnputl(p->lastoutuchars, ppp->pout.uchars);

	/* our numbers at time of last reception */
	hnputl(p->peerinreports, ppp->sin.reports);
	hnputl(p->peerinpackets, ppp->sin.packets);
	hnputl(p->peerindiscards, ppp->sin.discards);
	hnputl(p->peerinerrors, ppp->sin.errors);
	hnputl(p->peerinuchars, ppp->sin.uchars);

	/* our numbers now */
	hnputl(p->peeroutreports, ppp->out.reports+1);
	hnputl(p->peeroutpackets, ppp->out.packets+1);
	hnputl(p->peeroutuchars, ppp->out.uchars+53/*hack*/);

	putframe(ppp, Plqm, b);
	freeb(b);
	ppp->out.reports++;
}

static char*
getaproto(int proto)
{
	switch(proto){
	case APmd5:
		return "chap";
	case APmschap:
		return "mschap";
	case APmschapv2:
		return "mschapv2";
	}
	return nil;
}

/*
 * init challenge response dialog
 */
static void
chapinit(PPP *ppp)
{
	Block *b;
	Lcpmsg *m;
	Chap *c;
	int len;

	c = ppp->chap;
	c->id++;
	if(c->ai != nil){
		auth_freeAI(c->ai);
		c->ai = nil;
	}
	if((c->cs = auth_challenge("proto=%q role=server", getaproto(c->proto))) == nil){
		char err[ERRMAX];
		snprint(err, sizeof(err), "auth_challenge: %r");
		terminate(ppp, err, 1);
	}
	syslog(0, LOG, ": remote=%I: sending %d byte challenge", ppp->remote, c->cs->nchal);
	len = 4 + 1 + c->cs->nchal + strlen(ppp->chapname);
	b = alloclcp(Cchallenge, c->id, len, &m);

	*b->wptr++ = c->cs->nchal;
	memmove(b->wptr, c->cs->chal, c->cs->nchal);
	b->wptr += c->cs->nchal;
	memmove(b->wptr, ppp->chapname, strlen(ppp->chapname));
	b->wptr += strlen(ppp->chapname);
	hnputs(m->len, len);
	putframe(ppp, Pchap, b);
	freeb(b);

	c->state = Cchalsent;
}

/*
 *  challenge response dialog
 */
static void
setppekey(PPP *ppp, int isserver)
{
	Chap *c = ppp->chap;

	switch(c->proto){
	case APmschap:
		if(c->ai == nil || c->ai->nsecret != 16)
			terminate(ppp, "could not get the encryption key", 1);
		memmove(ppp->sendkey, c->ai->secret, 16);
		memmove(ppp->recvkey, c->ai->secret, 16);
		break;
	case APmschapv2:
		if(c->ai == nil || c->ai->nsecret != 16+20)
			terminate(ppp, "could not get the encryption key + authenticator", 1);
		getasymkey(ppp->sendkey, c->ai->secret, 1, isserver);
		getasymkey(ppp->recvkey, c->ai->secret, 0, isserver);
		break;
	}
	auth_freeAI(c->ai);
	c->ai = nil;
}

static void
getchap(PPP *ppp, Block *b)
{
	Lcpmsg *m;
	int len, vlen, i, id, n, nresp;
	char code;
	Chap *c;
	Chapreply cr;
	MSchapreply mscr;
	char uid[PATH];
	uchar resp[256], *p;

	m = (Lcpmsg*)b->rptr;
	len = nhgets(m->len);
	if(BLEN(b) < len){
		syslog(0, LOG, "short chap message");
		freeb(b);
		return;
	}

	qlock(ppp);
	c = ppp->chap;
	vlen = m->data[0];
	switch(m->code){
	case Cchallenge:
		id = m->id;
		memset(ppp->chapname, 0, sizeof(ppp->chapname));
		nresp = auth_respondAI(m->data+1, vlen,
			ppp->chapname, sizeof(ppp->chapname), 
			resp, sizeof(resp), &c->ai,
			auth_getkey,
			"proto=%s role=client service=ppp %s",
			getaproto(c->proto), keyspec);
		if(nresp < 0){
			char err[ERRMAX];
			snprint(err, sizeof(err), "auth_respond: %r");
			terminate(ppp, err, 1);
		}
		if(c->proto == APmschap || c->proto == APmschapv2)
			while(nresp < 49) resp[nresp++] = 0;
		freeb(b);
		len = 4 + 1 + nresp + strlen(ppp->chapname);
		b = alloclcp(Cresponse, id, len, &m);
		*b->wptr++ = nresp;
		memmove(b->wptr, resp, nresp);
		b->wptr += nresp;
		memmove(b->wptr, ppp->chapname, strlen(ppp->chapname));
		b->wptr += strlen(ppp->chapname);
		hnputs(m->len, len);
		netlog("ppp: sending response len %d\n", len);
		putframe(ppp, Pchap, b);
		break;
	case Cresponse:
		if(m->id != c->id || c->cs == nil) {
			netlog("ppp: chap: bad response id\n");
			break;
		}
		switch(c->proto) {
		default:
			terminate(ppp, "unknown chap protocol", 0);
			sysfatal("unknown chap protocol: %d", c->proto);
		case APmd5:
			if(vlen > len - 5 || vlen != 16) {
				netlog("ppp: chap: bad response len\n");
				break;
			}
			cr.id = m->id;
			memmove(cr.resp, m->data+1, 16);
			memset(uid, 0, sizeof(uid));
			n = len-5-vlen;
			if(n >= PATH)
				n = PATH-1;
			memmove(uid, m->data+1+vlen, n);
			c->cs->user = uid;
			c->cs->resp = &cr;
			c->cs->nresp = sizeof cr;
			break;
		case APmschap:
		case APmschapv2:
			if(vlen > len - 5 || vlen < 48) {
				netlog("ppp: chap: bad response len\n");
				break;
			}
			memset(&mscr, 0, sizeof(mscr));
			memmove(mscr.LMresp, m->data+1, 24);
			memmove(mscr.NTresp, m->data+24+1, 24);
			n = len-5-vlen;
			p = m->data+1+vlen;
			/* remove domain name */
			for(i=0; i<n; i++) {
				if(p[i] == '\\') {
					p += i+1;
					n -= i+1;
					break;
				}
			}
			if(n >= PATH)
				n = PATH-1;
			memset(uid, 0, sizeof(uid));
			memmove(uid, p, n);
			c->cs->user = uid;
			c->cs->resp = &mscr;
			c->cs->nresp = sizeof mscr;
			break;
		} 

		syslog(0, LOG, ": remote=%I vlen %d proto %d response user %s nresp %d",
			ppp->remote, vlen, c->proto, c->cs->user, c->cs->nresp);

		if((c->ai = auth_response(c->cs)) == nil || auth_chuid(c->ai, nil) < 0){
			c->state = Cunauth;
			code = Cfailure;
			syslog(0, LOG, ": remote=%I: auth failed: %r, uid=%s",
				ppp->remote, uid);
		}else{
			c->state = Cauthok;
			code = Csuccess;
			syslog(0, LOG, ": remote=%I: auth ok: uid=%s nsecret=%d",
				ppp->remote, uid, c->ai->nsecret);
		}
		auth_freechal(c->cs);
		c->cs = nil;
		freeb(b);

		/* send reply */
		if(code == Csuccess && c->proto == APmschapv2 && c->ai->nsecret == 16+20){
			b = alloclcp(code, c->id, 4+2+2*20+1, &m);
			b->wptr += sprint((char*)m->data, "S=%.20H", c->ai->secret+16);
		} else {
			b = alloclcp(code, c->id, 4, &m);
		}
		hnputs(m->len, BLEN(b));
		putframe(ppp, Pchap, b);

		if(c->state == Cauthok){
			setppekey(ppp, 1);
			setphase(ppp, Pnet);
		} else {
			/* restart chapp negotiation */
			chapinit(ppp);
		}
		break;
	case Csuccess:
		if(c->proto == APmschapv2 && c->ai != nil && c->ai->nsecret == 16+20){
			n = snprint((char*)resp, sizeof(resp), "S=%.20H", c->ai->secret+16);
			if(len - 4 < n || tsmemcmp(m->data, resp, n) != 0){
				netlog("ppp: chap: bad authenticator\n");
				terminate(ppp, "chap: bad authenticator", 0);
				break;
			}
		}
		netlog("ppp: chap succeeded\n");
		setppekey(ppp, 0);
		setphase(ppp, Pnet);
		break;
	case Cfailure:
		netlog("ppp: chap failed\n");
		terminate(ppp, "chap failed", 0);
		break;
	default:
		syslog(0, LOG, "chap code %d?", m->code);
		break;
	}
	qunlock(ppp);
	freeb(b);
}

static void
putpaprequest(PPP *ppp)
{
	Block *b;
	Lcpmsg *m;
	Chap *c;
	UserPasswd *up;
	int len, nlen, slen;

	up = auth_getuserpasswd(auth_getkey, "proto=pass service=ppp %s", keyspec);
	if(up == nil){
		char err[ERRMAX];
		snprint(err, sizeof(err), "auth_getuserpasswd: %r");
		terminate(ppp, err, 1);
	}
	c = ppp->chap;
	c->id++;
	netlog("ppp: pap: send authreq %d %s %s\n", c->id, up->user, "****");

	nlen = strlen(up->user);
	slen = strlen(up->passwd);
	len = 4 + 1 + nlen + 1 + slen;
	b = alloclcp(Pauthreq, c->id, len, &m);

	*b->wptr++ = nlen;
	memmove(b->wptr, up->user, nlen);
	b->wptr += nlen;
	*b->wptr++ = slen;
	memmove(b->wptr, up->passwd, slen);
	b->wptr += slen;
	hnputs(m->len, len);

	memset(up->user, 0, nlen);	/* no leaks */
	memset(up->passwd, 0, slen);	/* no leaks */
	free(up);

	putframe(ppp, Ppasswd, b);

	memset(b->rptr, 0, BLEN(b));	/* no leaks */
	freeb(b);
}

static void
papinit(PPP *ppp)
{
	ppp->chap->id = 0;
	putpaprequest(ppp);
}

static void
getpap(PPP *ppp, Block *b)
{
	Lcpmsg *m;
	int len;

	m = (Lcpmsg*)b->rptr;
	len = 4;
	if(BLEN(b) < 4 || BLEN(b) < (len = nhgets(m->len))){
		syslog(0, LOG, "short pap message (%zd < %d)", BLEN(b), len);
		freeb(b);
		return;
	}
	if(len < sizeof(Lcpmsg))
		m->data[0] = 0;

	qlock(ppp);
	switch(m->code){
	case Pauthreq:
		netlog("ppp: pap auth request, not supported\n");
		break;
	case Pauthack:
		if(ppp->phase == Pauth
		&& ppp->chap->proto == APpasswd
		&& m->id <= ppp-> chap->id){
			netlog("ppp: pap succeeded\n");
			setphase(ppp, Pnet);
		}
		break;
	case Pauthnak:
		if(ppp->phase == Pauth
		&& ppp->chap->proto == APpasswd
		&& m->id <= ppp-> chap->id){
			netlog("ppp: pap failed (%d:%.*s)\n",
				m->data[0], utfnlen((char*)m->data+1, m->data[0]), (char*)m->data+1);
			terminate(ppp, "pap failed", 0);
		}
		break;
	default:
		netlog("ppp: unknown pap messsage %d\n", m->code);
	}
	qunlock(ppp);
	freeb(b);
}

static void
printopts(Pstate *p, Block *b, int send)
{
	Lcpmsg *m;	
	Lcpopt *o;
	int proto, x, period;
	uchar *cp;
	char *code, *dir;

	m = (Lcpmsg*)b->rptr;
	switch(m->code) {
	default: code = "<unknown>"; break;
	case Lconfreq: code = "confrequest"; break;
	case Lconfack: code = "confack"; break;
	case Lconfnak: code = "confnak"; break;
	case Lconfrej: code = "confreject"; break;
	}

	if(send)
		dir = "send";
	else
		dir = "recv";

	netlog("ppp: %s %s: id=%d\n", dir, code, m->id);

	for(cp = m->data; cp < b->wptr; cp += o->len){
		o = (Lcpopt*)cp;
		if(cp + o->len > b->wptr || o->len < 2){
			netlog("\tbad option length %ux\n", o->type);
			return;
		}

		switch(p->proto){
		case Plcp:
			switch(o->type){
			default:
				netlog("\tunknown %d len=%d\n", o->type, o->len);
				break;
			case Omtu:
				netlog("\tmtu = %d\n", nhgets(o->data));
				break;
			case Octlmap:
				netlog("\tctlmap = %ux\n", nhgetl(o->data));
				break;
			case Oauth:
				netlog("\tauth = %ux", nhgetl(o->data));
				proto = nhgets(o->data);
				switch(proto) {
				default:
					netlog("unknown auth proto %d\n", proto);
					break;
				case Ppasswd:
					netlog("password\n");
					break;
				case Pchap:
					netlog("chap %ux\n", o->data[2]);
					break;
				}
				break;
			case Oquality:
				proto = nhgets(o->data);
				switch(proto) {
				default:
					netlog("\tunknown quality proto %d\n", proto);
					break;
				case Plqm:
					x = nhgetl(o->data+2)*10;
					period = (x+Period-1)/Period;
					netlog("\tlqm period = %d\n", period);
					break;
				}
			case Omagic:
				netlog("\tmagic = %ux\n", nhgetl(o->data));
				break;
			case Opc:
				netlog("\tprotocol compress\n");
				break;
			case Oac:
				netlog("\taddr compress\n");
				break;
			}
			break;
		case Pccp:
			switch(o->type){
			default:
				netlog("\tunknown %d len=%d\n", o->type, o->len);
				break;
			case Ocoui:	
				netlog("\tOUI\n");
				break;
			case Ocstac:
				netlog("\tstac LZS\n");
				break;
			case Ocmppc:	
				netlog("\tMicrosoft PPC len=%d %ux\n", o->len, nhgetl(o->data));
				break;
			case Octhwack:	
				netlog("\tThwack\n");
				break;
			}
			break;
		case Pecp:
			switch(o->type){
			default:
				netlog("\tunknown %d len=%d\n", o->type, o->len);
				break;
			case Oeoui:	
				netlog("\tOUI\n");
				break;
			case Oedese:
				netlog("\tDES\n");
				break;
			}
			break;
		case Pipcp:
			switch(o->type){
			default:
				netlog("\tunknown %d len=%d\n", o->type, o->len);
				break;
			case Oipaddrs:	
				netlog("\tip addrs - deprecated\n");
				break;
			case Oipcompress:
				netlog("\tip compress\n");
				break;
			case Oipaddr:	
				netlog("\tip addr %V\n", o->data);
				break;
			case Oipdns:	
				netlog("\tdns addr %V\n", o->data);
				break;
			case Oipwins:	
				netlog("\twins addr %V\n", o->data);
				break;
			case Oipdns2:	
				netlog("\tdns2 addr %V\n", o->data);
				break;
			case Oipwins2:	
				netlog("\twins2 addr %V\n", o->data);
				break;
			}
			break;
		case Pipv6cp:
			switch(o->type){
			default:
				netlog("\tunknown %d len=%d\n", o->type, o->len);
				break;
			case Oipv6eui:	
				netlog("\tipv6eui %.*H\n", o->len, o->data);
				break;
			}
			break;
		}
	}
}

static void
sendtermreq(PPP *ppp, Pstate *p)
{
	Block *b;
	Lcpmsg *m;

	p->termid = ++(p->id);
	b = alloclcp(Ltermreq, p->termid, 4, &m);
	hnputs(m->len, 4);
	putframe(ppp, p->proto, b);
	freeb(b);
	newstate(ppp, p, Sclosing);
}

static void
sendechoreq(PPP *ppp, Pstate *p)
{
	Block *b;
	Lcpmsg *m;

	p->termid = ++(p->id);
	b = alloclcp(Lechoreq, p->id, 8, &m);
	hnputl(b->wptr, ppp->magic);
	b->wptr += 4;
	hnputs(m->len, 8);
	putframe(ppp, p->proto, b);
	freeb(b);
}

enum
{
	CtrlD	= 0x4,
	CtrlE	= 0x5,
	CtrlO	= 0xf,
	Cr	= 13,
	View	= 0x80,
};

int conndone;

static void
xfer(int fd)
{
	int i, n;
	uchar xbuf[128];

	for(;;) {
		n = read(fd, xbuf, sizeof(xbuf));
		if(n < 0)
			break;
		if(conndone)
			break;
		for(i = 0; i < n; i++)
			if(xbuf[i] == Cr)
				xbuf[i] = ' ';
		write(1, xbuf, n);
	}
	close(fd);
}

static int
readcr(int fd, char *buf, int nbuf)
{
	char c;
	int n, tot;

	tot = 0;
	while((n=read(fd, &c, 1)) == 1){
		if(c == '\n'){
			buf[tot] = 0;
			return tot;
		}
		buf[tot++] = c;
		if(tot == nbuf)
			sysfatal("line too long in readcr");
	}
	return n;
}

static void
connect(int fd, int cfd)
{
	int n, ctl;
	char xbuf[128];

	if (chatfile) {
		int chatfd, lineno, nb;
		char *buf, *p, *s, response[128];
		Dir *dir;

		if ((chatfd = open(chatfile, OREAD)) < 0)
			sysfatal("cannot open %s: %r", chatfile);

		if ((dir = dirfstat(chatfd)) == nil)
			sysfatal("cannot fstat %s: %r",chatfile);

		buf = (char *)malloc(dir->length + 1);
		assert(buf);

		if ((nb = read(chatfd, buf, dir->length)) < 0)
			sysfatal("cannot read chatfile %s: %r", chatfile);
		assert(nb == dir->length);
		buf[dir->length] = '\0';
		free(dir);
		close(chatfd);

		p = buf;
		lineno = 0;
		for(;;) {
			char *_args[3];

			if ((s = strchr(p, '\n')) == nil)
				break;
			*s++ = '\0';
		
			lineno++;

			if (*p == '#') {
				p = s; 
				continue;
			}

			if (tokenize(p, _args, 3) != 2)
				sysfatal("invalid line %d (line expected: 'send' 'expect')", 
						lineno);

			if (debug)
				print("sending %s, expecting %s\n", _args[0], _args[1]);

			if(strlen(_args[0])){
				nb = fprint(fd, "%s\r", _args[0]);
				assert(nb > 0);
			}

			if (strlen(_args[1]) > 0) {
				if ((nb = readcr(fd, response, sizeof response-1)) < 0)
					sysfatal("cannot read response from: %r");

				if (debug)
					print("response %s\n", response);

				if (nb == 0)
					sysfatal("eof on input?");

				if (cistrstr(response, _args[1]) == nil)
					sysfatal("expected %s, got %s", _args[1], response);
			}
			p = s;
		}
		free(buf);
		return;
	}

	print("Connect to file system now, type ctrl-d when done.\n");
	print("...(Use the view or down arrow key to send a break)\n");
	print("...(Use ctrl-e to set even parity or ctrl-o for odd)\n");

	ctl = open("/dev/consctl", OWRITE);
	if(ctl < 0)
		sysfatal("opening consctl");
	fprint(ctl, "rawon");

	fd = dup(fd, -1);
	conndone = 0;
	switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		sysfatal("forking xfer");
	case 0:
		xfer(fd);
		exits(nil);
	}

	for(;;){
		read(0, xbuf, 1);
		switch(xbuf[0]&0xff) {
		case CtrlD:	/* done */
			conndone = 1;
			close(ctl);
			print("\n");
			return;
		case CtrlE:	/* set even parity */
			fprint(cfd, "pe");
			break;
		case CtrlO:	/* set odd parity */
			fprint(cfd, "po");
			break;
		case View:	/* send a break */
			fprint(cfd, "k500");
			break;
		default:
			n = write(fd, xbuf, 1);
			if(n < 0) {
				errstr(xbuf, sizeof(xbuf));
				conndone = 1;
				close(ctl);
				print("[remote write error (%s)]\n", xbuf);
				return;
			}
		}
	}
}

int interactive;

void
usage(void)
{
	fprint(2, "usage: ppp [-CPSacdfuy] [-b baud] [-k keyspec] [-m mtu] "
		"[-M chatfile] [-p dev] [-x netmntpt] [-t modemcmd] [-U duid] "
		"[local-addr [remote-addr]]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	static PPP ppp[1];
	int mtu, framing, user, mediain, mediaout, cfd;
	Ipaddr ipaddr[2], remip[2];
	char *dev, *duid, *modemcmd;
	char net[128];
	int pfd[2];
	char buf[128];

	quotefmtinstall();
	fmtinstall('I', eipfmt);
	fmtinstall('V', eipfmt);
	fmtinstall('E', eipfmt);
	fmtinstall('H', encodefmt);

	invalidate(ipaddr[0]);
	invalidate(ipaddr[1]);
	invalidate(remip[0]);
	invalidate(remip[1]);

	dev = nil;
	mtu = Defmtu;
	framing = 0;
	duid = nil;
	setnetmtpt(net, sizeof(net), nil);
	user = 0;
	modemcmd = nil;

	ARGBEGIN{
	case 'a':
		noauth = 1;
		break;
	case 'b':
		baud = atoi(EARGF(usage()));
		if(baud < 0)
			baud = 0;
		break;
	case 'c':
		nocompress = 1;
		break;
	case 'C':
		noipcompress = 1;
		break;
	case 'd':
		debug++;
		break;
	case 'f':
		framing = 1;
		break;
	case 'F':
		pppframing = 0;
		break;
	case 'k':
		keyspec = EARGF(usage());
		break;
	case 'm':
		mtu = atoi(EARGF(usage()));
		if(mtu < Minmtu)
			mtu = Minmtu;
		if(mtu > Maxmtu)
			mtu = Maxmtu;
		break;
	case 'M':
		chatfile = EARGF(usage());
		break;
	case 'p':
		dev = EARGF(usage());
		break;
	case 'P':	/* this seems reversed compared to ipconfig */
		primary = 1;
		break;
	case 'S':
		server = 1;
		break;
	case 't':
		modemcmd = EARGF(usage());
		break;
	case 'U':
		duid = EARGF(usage());
		break;
	case 'u':
		user = 1;
		break;
	case 'x':
		setnetmtpt(net, sizeof net, EARGF(usage()));
		break;
	case 'y':
		proxy = 1;
		break;
	default:
		fprint(2, "unknown option %c\n", ARGC());
		usage();
	}ARGEND;

	switch(argc){
	case 4:	/* [local [remote [local2 [remote2]]]] */
		if (parseip(remip[1], argv[3]) == -1)
			sysfatal("bad ip %s", argv[3]);
	case 3:	/* [local [remote [local2]]] */
		if (parseip(ipaddr[1], argv[2]) == -1)
			sysfatal("bad ip %s", argv[2]);
	case 2:	/* [local [remote]] */
		if (parseip(remip[0], argv[1]) == -1)
			sysfatal("bad ip %s", argv[1]);
	case 1:	/* [local] */
		if (parseip(ipaddr[0], argv[0]) == -1)
			sysfatal("bad ip %s", argv[0]);

		if (argc == 2 && isv4(ipaddr[0]) != isv4(remip[0])){
			ipmove(ipaddr[1], remip[0]);
			invalidate(remip[0]);
		} else if(argc > 2 && isv4(ipaddr[0]) != isv4(remip[0]) && isv4(remip[0]) != isv4(ipaddr[1])) {
			Ipaddr tmp;

			ipmove(tmp, remip[0]);
			ipmove(remip[0], ipaddr[1]);
			ipmove(ipaddr[1], tmp);
		}
	case 0:
		break;
	default:
		usage();
	}

	if(dev != nil){
		mediain = open(dev, ORDWR);
		if(mediain < 0){
			if(strchr(dev, '!')){
				if((mediain = dial(dev, 0, 0, &cfd)) == -1){
					fprint(2, "%s: couldn't dial %s: %r\n", argv0, dev);
					exits(dev);
				}
			} else {
				fprint(2, "%s: couldn't open %s\n", argv0, dev);
				exits(dev);
			}
		} else {
			snprint(buf, sizeof buf, "%sctl", dev);
			cfd = open(buf, ORDWR);
		}
		if(cfd >= 0){
			if(baud)
				fprint(cfd, "b%d", baud);
			fprint(cfd, "m1");	/* cts/rts flow control (and fifo's) on */
			fprint(cfd, "q64000");	/* increase q size to 64k */
			fprint(cfd, "n1");	/* nonblocking writes on */
			fprint(cfd, "r1");	/* rts on */
			fprint(cfd, "d1");	/* dtr on */
			fprint(cfd, "c1");	/* dcdhup on */
			if(user || chatfile)
				connect(mediain, cfd);
			close(cfd);
		} else {
			if(user || chatfile)
				connect(mediain, -1);
		}
		mediaout = dup(mediain, -1);
	} else {
		mediain = open("/fd/0", OREAD);
		if(mediain < 0){
			fprint(2, "%s: couldn't open /fd/0\n", argv0);
			exits("/fd/0");
		}
		mediaout = open("/fd/1", OWRITE);
		if(mediaout < 0){
			fprint(2, "%s: couldn't open /fd/0\n", argv0);
			exits("/fd/1");
		}
	}
	if(modemcmd != nil && mediaout >= 0)
		fprint(mediaout, "%s\r", modemcmd);

	if(pipe(pfd) < 0){
		fprint(2, "%s: can't create pipe\n", argv0);
		exits("pipe");
	}
	switch(rfork(RFFDG|RFREND|RFPROC|RFNOTEG|RFNOWAIT)){
	case -1:
		fprint(2, "%s: can't fork\n", argv0);
		exits("fork");
	case 0:
		close(pfd[0]);
		pppopen(ppp, mediain, mediaout, pfd[1], net, dev, ipaddr, remip, mtu, framing, duid);
		exits(nil);
	default:
		/* read commands from pipe */
		dup(pfd[0], 0);

		close(pfd[0]);
		close(pfd[1]);
		close(mediain);
		close(mediaout);

		/* stdout to stderr */
		dup(2, 1);
	}

	/* become a shell, reading commands from pipe */
	execl("/bin/rc", "rc", nil);
	exits("exec");
}

void
netlog(char *fmt, ...)
{
	va_list arg;
	char *m;
	static long start;
	long now;

	if(debug == 0)
		return;

	now = time(0);
	if(start == 0)
		start = now;

	va_start(arg, fmt);
	m = vsmprint(fmt, arg);
	fprint(2, "%ld %s", now-start, m);
	free(m);
	va_end(arg);
}

/*
 *  return non-zero if this is a valid v4 address
 */
static int
validv4(Ipaddr addr)
{
	return memcmp(addr, v4prefix, IPv4off) == 0 && memcmp(addr, v4prefix, IPaddrlen) != 0;
}

static void
invalidate(Ipaddr addr)
{
	ipmove(addr, IPnoaddr);
}

static uchar v6llprefix[16] = {
	0xfe, 0x80, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

enum {
	v6llprefixlen = 8,
};

/*
 *  return non-zero when addr is a valid link-local ipv6 address
 */
static int
validv6(Ipaddr addr)
{
	return memcmp(addr, v6llprefix, v6llprefixlen) == 0 && memcmp(addr, v6llprefix, IPaddrlen) != 0;
}

/*
 *  convert EUI-64 or EUI-48 to link-local ipv6 address,
 *  return non-zero when successfull or zero on failure.
 */
static int
euitov6(Ipaddr addr, uchar *data, int len)
{
	if(len < 1 || len > IPaddrlen-v6llprefixlen){
		invalidate(addr);
		return 0;
	}
	ipmove(addr, v6llprefix);
	memmove(&addr[IPaddrlen-len], data, len);
	return memcmp(addr, v6llprefix, IPaddrlen) != 0;
}
