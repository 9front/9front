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
int (*epread)(Dev *, uchar *, int);
void (*epwrite)(Dev *, uchar *, int);

/* temporary buffers */
uchar bout[4*1024];
uchar bin[4*1024];
int nbin;
