typedef struct	Snarf	Snarf;

struct Snarf
{
	QLock;
	int		vers;
	int		n;
	char		*buf;
};

enum
{
	MAXSNARF	= 100*1024
};

extern	Snarf		snarf;
extern	int		kbdin;

void			screenputs(char*, int);
void			vncputc(int, int);
void			setsnarf(char *buf, int n, int *vers);
