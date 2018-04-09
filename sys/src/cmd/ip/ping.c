/* ping for ip v4 and v6 */
#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <ip.h>
#include <bio.h>
#include <ndb.h>
#include "icmp.h"

enum {
	MAXMSG		= 32,
	SLEEPMS		= 1000,

	SECOND		= 1000000000LL,
	MINUTE		= 60*SECOND,
};

typedef struct Req Req;
struct Req
{
	ushort	seq;	/* sequence number */
	vlong	time;	/* time sent */
	vlong	rtt;
	int	ttl;
	int	replied;
	Req	 *next;
};

typedef struct {
	int	version;
	char	*net;
	int	echocmd;
	int	echoreply;
	unsigned iphdrsz;

	void	(*prreply)(Req *r, void *v);
	void	(*prlost)(ushort seq, void *v);
} Proto;


Req	*first;		/* request list */
Req	*last;		/* ... */
Lock	listlock;

char *argv0;

int addresses;
int debug;
int done;
int flood;
int lostmsgs;
int lostonly;
int quiet;
int rcvdmsgs;
int rint;
ushort firstseq;
vlong sum;
int waittime = 5000;

void lost(Req*, void*);
void reply(Req*, void*);

static void
usage(void)
{
	fprint(2,
	    "usage: %s [-6alq] [-s msgsize] [-i millisecs] [-n #pings] dest\n",
		argv0);
	exits("usage");
}

static void
catch(void *a, char *msg)
{
	USED(a);
	if(strstr(msg, "alarm"))
		noted(NCONT);
	else if(strstr(msg, "die"))
		exits("errors");
	else
		noted(NDFLT);
}

static void
prlost4(ushort seq, void *v)
{
	Ip4hdr *ip4 = v;

	print("lost %ud: %V -> %V\n", seq, ip4->src, ip4->dst);
}

static void
prlost6(ushort seq, void *v)
{
	Ip6hdr *ip6 = v;

	print("lost %ud: %I -> %I\n", seq, ip6->src, ip6->dst);
}

static void
prreply4(Req *r, void *v)
{
	Ip4hdr *ip4 = v;

	print("%ud: %V -> %V rtt %lld µs, avg rtt %lld µs, ttl = %d\n",
		r->seq - firstseq, ip4->src, ip4->dst, r->rtt, sum/rcvdmsgs,
		r->ttl);
}

static void
prreply6(Req *r, void *v)
{
	Ip6hdr *ip6 = v;

	print("%ud: %I -> %I rtt %lld µs, avg rtt %lld µs, ttl = %d\n",
		r->seq - firstseq, ip6->src, ip6->dst, r->rtt, sum/rcvdmsgs,
		r->ttl);
}

static Proto v4pr = {
	4,		"icmp",
	EchoRequest,	EchoReply,
	IPV4HDR_LEN,
	prreply4,	prlost4,
};
static Proto v6pr = {
	6,		"icmpv6",
	EchoRequestV6,	EchoReplyV6,
	IPV6HDR_LEN,
	prreply6,	prlost6,
};

static Proto *proto = &v4pr;


Icmphdr *
geticmp(void *v)
{
	char *p = v;

	return (Icmphdr *)(p + proto->iphdrsz);
}

void
clean(ushort seq, vlong now, void *v)
{
	int ttl;
	Req **l, *r;

	ttl = 0;
	if (v) {
		if (proto->version == 4)
			ttl = ((Ip4hdr *)v)->ttl;
		else
			ttl = ((Ip6hdr *)v)->ttl;
	}
	lock(&listlock);
	last = nil;
	for(l = &first; *l; ){
		r = *l;

		if(v && r->seq == seq){
			r->rtt = now-r->time;
			r->ttl = ttl;
			reply(r, v);
		}

		if(now-r->time > MINUTE){
			*l = r->next;
			r->rtt = now-r->time;
			if(v)
				r->ttl = ttl;
			if(r->replied == 0)
				lost(r, v);
			free(r);
		}else{
			last = r;
			l = &r->next;
		}
	}
	unlock(&listlock);
}

void
sender(int fd, int msglen, int interval, int n)
{
	int i, extra;
	ushort seq;
	char buf[64*1024+512];
	Icmphdr *icmp;
	Req *r;

	srand(time(0));
	firstseq = seq = rand();

	icmp = geticmp(buf);
	memset(buf, 0, proto->iphdrsz + ICMP_HDRSIZE);
	for(i = proto->iphdrsz + ICMP_HDRSIZE; i < msglen; i++)
		buf[i] = i;
	icmp->type = proto->echocmd;
	icmp->code = 0;

	if(rint != 0 && interval <= 0)
		rint = 0;
	extra = 0;
	for(i = 0; i < n; i++){
		if(i != 0){
			if(rint != 0)
				extra = nrand(interval);
			sleep(interval + extra);
		}
		r = malloc(sizeof *r);
		if (r == nil)
			continue;
		hnputs(icmp->seq, seq);
		r->seq = seq;
		r->next = nil;
		r->replied = 0;
		r->time = nsec();	/* avoid early free in reply! */
		lock(&listlock);
		if(first == nil)
			first = r;
		else
			last->next = r;
		last = r;
		unlock(&listlock);
		r->time = nsec();
		if(write(fd, buf, msglen) < msglen){
			fprint(2, "%s: write failed: %r\n", argv0);
			return;
		}
		seq++;
	}
	done = 1;
}

void
rcvr(int fd, int msglen, int interval, int nmsg)
{
	int i, n, munged;
	ushort x;
	vlong now;
	char err[ERRMAX];
	uchar buf[64*1024+512];
	Icmphdr *icmp;
	Req *r;

	sum = 0;
	while(lostmsgs+rcvdmsgs < nmsg){
		alarm((nmsg-lostmsgs-rcvdmsgs)*interval+waittime);
		n = read(fd, buf, sizeof buf);
		alarm(0);
		if(n == 0)
			strcpy(err, "got eof");
		else if(n < 0)
			rerrstr(err, sizeof(err));
		now = nsec();
		if(n <= 0){
			print("%s\n", err);
			clean(0, now+MINUTE, nil);
			if(strstr(err, "interrupted") == nil)
				sleep(waittime);
			continue;
		}
		if(n < msglen){
			print("bad len %d/%d\n", n, msglen);
			continue;
		}
		icmp = geticmp(buf);
		munged = 0;
		for(i = proto->iphdrsz + ICMP_HDRSIZE; i < msglen; i++)
			if(buf[i] != (uchar)i)
				munged++;
		if(munged)
			print("corrupted reply\n");
		x = nhgets(icmp->seq);
		if(icmp->type != proto->echoreply || icmp->code != 0) {
			print("bad type/code/sequence %d/%d/%d (want %d/%d/%d)\n",
				icmp->type, icmp->code, x,
				proto->echoreply, 0, x);
			continue;
		}
		clean(x, now, buf);
	}

	lock(&listlock);
	for(r = first; r; r = r->next)
		if(r->replied == 0)
			lostmsgs++;
	unlock(&listlock);

	if(!quiet && lostmsgs)
		print("%d out of %d messages lost\n", lostmsgs,
			lostmsgs+rcvdmsgs);
}

enum
{
	Maxstring	= 128,
	Maxpath		= 256,
};

void
main(int argc, char **argv)
{
	int fd, msglen, interval, nmsg;

	nsec();		/* make sure time file is already open */

	fmtinstall('V', eipfmt);
	fmtinstall('I', eipfmt);

	msglen = interval = 0;
	nmsg = MAXMSG;
	ARGBEGIN {
	case '6':
		proto = &v6pr;
		break;
	case 'a':
		addresses = 1;
		break;
	case 'd':
		debug++;
		break;
	case 'f':
		flood = 1;
		break;
	case 'i':
		interval = atoi(EARGF(usage()));
		if(interval < 0)
			usage();
		break;
	case 'l':
		lostonly++;
		break;
	case 'n':
		nmsg = atoi(EARGF(usage()));
		if(nmsg < 0)
			usage();
		break;
	case 'q':
		quiet = 1;
		break;
	case 'r':
		rint = 1;
		break;
	case 's':
		msglen = atoi(EARGF(usage()));
		break;
	case 'w':
		waittime = atoi(EARGF(usage()));
		if(waittime < 0)
			usage();
		break;
	default:
		usage();
		break;
	} ARGEND;

	if(msglen < proto->iphdrsz + ICMP_HDRSIZE)
		msglen = proto->iphdrsz + ICMP_HDRSIZE;
	if(msglen < 64)
		msglen = 64;
	if(msglen >= 64*1024)
		msglen = 64*1024-1;
	if(interval <= 0 && !flood)
		interval = SLEEPMS;

	if(argc < 1)
		usage();

	notify(catch);

	if(strstr(argv[0], "icmpv6!") != nil)
		proto = &v6pr;
again:
	fd = dial(netmkaddr(argv[0], proto->net, "1"), nil, nil, nil);
	if(fd < 0){
		if(proto == &v4pr){
			proto = &v6pr;
			goto again;
		}
		fprint(2, "%s: couldn't dial: %r\n", argv0);
		exits("dialing");
	}
	if (!quiet){
		NetConnInfo *nci = getnetconninfo(nil, fd);
		print("sending %d %d byte messages %d ms apart to %s\n",
			nmsg, msglen, interval, nci != nil? nci->raddr: argv[0]);
	}

	switch(rfork(RFPROC|RFMEM|RFFDG)){
	case -1:
		fprint(2, "%s: can't fork: %r\n", argv0);
		exits("forking");
	case 0:
		rcvr(fd, msglen, interval, nmsg);
		exits(0);
	default:
		sender(fd, msglen, interval, nmsg);
		wait();
		exits(lostmsgs ? "lost messages" : "");
	}
}

void
reply(Req *r, void *v)
{
	r->rtt /= 1000LL;
	sum += r->rtt;
	if(!r->replied)
		rcvdmsgs++;
	if(!quiet && !lostonly)
		if(addresses)
			(*proto->prreply)(r, v);
		else
			print("%ud: rtt %lld µs, avg rtt %lld µs, ttl = %d\n",
				r->seq - firstseq, r->rtt, sum/rcvdmsgs, r->ttl);
	r->replied = 1;
}

void
lost(Req *r, void *v)
{
	if(!quiet)
		if(addresses && v != nil)
			(*proto->prlost)(r->seq - firstseq, v);
		else
			print("lost %ud\n", r->seq - firstseq);
	lostmsgs++;
}
