typedef struct MLock	MLock;
typedef	struct Iosect	Iosect;
typedef	struct Iotrack	Iotrack;
typedef struct Xfs	Xfs;

struct MLock
{
	char	key;
};

struct Iosect
{
	Iosect *next;
	short	flags;
	MLock	lock;
	Iotrack *t;
	uchar *	iobuf;
};

struct Iotrack
{
	short	flags;
	Xfs *	xf;
	vlong	addr;
	Iotrack	*next;		/* in lru list */
	Iotrack	*prev;
	Iotrack	*hnext;		/* in hash list */
	Iotrack	*hprev;
	MLock	lock;
	int	ref;
	Iosect	**tp;
};

#define	BMOD		(1<<0)
#define	BIMM		(1<<1)
#define	BSTALE		(1<<2)

Iosect*	getosect(Xfs*, vlong);
Iosect*	getsect(Xfs*, vlong);
int	canmlock(MLock*);
int	devread(Xfs*, vlong, void*, long);
int	devwrite(Xfs*, vlong, void*, long);
int	tread(Iotrack*);
int	twrite(Iotrack*);
void	iotrack_init(void);
void	mlock(MLock*);
void	purgebuf(Xfs*);
void	putsect(Iosect*);
void	sync(void);
void	unmlock(MLock*);
