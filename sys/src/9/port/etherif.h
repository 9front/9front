enum {
	MaxEther	= 64,
	Ntypes		= 8,
};

typedef struct Macent Macent;
struct Macent
{
	uchar	ea[Eaddrlen];
	ushort	port;
};

typedef struct Ether Ether;
struct Ether {
	ISAConf;			/* hardware info */
	int	tbdf;			/* type+busno+devno+funcno */

	int	ctlrno;
	int	minmtu;
	int 	maxmtu;

	void	(*attach)(Ether*);	/* filled in by reset routine */
	void	(*transmit)(Ether*);
	long	(*ifstat)(Ether*, void*, long, ulong);
	long 	(*ctl)(Ether*, void*, long); /* custom ctl messages */
	void	(*power)(Ether*, int);	/* power on/off */
	void	(*shutdown)(Ether*);	/* shutdown hardware before reboot */
	void	*ctlr;

	Queue*	oq;

	Netif;

	uchar	ea[Eaddrlen];
	Macent	mactab[127];		/* for bridge */
};

extern void etheriq(Ether*, Block*);
extern void addethercard(char*, int(*)(Ether*));
extern ulong ethercrc(uchar*, int);
extern int parseether(uchar*, char*);

#define NEXT(x, l)	(((x)+1)%(l))
#define PREV(x, l)	(((x) == 0) ? (l)-1: (x)-1)
#define	HOWMANY(x, y)	(((x)+((y)-1))/(y))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))
