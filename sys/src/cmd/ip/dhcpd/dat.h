#include "../dhcp.h"

enum
{
	Maxstr=	256,
};

typedef struct Binding Binding;
struct Binding
{
	Binding *next;
	uchar	ip[IPaddrlen];

	char	*boundto;	/* id last bound to */
	char	*offeredto;	/* id we've offered this to */

	long	lease;		/* absolute time at which binding expires */
	long	expoffer;	/* absolute time at which offer times out */
	long	offer;		/* lease offered */
	long	lasttouched;	/* time this entry last assigned/unassigned */
	long	lastcomplained;	/* last time we complained about a used but not leased */
	long	tried;		/* last time we tried this entry */

	Qid	q;		/* qid at the last syncbinding */
};

typedef struct Info	Info;
struct Info
{
	int	indb;			/* true when found in ndb */
	Ipifc	*ifc;			/* ifc when directly connected */

	uchar	ipaddr[NDB_IPlen];	/* ip address of system */
	uchar	ipmask[NDB_IPlen];	/* ip network mask */
	uchar	ipnet[NDB_IPlen];	/* ip network address (ipaddr & ipmask) */

	char	domain[Maxstr];		/* system domain name */
	char	bootf[Maxstr];		/* boot file */
	char	bootf2[Maxstr];		/* alternative boot file */
	uchar	tftp[NDB_IPlen];	/* ip addr of tftp server */
	uchar	tftp2[NDB_IPlen];	/* ip addr of alternate server */
	uchar	gwip[NDB_IPlen];	/* gateway ip address */
	uchar	fsip[NDB_IPlen];	/* file system ip address */
	uchar	auip[NDB_IPlen];	/* authentication server ip address */
	uchar   rootserverip[NDB_IPlen];  /* ip addr of root nfs server  */
	char	rootpath[Maxstr];	/* rootfs for diskless nfs clients */
	char	vendor[Maxstr];		/* vendor info */
};


/* from dhcp.c */
extern int	validip(uchar*);
extern void	fatal(char*, ...);
extern void	warning(char*, ...);
#pragma	varargck argpos	fatal 1
#pragma	varargck argpos	warning 1
extern int	minlease;

/* from db.c */
extern char*	toid(uchar*, int);
extern void	initbinding(uchar*, int);
extern Binding*	iptobinding(uchar*, int);
extern Binding*	idtobinding(char*, Info*, int);
extern Binding*	idtooffer(char*, Info*);
extern int	commitbinding(Binding*);
extern int	releasebinding(Binding*, char*);
extern int	samenet(uchar *ip, Info *iip);
extern void	mkoffer(Binding*, char*, long);
extern int	syncbinding(Binding*, int);

/* from ndb.c */
extern int	lookup(Bootp*, Info*, Info*);
extern int	lookupip(uchar*, char*, char*, Info*, int);
extern void	lookupname(char*, int, Ndbtuple*);
extern Ipifc*	findifc(uchar*);
extern Iplifc*	localonifc(uchar*, Ipifc*);
extern void	localip(uchar*, uchar*, Ipifc*);
extern int	lookupserver(char*, uchar**, int, Ndbtuple *t);
extern Ndbtuple* lookupinfo(uchar *ipaddr, char **attr, int n);

/* from icmp.c */
extern int	icmpecho(uchar*);

extern char	*binddir;
extern int	debug;
extern char	*blog;
extern Ipifc	*ipifcs;
extern long	now;
extern char	*ndbfile;
extern uchar	zeros[];
