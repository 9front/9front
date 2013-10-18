typedef struct Block Block;
struct Block
{
	Ref;

	Block	*next;

	uchar	*rp;
	uchar	*wp;
	uchar	*lim;

	uchar	base[];
};

#define BLEN(s)	((s)->wp - (s)->rp)

Block*	allocb(int size);
void	freeb(Block*);
Block*	copyblock(Block*, int);

typedef struct Ehdr Ehdr;
struct Ehdr
{
	uchar	d[6];
	uchar	s[6];
	uchar	type[2];
};

enum {
	Ehdrsz	= 6+6+2,
	Maxpkt	= 2000,
};

enum
{
	Cdcunion = 6,
	Scether = 6,
	Fnether = 15,
};

int debug;
int setmac;

/* to be filled in by *init() */
uchar macaddr[6];

void	etheriq(Block*, int wire);

int	(*epreceive)(Dev*);
void	(*eptransmit)(Dev*, Block*);
