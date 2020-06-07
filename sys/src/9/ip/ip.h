typedef struct	Conv	Conv;
typedef struct	Fragment4 Fragment4;
typedef struct	Fragment6 Fragment6;
typedef struct	Fs	Fs;
typedef union	Hwaddr	Hwaddr;
typedef struct	IP	IP;
typedef struct	IPaux	IPaux;
typedef struct	Ip4hdr	Ip4hdr;
typedef struct	Ipfrag	Ipfrag;
typedef struct	Ipself	Ipself;
typedef struct	Ipselftab	Ipselftab;
typedef struct	Iplink	Iplink;
typedef struct	Iplifc	Iplifc;
typedef struct	Ipmulti	Ipmulti;
typedef struct	Ipifc	Ipifc;
typedef struct	Iphash	Iphash;
typedef struct	Ipht	Ipht;
typedef struct	Netlog	Netlog;
typedef struct	Medium	Medium;
typedef struct	Proto	Proto;
typedef struct	Arpent	Arpent;
typedef struct	Arp Arp;
typedef struct	Route	Route;
typedef struct	Routehint Routehint;

typedef struct	Routerparams	Routerparams;
typedef struct 	Hostparams	Hostparams;
typedef struct	v6params	v6params;

#pragma incomplete Arp
#pragma incomplete Ipself
#pragma incomplete Ipselftab
#pragma incomplete IP
#pragma incomplete Netlog

enum
{
	Addrlen=	64,
	Maxproto=	20,
	Maxincall=	10,
	Nchans=		1024,
	MAClen=		8,		/* longest mac address */

	MAXTTL=		255,
	DFLTTOS=	0,

	IPaddrlen=	16,
	IPv4addrlen=	4,
	IPv4off=	12,
	IPllen=		4,

	/* ip versions */
	V4=		4,
	V6=		6,
	IP_VER4= 	0x40,
	IP_VER6=	0x60,
	IP_HLEN4=	5,		/* v4: Header length in words */
	IP_DF=		0x4000,		/* v4: Don't fragment */
	IP_MF=		0x2000,		/* v4: More fragments */
	IP_FO=		0x1fff,		/* v4: Fragment offset */
	IP4HDR=		IP_HLEN4<<2,	/* sizeof(Ip4hdr) */
	IP_MAX=		64*1024,	/* Max. Internet packet size, v4 & v6 */

	/* 2^Lroot trees in the root table */
	Lroot=		10,

	Maxpath =	64,
};

enum
{
	Idle=		0,
	Announcing=	1,
	Announced=	2,
	Connecting=	3,
	Connected=	4,
};

/* MIB II counters */
enum
{
	Forwarding,
	DefaultTTL,
	InReceives,
	InHdrErrors,
	InAddrErrors,
	ForwDatagrams,
	InUnknownProtos,
	InDiscards,
	InDelivers,
	OutRequests,
	OutDiscards,
	OutNoRoutes,
	ReasmTimeout,
	ReasmReqds,
	ReasmOKs,
	ReasmFails,
	FragOKs,
	FragFails,
	FragCreates,

	Nipstats,
};

struct Fragment4
{
	Block*	blist;
	Fragment4*	next;
	ulong 	src;
	ulong 	dst;
	ushort	id;
	ulong 	age;
};

struct Fragment6
{
	Block*	blist;
	Fragment6*	next;
	uchar 	src[IPaddrlen];
	uchar 	dst[IPaddrlen];
	uint	id;
	ulong 	age;
};

struct Ipfrag
{
	ushort	foff;
	ushort	flen;
	uchar	payload[];
};

#define IPFRAGSZ offsetof(Ipfrag, payload[0])

/* an instance of IP */
struct IP
{
	uvlong		stats[Nipstats];

	QLock		fraglock4;
	Fragment4*	flisthead4;
	Fragment4*	fragfree4;
	Ref		id4;

	QLock		fraglock6;
	Fragment6*	flisthead6;
	Fragment6*	fragfree6;
	Ref		id6;

	int		iprouting;	/* true if we route like a gateway */
};

/* on the wire packet header */
struct Ip4hdr
{
	uchar	vihl;		/* Version and header length */
	uchar	tos;		/* Type of service */
	uchar	length[2];	/* packet length */
	uchar	id[2];		/* ip->identification */
	uchar	frag[2];	/* Fragment information */
	uchar	ttl;      	/* Time to live */
	uchar	proto;		/* Protocol */
	uchar	cksum[2];	/* Header checksum */
	uchar	src[4];		/* IP source */
	uchar	dst[4];		/* IP destination */
};

struct Routehint
{
	Route	*r;			/* last route used */
	ulong	rgen;			/* routetable generation for *r */
};

/*
 *  one per conversation directory
 */
struct Conv
{
	QLock;

	int	x;			/* conversation index */
	Proto*	p;

	int	restricted;		/* remote port is restricted */
	int	ignoreadvice;		/* don't terminate connection on icmp errors */
	uint	ttl;			/* max time to live */
	uint	tos;			/* type of service */

	uchar	ipversion;
	uchar	laddr[IPaddrlen];	/* local IP address */
	uchar	raddr[IPaddrlen];	/* remote IP address */
	ushort	lport;			/* local port number */
	ushort	rport;			/* remote port number */

	char	*owner;			/* protections */
	int	perm;
	int	inuse;			/* opens of listen/data/ctl */
	int	length;
	int	state;

	/* udp specific */
	int	headers;		/* data src/dst headers in udp */
	int	reliable;		/* true if reliable udp */

	Conv*	incall;			/* calls waiting to be listened for */
	Conv*	next;

	Queue*	rq;			/* queued data waiting to be read */
	Queue*	wq;			/* queued data waiting to be written */
	Queue*	eq;			/* returned error packets */
	Queue*	sq;			/* snooping queue */
	Ref	snoopers;		/* number of processes with snoop open */

	Rendez	cr;
	char	cerr[ERRMAX];

	QLock	listenq;
	Rendez	listenr;

	Ipmulti	*multi;			/* multicast bindings for this interface */

	void*	ptcl;			/* protocol specific stuff */

	Routehint;
};

struct Medium
{
	char	*name;
	int	hsize;		/* medium header size */
	int	mintu;		/* default min mtu */
	int	maxtu;		/* default max mtu */
	int	maclen;		/* mac address length  */
	void	(*bind)(Ipifc*, int, char**);
	void	(*unbind)(Ipifc*);
	void	(*bwrite)(Ipifc *ifc, Block *b, int version, uchar *ip);

	/* for arming interfaces to receive multicast */
	void	(*addmulti)(Ipifc *ifc, uchar *a, uchar *ia);
	void	(*remmulti)(Ipifc *ifc, uchar *a, uchar *ia);

	/* process packets written to 'data' */
	void	(*pktin)(Fs *f, Ipifc *ifc, Block *bp);

	/* address resolution */
	void	(*areg)(Fs *f, Ipifc *ifc, Iplifc *lifc, uchar *ip);

	/* v6 address generation */
	void	(*pref2addr)(uchar *pref, uchar *ea);

	int	unbindonclose;	/* if non-zero, unbind on last close */
};

/* logical interface associated with a physical one */
struct Iplifc
{
	uchar	local[IPaddrlen];
	uchar	mask[IPaddrlen];
	uchar	remote[IPaddrlen];
	uchar	net[IPaddrlen];
	uchar	type;		/* route type */
	uchar	tentative;	/* =1 => v6 dup disc on, =0 => confirmed unique */
	uchar	onlink;		/* =1 => onlink, =0 offlink. */
	uchar	autoflag;	/* v6 autonomous flag */
	ulong 	validlt;	/* v6 valid lifetime */
	ulong 	preflt;		/* v6 preferred lifetime */
	ulong	origint;	/* time when addr was added */
	Iplink	*link;		/* addresses linked to this lifc */
	Iplifc	*next;
};

/* binding twixt Ipself and Iplifc */
struct Iplink
{
	Ipself	*self;
	Iplifc	*lifc;
	Iplink	*selflink;	/* next link for this local address */
	Iplink	*lifclink;	/* next link for this lifc */
	Iplink	*next;		/* free list */
	ulong	expire;
	int	ref;
};

/* rfc 2461, pp.40—43. */

/* default values, one per stack */
struct Routerparams {
	int	mflag;		/* flag: managed address configuration */
	int	oflag;		/* flag: other stateful configuration */
	int 	maxraint;	/* max. router adv interval (ms) */
	int	minraint;	/* min. router adv interval (ms) */
	int	linkmtu;	/* mtu options */
	int	reachtime;	/* reachable time */
	int	rxmitra;	/* retransmit interval */
	int	ttl;		/* cur hop count limit */
	int	routerlt;	/* router lifetime */
};

struct Hostparams {
	int	rxmithost;
};

struct Ipifc
{
	RWlock;

	Conv	*conv;		/* link to its conversation structure */
	char	dev[64];	/* device we're attached to */
	Medium	*m;		/* Media pointer */
	int	maxtu;		/* Maximum transfer unit */
	int	mintu;		/* Minumum tranfer unit */
	void	*arg;		/* medium specific */

	uchar	reflect;	/* allow forwarded packets to go out the same interface */
	uchar	reassemble;	/* reassemble IP packets before forwarding to this interface */
	
	uchar	ifcid;		/* incremented each 'bind/unbind/add/remove' */

	uchar	mac[MAClen];	/* MAC address */

	Iplifc	*lifc;		/* logical interfaces on this physical one */

	ulong	in, out;	/* message statistics */
	ulong	inerr, outerr;	/* ... */

	uchar	sendra6;	/* flag: send router advs on this ifc */
	uchar	recvra6;	/* flag: recv router advs on this ifc */
	Routerparams rp;	/* router parameters as in RFC 2461, pp.40—43.
					used only if node is router */

	int	speed;		/* link speed in bits per second */
	int	delay;		/* burst delay in ms */
	int	burst;		/* burst delay in bytes */
	int	load;		/* bytes in flight */
	ulong	ticks;
};

/*
 *  one per multicast-lifc pair used by a Conv
 */
struct Ipmulti
{
	uchar	ma[IPaddrlen];
	uchar	ia[IPaddrlen];
	Ipmulti	*next;
};

/*
 *  hash table for 2 ip addresses + 2 ports
 */
enum
{
	Nipht=		521,	/* convenient prime */

	IPmatchexact=	0,	/* match on 4 tuple */
	IPmatchany,		/* *!* */
	IPmatchport,		/* *!port */
	IPmatchaddr,		/* addr!* */
	IPmatchpa,		/* addr!port */
};
struct Iphash
{
	Iphash	*next;
	Conv	*c;
	int	match;
};
struct Ipht
{
	Lock;
	Iphash	*tab[Nipht];
};
void iphtadd(Ipht*, Conv*);
void iphtrem(Ipht*, Conv*);
Conv* iphtlook(Ipht *ht, uchar *sa, ushort sp, uchar *da, ushort dp);

/*
 *  one per multiplexed protocol
 */
struct Proto
{
	QLock;
	char*		name;		/* protocol name */
	int		x;		/* protocol index */
	int		ipproto;	/* ip protocol type */

	char*		(*connect)(Conv*, char**, int);
	char*		(*announce)(Conv*, char**, int);
	char*		(*bind)(Conv*, char**, int);
	int		(*state)(Conv*, char*, int);
	void		(*create)(Conv*);
	void		(*close)(Conv*);
	void		(*rcv)(Proto*, Ipifc*, Block*);
	char*		(*ctl)(Conv*, char**, int);
	void		(*advise)(Proto*, Block*, char*);
	int		(*stats)(Proto*, char*, int);
	int		(*local)(Conv*, char*, int);
	int		(*remote)(Conv*, char*, int);
	int		(*inuse)(Conv*);
	int		(*gc)(Proto*);	/* returns true if any conversations are freed */

	Fs		*f;		/* file system this proto is part of */
	Conv		**conv;		/* array of conversations */
	int		ptclsize;	/* size of per protocol ctl block */
	int		nc;		/* number of conversations */
	int		ac;
	Qid		qid;		/* qid for protocol directory */
	ushort		nextrport;

	void		*priv;
};


/*
 *  one per IP protocol stack
 */
struct Fs
{
	RWlock;
	int	dev;

	int	np;
	Proto*	p[Maxproto+1];		/* list of supported protocols */
	Proto*	t2p[256];		/* vector of all protocols */
	Proto*	ipifc;			/* kludge for ipifcremroute & ipifcaddroute */
	Proto*	ipmux;			/* kludge for finding an ip multiplexor */

	IP	*ip;
	Ipselftab	*self;
	Arp	*arp;
	v6params	*v6p;

	Route	*v4root[1<<Lroot];	/* v4 routing forest */
	Route	*v6root[1<<Lroot];	/* v6 routing forest */
	Route	*queue;			/* used as temp when reinjecting routes */

	Netlog	*alog;

	char	ndb[1024];		/* an ndb entry for this interface */
	int	ndbvers;
	long	ndbmtime;
};

struct v6params
{
	Routerparams	rp;		/* v6 params, one copy per node now */
	Hostparams	hp;
};


int	Fsconnected(Conv*, char*);
Conv*	Fsnewcall(Conv*, uchar*, ushort, uchar*, ushort, uchar);
int	Fspcolstats(char*, int);
int	Fsproto(Fs*, Proto*);
int	Fsbuiltinproto(Fs*, uchar);
Conv*	Fsprotoclone(Proto*, char*);
Proto*	Fsrcvpcol(Fs*, uchar);
Proto*	Fsrcvpcolx(Fs*, uchar);
char*	Fsstdconnect(Conv*, char**, int);
char*	Fsstdannounce(Conv*, char**, int);
char*	Fsstdbind(Conv*, char**, int);
ulong	scalednconv(void);
void	closeconv(Conv*);
/*
 *  logging
 */
enum
{
	Logip=		1<<1,
	Logtcp=		1<<2,
	Logfs=		1<<3,
	Logil=		1<<4,
	Logicmp=	1<<5,
	Logudp=		1<<6,
	Logcompress=	1<<7,
	Logilmsg=	1<<8,
	Loggre=		1<<9,
	Logppp=		1<<10,
	Logtcprxmt=	1<<11,
	Logigmp=	1<<12,
	Logudpmsg=	1<<13,
	Logipmsg=	1<<14,
	Logrudp=	1<<15,
	Logrudpmsg=	1<<16,
	Logesp=		1<<17,
	Logtcpwin=	1<<18,
};

void	netloginit(Fs*);
void	netlogopen(Fs*);
void	netlogclose(Fs*);
void	netlogctl(Fs*, char*, int);
long	netlogread(Fs*, void*, ulong, long);
void	netlog(Fs*, int, char*, ...);
void	ifcloginit(Fs*);
long	ifclogread(Fs*, Chan *,void*, ulong, long);
void	ifclog(Fs*, uchar *, int);
void	ifclogopen(Fs*, Chan*);
void	ifclogclose(Fs*, Chan*);

#pragma varargck argpos netlog	3

/*
 *  iproute.c
 */
typedef	struct RouteTree RouteTree;
typedef struct V4route V4route;
typedef struct V6route V6route;

enum
{
	/* type bits */
	Rv4=		(1<<0),		/* this is a version 4 route */
	Rifc=		(1<<1),		/* this route is a directly connected interface */
	Rptpt=		(1<<2),		/* this route is a pt to pt interface */
	Runi=		(1<<3),		/* a unicast self address */
	Rbcast=		(1<<4),		/* a broadcast self address */
	Rmulti=		(1<<5),		/* a multicast self address */
	Rproxy=		(1<<6),		/* this route should be proxied */
	Rsrc=		(1<<7),		/* source specific route */
};

struct	RouteTree
{
	Route	*mid;
	Route	*left;
	Route	*right;
	Ipifc	*ifc;
	uchar	ifcid;		/* must match ifc->id */
	uchar	depth;
	uchar	type;
	char	tag[4];
	int	ref;
};

struct V4route
{
	ulong	address;
	ulong	endaddress;

	ulong	source;
	ulong	endsource;

	uchar	gate[IPv4addrlen];
};

struct V6route
{
	ulong	address[IPllen];
	ulong	endaddress[IPllen];

	ulong	source[IPllen];
	ulong	endsource[IPllen];

	uchar	gate[IPaddrlen];
};

struct Route
{
	RouteTree;

	union {
		V6route	v6;
		V4route v4;
	};
};

extern void	addroute(Fs *f, uchar *a, uchar *mask, uchar *s, uchar *smask, uchar *gate, int type, Ipifc *ifc, char *tag);
extern void	remroute(Fs *f, uchar *a, uchar *mask, uchar *s, uchar *smask, uchar *gate, int type, Ipifc *ifc, char *tag);
extern Route*	v4lookup(Fs *f, uchar *a, uchar *s, Routehint *h);
extern Route*	v6lookup(Fs *f, uchar *a, uchar *s, Routehint *h);
extern Route*	v4source(Fs *f, uchar *a, uchar *s);
extern Route*	v6source(Fs *f, uchar *a, uchar *s);
extern long	routeread(Fs *f, char*, ulong, int);
extern long	routewrite(Fs *f, Chan*, char*, int);
extern void	routetype(int type, char p[8]);

/*
 *  devip.c
 */

/*
 *  Hanging off every ip channel's ->aux is the following structure.
 *  It maintains the state used by devip and iproute.
 */
struct IPaux
{
	char	*owner;		/* the user that did the attach */
	char	tag[4];
};

extern IPaux*	newipaux(char*, char*);

/*
 *  arp.c
 */
struct Arpent
{
	uchar	ip[IPaddrlen];
	uchar	mac[MAClen];
	Arpent	*hash;
	Arpent	*nextrxt;		/* re-transmit chain */
	Block	*hold;
	Block	*last;
	Ipifc	*ifc;
	uchar	ifcid;			/* must match ifc->id */
	uchar	state;
	uchar	rxtsrem;		/* re-tranmissions remaining */
	ulong	ctime;			/* time entry was created or refreshed */
	ulong	utime;			/* time entry was last used */
};

extern void	arpinit(Fs*);
extern int	arpread(Arp*, char*, ulong, int);
extern int	arpwrite(Fs*, char*, int);
extern Arpent*	arpget(Arp*, Block *bp, int version, Ipifc *ifc, uchar *ip, uchar *h);
extern void	arprelease(Arp*, Arpent *a);
extern Block*	arpresolve(Arp*, Arpent *a, Medium *type, uchar *mac);
extern int	arpenter(Fs*, int version, uchar *ip, uchar *mac, int n, uchar *ia, Ipifc *ifc, int refresh);
extern void	ndpsendsol(Fs*, Ipifc*, Arpent*);

/*
 * ipaux.c
 */

extern int	parseether(uchar*, char*);
extern vlong	parseip(uchar*, char*);
extern vlong	parseipmask(uchar*, char*, int);
extern vlong	parseipandmask(uchar*, uchar*, char*, char*);
extern char*	v4parseip(uchar*, char*);
extern void	maskip(uchar *from, uchar *mask, uchar *to);
extern int	parsemac(uchar *to, char *from, int len);
extern uchar*	defmask(uchar*);
extern int	isv4(uchar*);
extern void	v4tov6(uchar *v6, uchar *v4);
extern int	v6tov4(uchar *v4, uchar *v6);
extern int	eipfmt(Fmt*);
extern int	convipvers(Conv *c);

#define	ipmove(x, y) memmove(x, y, IPaddrlen)
#define	ipcmp(x, y) ( (x)[IPaddrlen-1] != (y)[IPaddrlen-1] || memcmp(x, y, IPaddrlen) )

extern uchar IPv4bcast[IPaddrlen];
extern uchar IPv4bcastobs[IPaddrlen];
extern uchar IPv4allsys[IPaddrlen];
extern uchar IPv4allrouter[IPaddrlen];
extern uchar IPnoaddr[IPaddrlen];
extern uchar v4prefix[IPaddrlen];
extern uchar IPallbits[IPaddrlen];

#define	NOW	TK2MS(MACHP(0)->ticks)

/*
 *  media
 */
extern Medium	ethermedium;
extern Medium	nullmedium;
extern Medium	pktmedium;

/*
 *  ipifc.c
 */
extern Medium*	ipfindmedium(char *name);
extern void	addipmedium(Medium *med);
extern void	ipifcoput(Ipifc *ifc, Block *bp, int version, uchar *ip);
extern int	ipforme(Fs*, uchar *addr);
extern int	ipismulticast(uchar *ip);
extern Ipifc*	findipifc(Fs*, uchar *local, uchar *remote, int type);
extern Ipifc*	findipifcstr(Fs *f, char *s);
extern void	findlocalip(Fs*, uchar *local, uchar *remote);
extern int	ipv4local(Ipifc *ifc, uchar *local, int prefixlen, uchar *remote);
extern int	ipv6local(Ipifc *ifc, uchar *local, int prefixlen, uchar *remote);
extern Iplifc*	iplocalonifc(Ipifc *ifc, uchar *ip);
extern Iplifc*	ipremoteonifc(Ipifc *ifc, uchar *ip);
extern int	ipproxyifc(Fs *f, Ipifc *ifc, uchar *ip);
extern void	ipifcremmulti(Conv *c, uchar *ma, uchar *ia);
extern void	ipifcaddmulti(Conv *c, uchar *ma, uchar *ia);
extern char*	ipifcrem(Ipifc *ifc, char **argv, int argc);
extern char*	ipifcadd(Ipifc *ifc, char **argv, int argc, int tentative, Iplifc *lifcp);
extern long	ipselftabread(Fs*, char *a, ulong offset, int n);
extern char*	ipifcadd6(Ipifc *ifc, char**argv, int argc);
extern char*	ipifcremove6(Ipifc *ifc, char**argv, int argc);
/*
 *  ip.c
 */
extern void	iprouting(Fs*, int);
extern void	icmpnoconv(Fs*, Block*);
extern void	icmpcantfrag(Fs*, Block*, int);
extern void	icmpttlexceeded(Fs*, Ipifc*, Block*);
extern ushort	ipcsum(uchar*);
extern void	ipiput4(Fs*, Ipifc*, Block*);
extern void	ipiput6(Fs*, Ipifc*, Block*);
extern int	ipoput4(Fs*, Block*, int, int, int, Routehint*);
extern int	ipoput6(Fs*, Block*, int, int, int, Routehint*);
extern int	ipstats(Fs*, char*, int);
extern ushort	ptclbsum(uchar*, int);
extern ushort	ptclcsum(Block*, int, int);
extern void	ip_init(Fs*);
extern void	ip_init_6(Fs*);

/*
 * bootp.c
 */
extern int	bootpread(char*, ulong, int);

/*
 *  resolving inferno/plan9 differences
 */
char*		commonuser(void);
char*		commonerror(void);

/*
 * chandial.c
 */
extern Chan*	chandial(char*, char*, char*, Chan**);

/*
 *  global to all of the stack
 */
extern void	(*igmpreportfn)(Ipifc*, uchar*);
