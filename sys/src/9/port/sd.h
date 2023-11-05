/*
 * Storage Device.
 */
typedef struct SDev SDev;
typedef struct SDfile SDfile;
typedef struct SDifc SDifc;
typedef struct SDio SDio;
typedef struct SDiocmd SDiocmd;
typedef struct SDpart SDpart;
typedef struct SDperm SDperm;
typedef struct SDreq SDreq;
typedef struct SDunit SDunit;

struct SDperm {
	char*	name;
	char*	user;
	ulong	perm;
};

struct SDpart {
	uvlong	start;
	uvlong	end;
	SDperm;
	int	valid;
	ulong	vers;
};

typedef long SDrw(SDunit*, Chan*, void*, long, vlong);
struct SDfile {
	SDperm;
	SDrw	*r;
	SDrw	*w;
};

struct SDunit {
	SDev*	dev;
	int	subno;
	uchar	inquiry[255];		/* format follows SCSI spec */
	uchar	sense[18];		/* format follows SCSI spec */
	uchar	rsense[18];		/* support seperate rq sense and inline return */
	uchar	haversense;
	SDperm;

	QLock	ctl;
	uvlong	sectors;
	ulong	secsize;
	SDpart*	part;			/* nil or array of size npart */
	int	npart;
	ulong	vers;
	SDperm	ctlperm;

	QLock	raw;			/* raw read or write in progress */
	ulong	rawinuse;		/* really just a test-and-set */
	int	state;
	SDreq*	req;
	SDperm	rawperm;
	SDfile	efile[5];
	int	nefile;
};

/*
 * Each controller is represented by a SDev.
 */
struct SDev {
	Ref	r;			/* Number of callers using device */
	SDifc*	ifc;			/* pnp/legacy */
	void*	ctlr;
	int	idno;
	char	name[8];
	SDev*	next;

	QLock;				/* enable/disable */
	int	enabled;
	int	nunit;			/* Number of units */
	QLock	unitlock;		/* `Loading' of units */
	char*	unitflg;		/* Unit flags */
	SDunit**unit;
};

struct SDifc {
	char*	name;

	SDev*	(*pnp)(void);
	int	(*enable)(SDev*);
	int	(*disable)(SDev*);

	int	(*verify)(SDunit*);
	int	(*online)(SDunit*);
	int	(*rio)(SDreq*);
	char*	(*rctl)(SDunit*, char*, char*);
	int	(*wctl)(SDunit*, Cmdbuf*);

	long	(*bio)(SDunit*, int, int, void*, long, uvlong);
	SDev*	(*probe)(DevConf*);
	void	(*clear)(SDev*);
	char*	(*rtopctl)(SDev*, char*, char*);
	int	(*wtopctl)(SDev*, Cmdbuf*);
	int	(*ataio)(SDreq*);
};

struct SDreq {
	SDunit*	unit;
	int	lun;
	char	write;
	char	proto;
	char	ataproto;
	uchar	cmd[0x20];
	int	clen;
	void*	data;
	int	dlen;

	int	flags;

	int	status;
	long	rlen;
	uchar	sense[32];
};

enum {
	SDnosense	= 0x00000001,
	SDvalidsense	= 0x00010000,
};

enum {
	SDretry		= -5,		/* internal to controllers */
	SDmalloc	= -4,
	SDeio		= -3,
	SDtimeout	= -2,
	SDnostatus	= -1,

	SDok		= 0,

	SDcheck		= 0x02,		/* check condition */
	SDbusy		= 0x08,		/* busy */

	SDmaxio		= 2048*1024,
	SDnpart		= 16,

	SDread	= 0,
	SDwrite,

	SData		= 1,
	SDcdb		= 2,
};

/*
 * Avoid extra copying by making sd buffers page-aligned for DMA.
 */
#define sdmalloc(n)	mallocalign(n, BY2PG, 0, 0)
#define sdfree(p)	free(p)


/*
 * mmc/sd/sdio host controller interface
 */

struct SDiocmd {
	uchar	index;
	uchar	resp;	/* 0 = none, 1 = R1, 2 = R2, 3 = R3 ... */
	uchar	busy;
	uchar	data;	/* 1 = read, 2 = write, 3 = multi read, 4 = multi write */

	char	*name;
};

/* Commands */
extern SDiocmd GO_IDLE_STATE;
extern SDiocmd SEND_OP_COND;
extern SDiocmd ALL_SEND_CID;
extern SDiocmd SET_RELATIVE_ADDR;
extern SDiocmd SEND_RELATIVE_ADDR;
extern SDiocmd SWITCH;
extern SDiocmd SWITCH_FUNC;
extern SDiocmd SELECT_CARD;
extern SDiocmd SEND_EXT_CSD;
extern SDiocmd SD_SEND_IF_COND;
extern SDiocmd SEND_CSD;
extern SDiocmd STOP_TRANSMISSION;
extern SDiocmd SEND_STATUS;
extern SDiocmd SET_BLOCKLEN;
extern SDiocmd READ_SINGLE_BLOCK;
extern SDiocmd READ_MULTIPLE_BLOCK;
extern SDiocmd WRITE_SINGLE_BLOCK;
extern SDiocmd WRITE_MULTIPLE_BLOCK;

/* prefix for following app-specific commands */
extern SDiocmd APP_CMD;
extern SDiocmd SD_SET_BUS_WIDTH;
extern SDiocmd SD_SEND_OP_COND;

struct SDio {
	char	*name;
	int	(*init)(SDio*);
	void	(*enable)(SDio*);
	int	(*inquiry)(SDio*, char*, int);
	int	(*cmd)(SDio*, SDiocmd*, u32int, u32int*);
	void	(*iosetup)(SDio*, int, void*, int, int);
	void	(*io)(SDio*, int, uchar*, int);
	void	(*bus)(SDio*, int, int);
	void	(*led)(SDio*, int);
	int	(*cardintr)(SDio*, int);
	char	nomultiwrite;	/* quirk for usdhc */
	void	*aux;
};

extern void addmmcio(SDio *io);
extern SDio* annexsdio(char*);

/* devsd.c */
extern void sdadddevs(SDev*);
extern int sdsetsense(SDreq*, int, int, int, int);
extern int sdfakescsi(SDreq*);
extern int sdfakescsirw(SDreq*, uvlong*, int*, int*);
extern int sdaddfile(SDunit*, char*, int, char*, SDrw*, SDrw*);
extern void* sdannexctlr(char*, SDifc*);

/* sdscsi.c */
extern int scsiverify(SDunit*);
extern int scsionline(SDunit*);
extern long scsibio(SDunit*, int, int, void*, long, uvlong);
