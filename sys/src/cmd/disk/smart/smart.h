enum {
	Tscsi	= 1,
	Tata	= 2,

	Sok	= 0,
	Ssoon	= 1,
	Sfail	= 2,

	Nrb	= 32,
	Pathlen	= 256,
};

typedef struct Dtype Dtype;
typedef struct Sdisk Sdisk;

struct Dtype {
	int	type;
	char	*tname;
	int	(*probe)(Sdisk*);
	int	(*enable)(Sdisk*);
	int	(*status)(Sdisk*, char*, int);
};

struct Sdisk {
	Sdisk	*next;
	Dtype	*t;
	int	fd;
	Sfis;
	char	path[Pathlen];
	char	name[28];
	char	status;
	uchar	silent;
	uvlong	lastcheck;
	uvlong	lastlog;
};

int	scsiprobe(Sdisk*);
int	scsienable(Sdisk*);
int	scsistatus(Sdisk*, char*, int);
int	ataprobe(Sdisk*);
int	ataenable(Sdisk*);
int	atastatus(Sdisk*, char*, int);

void	eprint(Sdisk*, char *, ...);
