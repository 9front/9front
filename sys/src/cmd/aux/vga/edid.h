typedef struct Modelist Modelist;
typedef struct Edid Edid;
typedef struct Flag Flag;

struct Edid {
	char		mfr[4];		/* manufacturer */
	char		serialstr[16];	/* serial number as string (in extended data) */
	char		name[16];	/* monitor name as string (in extended data) */
	ushort		product;	/* product code, 0 = unused */
	ulong		serial;		/* serial number, 0 = unused */
	uchar		version;	/* major version number */
	uchar		revision;	/* minor version number */
	uchar		mfrweek;	/* week of manufacture, 0 = unused */
	int		mfryear;	/* year of manufacture, 0 = unused */
	uchar 		dxcm;		/* horizontal image size in cm. */
	uchar		dycm;		/* vertical image size in cm. */
	int		gamma;		/* gamma*100 */
	int		rrmin;		/* minimum vertical refresh rate */
	int		rrmax;		/* maximum vertical refresh rate */
	int		hrmin;		/* minimum horizontal refresh rate */
	int		hrmax;		/* maximum horizontal refresh rate */
	ulong		pclkmax;	/* maximum pixel clock */
	int		flags;
	Modelist	*modelist;	/* list of supported modes */
};

struct Modelist
{
	Mode;
	Modelist *next;
};

struct Flag {
	int bit;
	char *desc;
};

enum {
	Fdigital	= 1<<0,	/* is a digital display */
	Fdpmsstandby	= 1<<1,	/* supports DPMS standby mode */
	Fdpmssuspend	= 1<<2,	/* supports DPMS suspend mode */
	Fdpmsactiveoff	= 1<<3,	/* supports DPMS active off mode */
	Fmonochrome	= 1<<4,	/* is a monochrome display */
	Fgtf		= 1<<5,	/* supports VESA GTF: see /public/doc/vesa/gtf10.pdf */
};
Flag	edidflags[];
void	printflags(Flag *f, int b);

int	parseedid128(Edid *e, void *v);
void	printedid(Edid *e);
