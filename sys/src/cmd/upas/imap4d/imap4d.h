/*
 * mailbox and message representations
 *
 * these structures are allocated with emalloc and must be explicitly freed
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <auth.h>
#include <avl.h>
#include <bin.h>

typedef struct Box	Box;
typedef struct Header	Header;
typedef struct Maddr	Maddr;
typedef struct Mblock	Mblock;
typedef struct Mimehdr	Mimehdr;
typedef struct Msg	Msg;
typedef struct Namedint	Namedint;
typedef struct Pair	Pair;
typedef struct Uidplus	Uidplus;

enum
{
	Stralloc		= 32,		/* characters allocated at a time */
	Bufsize		= IOUNIT,	/* size of transfer block */
	Ndigest		= 40,		/* length of digest string */
	Nuid		= 10,		/* length of .imp uid string */
	Nflags		= 8,		/* length of .imp flag string */
	Locksecs		= 5 * 60,	/* seconds to wait for acquiring a locked file */
	Pathlen		= 256,		/* max. length of upas/fs mbox name */
	Filelen		= 32,		/* max. length of a file in a upas/fs mbox */
	Userlen		= 64,		/* max. length of user's name */

	Mutf7max	= 6,		/* max bytes for a mutf7 character: &bbbb- */

	/*
	 * message flags
	 */
	Fseen		= 1 << 0,
	Fanswered	= 1 << 1,
	Fflagged	= 1 << 2,
	Fdeleted	= 1 << 3,
	Fdraft		= 1 << 4,
	Frecent		= 1 << 5,
};

typedef struct Fstree Fstree;
struct Fstree {
	Avl;
	Msg *m;
};

struct Box
{
	char	*name;		/* path name of mailbox */
	char	*fs;		/* fs name of mailbox */
	char	*fsdir;		/* /mail/fs/box->fs */
	char	*imp;		/* path name of .imp file */
	uchar	writable;	/* can write back messages? */
	uchar	dirtyimp;	/* .imp file needs to be written? */
	uchar	sendflags;	/* need flags update */
	Qid	qid;		/* qid of fs mailbox */
	Qid	impqid;		/* qid of .imp when last synched */
	long	mtime;		/* file mtime when last read */
	uint	max;		/* maximum msgs->seq, same as number of messages */
	uint	toldmax;	/* last value sent to client */
	uint	recent;		/* number of recently received messaged */
	uint	toldrecent;	/* last value sent to client */
	uint	uidnext;	/* next uid value assigned to a message */
	uint	uidvalidity;	/* uid of mailbox */
	Msg	*msgs;		/* msgs in uid order */
	Avltree	*fstree;		/* msgs in upas/fs order */
};

/*
 * fields of Msg->info
 */
enum
{
	/*
	 * read from upasfs
	 */
	Ifrom,
	Ito,
	Icc,
	Ireplyto,
	Iunixdate,
	Isubject,
	Itype,
	Idisposition,
	Ifilename,
	Idigest,
	Ibcc,
	Iinreplyto,
	Idate,
	Isender,
	Imessageid,
	Ilines,		/* number of lines of raw body */
	Isize,
//	Iflags,
//	Idatesec

	Imax
};

struct Header
{
	char	*buf;		/* header, including terminating \r\n */
	uint	size;		/* strlen(buf) */
	uint	lines;		/* number of \n characters in buf */

	/*
	 * pre-parsed mime headers
	 */
	Mimehdr	*type;		/* content-type */
	Mimehdr	*id;		/* content-id */
	Mimehdr	*description;	/* content-description */
	Mimehdr	*encoding;	/* content-transfer-encoding */
//	Mimehdr	*md5;		/* content-md5 */
	Mimehdr	*disposition;	/* content-disposition */
	Mimehdr	*language;	/* content-language */
};

struct Msg
{
	Msg	*next;
	Msg	*kids;
	Msg	*parent;
	char	*fsdir;		/* box->fsdir of enclosing message */
	Header	head;		/* message header */
	Header	mime;		/* mime header from enclosing multipart spec */
	int	flags;
	uchar	sendflags;	/* flags value needs to be sent to client */
	uchar	expunged;	/* message actually expunged, but not yet reported to client */
	uchar	matched;	/* search succeeded? */
	uint	uid;		/* imap unique identifier */
	uint	seq;		/* position in box; 1 is oldest */
	uint	id;		/* number of message directory in upas/fs */
	char	*fs;		/* name of message directory */
	char	*efs;		/* pointer after / in fs; enough space for file name */

	uint	size;		/* size of fs/rawbody, in bytes, with \r added before \n */
	uint	lines;		/* number of lines in rawbody */

	char	*ibuf;
	char	*info[Imax];	/* all info about message */

	Maddr	*to;		/* parsed out address lines */
	Maddr	*from;
	Maddr	*replyto;
	Maddr	*sender;
	Maddr	*cc;
	Maddr	*bcc;
};

/*
 * pre-parsed header lines
 */
struct Maddr
{
	char	*personal;
	char	*box;
	char	*host;
	Maddr	*next;
};

struct Mimehdr
{
	char	*s;
	char	*t;
	Mimehdr	*next;
};

/*
 * mapping of integer & names
 */
struct Namedint
{
	char	*name;
	int	v;
};

/*
 * lock for all mail file operations
 */
struct Mblock
{
	int	fd;
};

/*
 * parse nodes for imap4rev1 protocol
 *
 * important: all of these items are allocated
 * in one can, so they can be tossed out at the same time.
 * this allows leakless parse error recovery by simply tossing the can away.
 * however, it means these structures cannot be mixed with the mailbox structures
 */

typedef struct Fetch	Fetch;
typedef struct Nlist	Nlist;
typedef struct Slist	Slist;
typedef struct Msgset	Msgset;
typedef struct Store	Store;
typedef struct Search	Search;

/*
 * parse tree for fetch command
 */
enum
{
	Fenvelope,
	Fflags,
	Finternaldate,
	Frfc822,
	Frfc822head,
	Frfc822size,
	Frfc822text,
	Fbodystruct,
	Fuid,
	Fbody,			/* BODY */
	Fbodysect,		/* BODY [...] */
	Fbodypeek,

	Fmax
};

enum
{
	FPall,
	FPhead,
	FPheadfields,
	FPheadfieldsnot,
	FPmime,
	FPtext,

	FPmax
};

struct Fetch
{
	uchar	op;		/* F.* operator */
	uchar	part;		/* FP.* subpart for body[] & body.peek[]*/
	uchar	partial;	/* partial fetch? */
	long	start;		/* partial fetch amounts */
	long	size;
	Nlist	*sect;
	Slist	*hdrs;
	Fetch	*next;
};

/*
 * status items
 */
enum{
	Smessages	= 1 << 0,
	Srecent		= 1 << 1,
	Suidnext		= 1 << 2,
	Suidvalidity	= 1 << 3,
	Sunseen		= 1 << 4,
};

/*
 * parse tree for store command
 */
enum
{
	Stflags,
	Stflagssilent,

	Stmax
};

struct Store
{
	uchar	sign;
	uchar	op;
	int	flags;
};

/*
 * parse tree for search command
 */
enum
{
	SKnone,

	SKcharset,

	SKall,
	SKanswered,
	SKbcc,
	SKbefore,
	SKbody,
	SKcc,
	SKdeleted,
	SKdraft,
	SKflagged,
	SKfrom,
	SKheader,
	SKkeyword,
	SKlarger,
	SKnew,
	SKnot,
	SKold,
	SKon,
	SKor,
	SKrecent,
	SKseen,
	SKsentbefore,
	SKsenton,
	SKsentsince,
	SKset,
	SKsince,
	SKsmaller,
	SKsubject,
	SKtext,
	SKto,
	SKuid,
	SKunanswered,
	SKundeleted,
	SKundraft,
	SKunflagged,
	SKunkeyword,
	SKunseen,

	SKmax
};

struct Search
{
	int	key;
	char	*s;
	char	*hdr;
	uint	num;
	int	year;
	int	mon;
	int	mday;
	Msgset	*set;
	Search	*left;
	Search	*right;
	Search	*next;
};

struct Nlist
{
	uint	n;
	Nlist	*next;
};

struct Slist
{
	char	*s;
	Slist	*next;
};

struct Msgset
{
	uint	from;
	uint	to;
	Msgset	*next;
};

struct Pair
{
	uint	start;
	uint	stop;
};

struct Uidplus
{
	uint	uid;
	uint	uidvalidity;
	Uidplus	*next;
};

extern	Bin	*parsebin;
extern	Biobuf	bout;
extern	Biobuf	bin;
extern	char	username[Userlen];
extern	char	mboxdir[Pathlen];
extern	char	*fetchpartnames[FPmax];
extern	char	*binupas;
extern	char	*site;
extern	char	*remote;
extern	int	debug;
extern	char	logfile[28];
extern	Uidplus	*uidlist;
extern	Uidplus	**uidtl;

#include "fns.h"
