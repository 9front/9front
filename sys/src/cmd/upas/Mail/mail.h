typedef struct Event	Event;
typedef struct Win	Win;
typedef struct Mesg	Mesg;
typedef struct Mbox	Mbox;
typedef struct Comp	Comp;

enum {
	Stack	= 64*1024,
	Bufsz	= 8192,
	Eventsz	= 256*UTFmax,
	Subjlen	= 56,
};

enum {
	Sdummy	= 1<<0,	/* message placeholder */
	Stoplev	= 1<<1,	/* not a response to anything */
	Sopen	= 1<<2,	/* opened for viewing */
	Szap	= 1<<3, /* flushed, to be removed from list */
};

enum {
	Fresp	= 1<<0,	/* has been responded to */
	Fseen	= 1<<1,	/* has been viewed */
	Fdel	= 1<<2, /* was deleted */
	Ftodel	= 1<<3,	/* pending deletion */
};

enum {
	Vflat,
	Vgroup,
};

struct Event {
	char	action;
	char	type;
	int	p0;	/* click point */
	int	q0;	/* expand lo */
	int	q1;	/* expand hi */
	int	flags;
	int	ntext;
	char	text[Eventsz + 1];
};

struct Win {
	int	id;
	Ioproc	*io;
	Biobuf	*event;
	int	revent;
	int	ctl;
	int	addr;
	int	data;
	int	open;
};

/*
 * In progress compositon
 */
struct Comp {
	Win;

	/* exec setup */
	Channel *sync;
	int	fd[2];

	/* to relate back the message */
	char	*replyto;
	char	*rname;
	char	*rpath;
	char	*rdigest;
	char	*path;

	int	quitting;
	Comp	*qnext;
};

/*
 * Message in mailbox
 */
struct Mesg {
	Win;

	/* bookkeeping */
	char	*name;
	int	state;
	int	flags;
	u32int	hash;
	char	quitting;
	Mesg	*qnext;

	/* exec setup */
	Channel *sync;
	char	*path;
	int	fd[2];

	Mesg	*parent;
	Mesg	**child;
	int	nchild;
	int	nsub;	/* transitive children */
	
	Mesg	*body;	/* best attachment to use, or nil */
	Mesg	**parts;
	int	nparts;
	int xparts;

	/* info fields */
	char	*from;
	char	*to;
	char	*cc;
	char	*replyto;
	char	*date;
	char	*subject;
	char	*type;
	char	*disposition;
	char	*messageid;
	char	*filename;
	char	*digest;
	char	*mflags;
	char	*fromcolon;
	char	*inreplyto;

	vlong	time;
};

/*
 *The mailbox we're showing.
 */
struct Mbox {
	Win;

	Mesg	**mesg;
	Mesg	**hash;
	int	mesgsz;
	int	hashsz;
	int	nmesg;
	int	ndead;

	Mesg	*openmesg;
	Comp	*opencomp;
	int	canquit;

	Channel	*see;
	Channel	*show;
	Channel	*event;
	Channel	*send;

	int	view;
	int	nopen;
	char	*path;
};

extern Mbox	mbox;
extern Reprog	*mesgpat;
extern char	*savebox;

/* window management */
void	wininit(Win*, char*);
int	winopen(Win*, char*, int);
Biobuf	*bwinopen(Win*, char*, int);
Biobuf	*bwindata(Win*, int);
void	winclose(Win*);
void	wintagwrite(Win*, char*);
int	winevent(Win*, Event*);
void	winreturn(Win*, Event*);
int	winread(Win*, int, int, char*, int);
int	matchmesg(Win*, char*);
char	*winreadsel(Win*);
void	wingetsel(Win*, int*, int*);
void	winsetsel(Win*, int, int);

/* messages */
Mesg	*mesglookup(char*, char*);
Mesg	*mesgload(char*);
Mesg	*mesgopen(char*, char*);
int	mesgmatch(Mesg*, char*, char*);
void	mesgclear(Mesg*);
void	mesgfree(Mesg*);
void	mesgpath2name(char*, int, char*);
int	mesgflagparse(char*, int*);
Biobuf*	mesgopenbody(Mesg*);

/* mailbox */
void	mbredraw(Mesg*, int, int);

/* composition */
void	compose(char*, Mesg*, int);

/* utils */
void	*emalloc(ulong);
void	*erealloc(void*, ulong);
char	*estrdup(char*);
char	*estrjoin(char*, ...);
char	*esmprint(char*, ...);
char	*rslurp(Mesg*, char*, int*);
char	*fslurp(int, int*);
u32int	strhash(char*);

