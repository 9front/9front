typedef struct Url Url;
typedef struct Buq Buq;
typedef struct Buf Buf;
typedef struct Key Key;

typedef struct {
	char	*s1;
	char	*s2;
} Str2;

/* 9p */
typedef struct Req Req;

struct Url
{
	char	*scheme;
	char	*user;
	char	*pass;
	char	*host;
	char	*port;
	char	*path;
	char	*query;
	char	*fragment;
};

struct Buf
{
	Buf	*next;
	uchar	*rp;
	uchar	*ep;
	Req	*wreq;
	uchar	end[];
};

struct Key
{
	Key	*next;
	char	*val;
	char	key[];
};

struct Buq
{
	Ref;
	QLock;

	Url	*url;
	Key	*hdr;
	char	*error;

	int	closed;
	int	limit;
	int	size;
	int	nwq;

	/* write buffers */
	Buf	*bh;
	Buf	**bt;

	/* read requests */
	Req	*rh;
	Req	**rt;

	Rendez	rz;
};

int	debug;
Url	*proxy;
int	timeout;
char	*whitespace;

enum {
	Domlen = 256,
};
