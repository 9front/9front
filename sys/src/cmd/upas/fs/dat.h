#include <avl.h>

enum {
	/* cache states */
	Cidx		= 1<<0,
	Cidxstale	= 1<<1,
	Cheader 	= 1<<2,
	Cbody		= 1<<3,
	Cnew		= 1<<4,

	/* encodings */
	Enone=	0,
	Ebase64,
	Equoted,

	/* dispositions */
	Dnone=	0,
	Dinline,
	Dfile,
	Dignore,

	/* mb create flags */
	DMcreate	=  0x02000000,

	/* rm flags */
	Rrecur		= 1<<0,
	Rtrunc		= 1<<1,

	/* m->deleted flags */
	Deleted		= 1<<0,
	Dup		= 1<<1,
	Dead		= 1<<2,
	Disappear	= 1<<3,
	Dmark		= 1<<4,	/* temporary mark for idx scan */

	/* mime flags */
	Mtrunc		= 1<<0,	/* message had no boundary */

	Maxmsg		= 75*1024*1024,	/* maxmessage size; debugging */
	Maxcache	= 512*1024,	/* target cache size; set low for debugging */
	Nctab		= 15,		/* max # of cached messages >10 */
	Nref		= 10,
};

typedef struct Idx Idx;
struct Idx {
	char	*str;			/* as read from idx file */
	uchar	*digest;
	uchar	flags;
	uvlong	fileid;
	ulong	lines;
	ulong	size;
	ulong	rawbsize;			/* nasty imap4d */
	ulong	ibadchars;

	char	*ffrom;
	char	*from;
	char	*to;
	char	*cc;
	char	*bcc;
	char	*replyto;
	char	*messageid;
	char	*subject;
	char	*sender;
	char	*inreplyto;
	char	*idxaux;		/* mailbox specific */

	char	*type;			/* mime info */
	char	*filename;
	char	disposition;

	int	nparts;
};

typedef struct Message Message;
struct Message {
	ulong	id;
	int	refs;
	int	subname;
	char	name[12];

	/* top-level indexed information */
	Idx;

	/* caching help */
	uchar	cstate;
	ulong	infolen;
	ulong	csize;

	/*
	 * a plethoria of pointers into message
	 * and some status.  not valid unless cached
	 */
	char	*start;		/* start of message */
	char	*end;		/* end of message */
	char	*header;		/* start of header */
	char	*hend;		/* end of header */
	int	hlen;		/* length of header minus ignored fields */
	char	*mheader;	/* start of mime header */
	char	*mhend;		/* end of mime header */
	char	*body;		/* start of body */
	char	*bend;		/* end of body */
	char	*rbody;		/* raw (unprocessed) body */
	char	*rbend;		/* end of raw (unprocessed) body */
	char	mallocd;		/* message is malloc'd */
	char	ballocd;		/* body is malloc'd */
	char	hallocd;		/* header is malloc'd */
	int	badchars;	/* running count of bad chars. */

	char	deleted;
	char	inmbox;

	/* mail info */
	char	*unixheader;
	char	*unixfrom;
	char	*date822;
	char	*references[Nref];

	/* mime info */
	char	*charset;		
	char	*boundary;
	char	converted;
	char	encoding;
	char	decoded;
	char	mimeflag;

	Message	*next;
	Message	*part;
	Message	*whole;
	Message	*lru;		/* least recently use chain */

	union{
		char	*lim;	/* used by plan9; not compatable with cache */
		vlong	imapuid;	/* used by imap4 */
		void	*aux;
	};
};

typedef struct {
	Avl;
	Message *m;
} Mtree;

typedef struct Mcache Mcache;
struct Mcache {
	uvlong	cached;
	int	nlru;
	Message	*lru;
};

typedef struct Mailbox Mailbox;
struct Mailbox {
	int	refs;
	Mailbox	*next;
	ulong	id;
	int	flags;
	char	rmflags;
	char	dolock;		/* lock when syncing? */
	char	addfrom;
	char	name[Elemlen];
	char	path[Pathlen];
	Dir	*d;
	Message	*root;
	Avltree	*mtree;
	ulong	vers;		/* goes up each time mailbox is changed */

	/* cache tracking */
	Mcache;

	/* index tracking */
	Qid	qid;

	ulong	waketime;
	void	(*close)(Mailbox*);
	void	(*decache)(Mailbox*, Message*);
	int	(*fetch)(Mailbox*, Message*, uvlong, ulong);
	void	(*delete)(Mailbox*, Message*);
	char	*(*ctl)(Mailbox*, int, char**);
	char	*(*remove)(Mailbox *, int);
	char	*(*rename)(Mailbox*, char*, int);
	char	*(*sync)(Mailbox*);
	void	(*modflags)(Mailbox*, Message*, int);
	void	(*idxwrite)(Biobuf*, Mailbox*);
	int	(*idxread)(char*, Mailbox*);
	void	(*idxinvalid)(Mailbox*);
	void	*aux;		/* private to Mailbox implementation */

	int	syncing;	/* currently syncing? */
};

typedef char *Mailboxinit(Mailbox*, char*);

Mailboxinit	plan9mbox;
Mailboxinit	pop3mbox;
Mailboxinit	imap4mbox;
Mailboxinit	mdirmbox;

void		genericidxwrite(Biobuf*, Mailbox*);
int		genericidxread(char*, Mailbox*);
void		genericidxinvalid(Mailbox*);

void		cachehash(Mailbox*, Message*);
int		cacheheaders(Mailbox*, Message*);		/* "getcache" */
int		cachebody(Mailbox*, Message*);
int		cacheidx(Mailbox*, Message*);
int		insurecache(Mailbox*, Message*);

/**/
void		putcache(Mailbox*, Message*);		/* asymmetricial */
void		cachefree(Mailbox*, Message*);

char*		syncmbox(Mailbox*, int);
void*		emalloc(ulong);
void*		erealloc(void*, ulong);
Message*	newmessage(Message*);
void		unnewmessage(Mailbox*, Message*, Message*);
char*		delmessages(int, char**);
char		*flagmessages(int, char**);
void		digestmessage(Mailbox*, Message*);

int		wraptls(int, char*);

void		eprint(char*, ...);
void		iprint(char *, ...);
char*		newmbox(char*, char*, int, Mailbox**);
void		freembox(char*);
char*		removembox(char*, int);
void		syncallmboxes(void);
void		logmsg(Message*, char*, ...);
void		msgincref(Mailbox*, Message*);
void		msgdecref(Mailbox*, Message*);
void		mboxincref(Mailbox*);
void		mboxdecref(Mailbox*);
char		*mboxrename(char*, char*, int);
void		convert(Message*);
void		decode(Message*);
int		decquoted(char*, char*, char*, int);
int		xtoutf(char*, char**, char*, char*);
ulong		countlines(Message*);
void		parse(Mailbox*, Message*, int, int);
void		parseheaders(Mailbox*, Message*, int, int);
void		parsebody(Message*, Mailbox*);
char*		date822tounix(Message*, char*);
int		strtotm(char*, Tm*);
char*		lowercase(char*);

char*		sputc(char*, char*, int);
char*		seappend(char*, char*, char*, int);

int		hdrlen(char*, char*);
char*		rfc2047(char*, char*, char*, int, int);

char*		localremove(Mailbox*, int);
char*		localrename(Mailbox*, char*, int);
void		rmidx(char*, int);
int		vremove(char*);
int		rename(char *, char*, int);

int		mtreecmp(Avl*, Avl*);
int		mtreeisdup(Mailbox *, Message *);
Message*	mtreefind(Mailbox*, uchar*);
void		mtreeadd(Mailbox*, Message*);
void		mtreedelete(Mailbox*, Message*);

enum {
	/* mail sub-objects; must be sorted */
	Qbcc,
	Qbody,
	Qcc,
	Qdate,
	Qdigest,
	Qdisposition,
	Qffrom,
	Qfileid,
	Qfilename,
	Qflags,
	Qfrom,
	Qheader,
	Qinfo,
	Qinreplyto,
	Qlines,
	Qmessageid,
	Qmimeheader,
	Qraw,
	Qrawbody,
	Qrawheader,
	Qrawunix,
	Qreferences,
	Qreplyto,
	Qsender,
	Qsize,
	Qsubject,
	Qto,
	Qtype,
	Qunixdate,
	Qunixheader,
	Qmax,

	/* other files */
	Qtop,
	Qmbox,
	Qdir,
	Qctl,
	Qmboxctl,
};

#define PATH(id, f)	(((uvlong)(id)<<10) | (f))
#define FILE(p)		((int) (p) & 0x3ff)

/* hash table to aid in name lookup, all files have an entry */
typedef struct Hash Hash;
struct Hash {
	Hash	*next;
	char	*name;
	uvlong	ppath;
	Qid	qid;
	Mailbox	*mb;
	Message	*m;
};

ulong	hash(char*);
Hash	*hlook(uvlong, char*);
void	henter(uvlong, char*, Qid, Message*, Mailbox*);
void	hfree(uvlong, char*);

char	*intern(char*);
void	idxfree(Idx*);
int	rdidxfile(Mailbox*);
int	wridxfile(Mailbox*);

char	*modflags(Mailbox*, Message*, char*);
int	getmtokens(char *, char**, int, int);

extern char	Enotme[];
extern char	*mntpt;
extern char	user[Elemlen];
extern char 	*dirtab[];
extern int	Sflag;
extern int	iflag;
extern int	biffing;
extern ulong	cachetarg;
extern int	debug;
extern int	lflag;
extern int	plumbing;
extern ulong	msgallocd;
extern ulong	msgfreed;
extern Mailbox	*mbl;
extern Message	*root;

#define	dprint(...)	if(debug) fprint(2, __VA_ARGS__); else {}
#define	Topmsg(mb, m)	(m->whole == mb->root)
#pragma	varargck	type	"A"	uchar*
#pragma	varargck	type	"D"	uvlong
#pragma	varargck	type	"Δ"	uvlong
#pragma	varargck	argpos	eprint	1
#pragma	varargck	argpos	iprint	1
#pragma	varargck	argpos	logmsg	2

