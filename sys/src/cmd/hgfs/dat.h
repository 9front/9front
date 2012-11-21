enum {
	MAXPATH = 1024,
	BUFSZ = 1024,
	HASHSZ = 20,
};

typedef struct Revlog Revlog;
typedef struct Revmap Revmap;
typedef struct Revinfo Revinfo;
typedef struct Revtree Revtree;
typedef struct Revnode Revnode;
typedef struct Revfile Revfile;

struct Revmap
{
	int	rev;
	int	p1rev;
	int	p2rev;
	int	baserev;
	int	linkrev;

	uchar	hash[HASHSZ];

	int	flags;

	vlong	hoff;
	vlong	hlen;

	vlong	flen;

	void	*aux;
};

struct Revlog
{
	Ref;

	Revlog	*next;

	char	*path;

	int	ifd;
	int	dfd;

	vlong	ioff;

	int	nmap;
	Revmap	*map;

	int	tfd;
	int	tid;
};

struct Revnode
{
	char	*name;
	uchar	*hash;
	uvlong	path;

	Revnode	*up;
	Revnode	*next;
	Revnode	*down;
	Revnode *before;

	char	mode;
};

struct Revinfo
{
	uchar	chash[HASHSZ];
	uchar	mhash[HASHSZ];

	char	*who;
	char	*why;
	ulong	when;

	vlong	logoff;
	vlong	loglen;
};

struct Revtree
{
	Ref;
	int	level;
	Revnode	*root;
};

struct Revfile
{
	int	level;

	Revinfo	*info;
	Revtree	*tree;
	Revnode	*node;
	Revlog	*rlog;

	char	*buf;
	int	fd;
	int	doff;	/* length of metadata to skip */
};

uchar nullid[HASHSZ];
