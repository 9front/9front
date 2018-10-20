#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

// path:
// 3 bits - generic file type (Qinctl, Qindata)
// 3 bits - parent type
// 3 bits - chosen scheme type (Qgeneric, Qbcm, Qboard, Qwpi)
// 6 bits - input number

#define PIN_TABLE_SIZE	32

#define PIN_OFFSET		SCHEME_OFFSET + SCHEME_BITS
#define PIN_BITS		6
#define PIN_MASK		((1 << PIN_BITS) - 1)
#define PIN_NUMBER(q)	(((q).path >> PIN_OFFSET) & PIN_MASK)

#define SCHEME_OFFSET	PARENT_OFFSET + PARENT_BITS
#define SCHEME_BITS	3
#define SCHEME_MASK	((1 << SCHEME_BITS) - 1)
#define SCHEME_TYPE(q)	(((q).path >> SCHEME_OFFSET) & SCHEME_MASK)

#define PARENT_OFFSET	FILE_OFFSET + FILE_BITS
#define PARENT_BITS	3
#define PARENT_MASK	((1 << PARENT_BITS) - 1)
#define PARENT_TYPE(q)	(((q).path >> PARENT_OFFSET) & PARENT_MASK)

#define FILE_OFFSET	0
#define FILE_BITS		3
#define FILE_MASK		((1 << FILE_BITS) - 1)
#define FILE_TYPE(q)	(((q).path >> FILE_OFFSET) & FILE_MASK)

// pin is valid only when file is Qdata otherwise 0 is used
#define PATH(pin, scheme, parent, file) \
						((pin & PIN_MASK) << PIN_OFFSET) \
						| ((scheme & SCHEME_MASK) << SCHEME_OFFSET) \
						| ((parent & PARENT_MASK) << PARENT_OFFSET) \
						| ((file & FILE_MASK) << FILE_OFFSET)

#define SET_BIT(f, offset, value) \
	(*f = ((*f & ~(1 << (offset % 32))) | (value << (offset % 32))))

static int dflag = 0;
#define D(...)	if(dflag) print(__VA_ARGS__)

enum {
	// parent types
	Qtopdir = 0,
	Qgpiodir,
	// file types
	Qdir,
	Qdata,
	Qctl,
	Qevent,
};
enum {
	// naming schemes
	Qbcm,
	Qboard,
	Qwpi,
	Qgeneric
};


// commands
enum {
	CMzero,
	CMone,
	CMscheme,
	CMfunc,
	CMpull,
	CMevent,
};

// dev entries
Dirtab topdir = { "#G", {PATH(0, Qgeneric, Qtopdir, Qdir), 0, QTDIR}, 0, 0555 };
Dirtab gpiodir = { "gpio", {PATH(0, Qgeneric, Qgpiodir, Qdir), 0, QTDIR}, 0, 0555 };

Dirtab typedir[] = {
	"OK",	{ PATH(16, Qgeneric, Qgpiodir, Qdata), 0, QTFILE }, 0, 0666,
	"ctl",	{ PATH(0, Qgeneric, Qgpiodir, Qctl), 0, QTFILE }, 0, 0666,
	"event",	{ PATH(0, Qgeneric, Qgpiodir, Qevent), 0, QTFILE }, 0, 0444,
};

// commands definition
static
Cmdtab gpiocmd[] = {
	CMzero,		"0",		1,
	CMone,		"1",		1,
	CMscheme,	"scheme",	2,
	CMfunc, 	"function",	3,
	CMpull,		"pull",		3,
	CMevent,	"event",	4,
};

static int pinscheme;
static int boardrev;

static Rendez rend;
static u32int eventvalue;
static long eventinuse;
static Lock eventlock;

//
// BCM
//
enum {
	Fin = 0,
	Fout,
	Ffunc5,
	Ffunc4,
	Ffunc0,
	Ffunc1,
	Ffunc2,
	Ffunc3,
};

static char *funcname[] = {
	"in", "out", "5", "4", "0", "1", "2", "3",
};

enum {
	Poff = 0,
	Pdown,
	Pup,
};

static char *pudname[] = {
	"off", "down", "up",
};

static char *evstatename[] = {
	"disable", "enable",
};

enum {
	Erising,
	Efalling,
};

static char *evtypename[] = {
	"edge-rising", "edge-falling",
};

static char *bcmtableR1[PIN_TABLE_SIZE] = {
	"1", "2", 0, 0,			// 0-3
	"4", 0, 0, "7",			// 4-7
	"8", "9", "10", "11",	// 8-11
	0, 0, "14", "15",		// 12-15
	0, "17", "18", 0,		// 16-19
	0, "21", "22", "23",	// 20-23
	"24", "25", 0, 0,		// 24-27
	0, 0, 0, 0,				// 28-31
};

static char *bcmtableR2[PIN_TABLE_SIZE] = {
	0, 0, "2", "3",			// 0-3
	"4", 0, 0, "7",			// 4-7
	"8", "9", "10", "11",	// 8-11
	0, 0, "14", "15",		// 12-15
	0, "17", "18", 0,		// 16-19
	0, 0, "22", "23",		// 20-23
	"24", "25", 0, "27",	// 24-27
	"28", "29", "30", "31",	// 28-31
};

static char *boardtableR1[PIN_TABLE_SIZE] = {
	"SDA", "SCL", 0, 0,				// 0-3
	"GPIO7", 0, 0, "CE1",			// 4-7
	"CE0", "MISO", "MOSI", "SCLK",	// 8-11
	0, 0, "TxD", "RxD",				// 12-15
	0, "GPIO0", "GPIO1", 0,			// 16-19
	0, "GPIO2", "GPIO3", "GPIO4",	// 20-23
	"GPIO5", "GPIO6", 0, 0,			// 24-27
	0, 0, 0, 0,						// 28-31
};

static char *boardtableR2[PIN_TABLE_SIZE] = {
	0, 0, "SDA", "SCL",						// 0-3
	"GPIO7", 0, 0, "CE1",					// 4-7
	"CE0", "MISO", "MOSI", "SCLK",			// 8-11
	0, 0, "TxD", "RxD",						// 12-15
	0, "GPIO0", "GPIO1", 0,					// 16-19
	0, 0, "GPIO3", "GPIO4",					// 20-23
	"GPIO5", "GPIO6", 0, "GPIO2",			// 24-27
	"GPIO8", "GPIO9", "GPIO10", "GPIO11",	// 28-31
};

static char *wpitableR1[PIN_TABLE_SIZE] = {
	"8", "9", 0, 0,			// 0-3
	"7", 0, 0, "11",		// 4-7
	"10", "13", "12", "14",	// 8-11
	0, 0, "15", "16",		// 12-15
	0, "0", "1", 0,			// 16-19
	0, "2", "3", "4",		// 20-23
	"5", "6", 0, 0,			// 24-27
	0, 0, 0, 0,				// 28-31
};

static char *wpitableR2[PIN_TABLE_SIZE] = {
	0, 0, "8", "9",			// 0-3
	"7", 0, 0, "11",		// 4-7
	"10", "13", "12", "14",	// 8-11
	0, 0, "15", "16",		// 12-15
	0, "0", "1", 0,			// 16-19
	0, 0, "3", "4",			// 20-23
	"5", "6", 0, "2",		// 24-27
	"17", "18", "19", "20",	// 28-31
};

static char *schemename[] = {
	"bcm", "board", "wpi",
};

static char**
getpintable(void)
{
	switch(pinscheme)
	{
	case Qbcm:
		return (boardrev>3)?bcmtableR2:bcmtableR1;
	case Qboard:
		return (boardrev>3)?boardtableR2:boardtableR1;
	case Qwpi:
		return (boardrev>3)?wpitableR2:wpitableR1;
	default:
		return nil;
	}
}

static void
mkdeventry(Chan *c, Qid qid, Dirtab *tab, Dir *db)
{
	mkqid(&qid, tab->qid.path, tab->qid.vers, tab->qid.type);
	devdir(c, qid, tab->name, tab->length, eve, tab->perm, db);
}

static int
gpiogen(Chan *c, char *, Dirtab *, int , int s, Dir *db)
{
	Qid qid;
	int parent, scheme, l;
	char **pintable = getpintable();
	
	qid.vers = 0;
	parent = PARENT_TYPE(c->qid);
	scheme = SCHEME_TYPE(c->qid);
	
	if(s == DEVDOTDOT)
	{
		switch(parent)
		{
		case Qtopdir:
		case Qgpiodir:
			mkdeventry(c, qid, &topdir, db);
			break;
		default:
			return -1;
		}
		return 1;
	}

	if(parent == Qtopdir)
	{
		switch(s)
		{
		case 0:
			mkdeventry(c, qid, &gpiodir, db);
			break;
		default:
			return -1;
		}
	return 1;
	}

	if(scheme != Qgeneric && scheme != pinscheme)
	{
		error(nil);
	}

	if(parent == Qgpiodir)
	{
		l = nelem(typedir);
		if(s < l)
		{
			mkdeventry(c, qid, &typedir[s], db);
		} else if (s < l + PIN_TABLE_SIZE)
		{
			s -= l;
			
			if(pintable[s] == 0)
			{
				return 0;
			}
			mkqid(&qid, PATH(s, pinscheme, Qgpiodir, Qdata), 0, QTFILE);
			snprint(up->genbuf, sizeof up->genbuf, "%s", pintable[s]);
			devdir(c, qid, up->genbuf, 0, eve, 0666, db);
		}
		else
		{
			return -1;
		}
		return 1;
	}

	return 1;
}

static void
interrupt(Ureg*, void *)
{
	
	uint pin;
	
	coherence();
	
	eventvalue = 0;
	
	for(pin = 0; pin < PIN_TABLE_SIZE; pin++)
	{
		if(gpiogetevent(pin))
			SET_BIT(&eventvalue, pin, 1);
	}
	coherence();

	wakeup(&rend);
}

static void
gpioinit(void)
{
	gpiomeminit();
	boardrev = getboardrev() & 0xff;
	pinscheme = Qboard;
	intrenable(49, interrupt, nil, 0, "gpio1");
}

static void
gpioshutdown(void)
{ }

static Chan*
gpioattach(char *spec)
{
	return devattach('G', spec);
}

static Walkqid*
gpiowalk(Chan *c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, gpiogen);
}

static int
gpiostat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, 0, 0, gpiogen);
}

static Chan*
gpioopen(Chan *c, int omode)
{
	int type;
	
	c = devopen(c, omode, 0, 0, gpiogen);
	
	type = FILE_TYPE(c->qid);
	
	switch(type)
	{
	case Qdata:
		c->iounit = 1;
		break;
	case Qctl:
		break;
	case Qevent:
		lock(&eventlock);
		if(eventinuse != 0){
			c->flag &= ~COPEN;
			unlock(&eventlock);
			error(Einuse);
		}
		eventinuse = 1;
		unlock(&eventlock);
		eventvalue = 0;
		c->iounit = 4;
	}

	return c;
}

static void
gpioclose(Chan *c)
{
	int type;
	type = FILE_TYPE(c->qid);
	
	switch(type)
	{
	case Qevent:
		if(c->flag & COPEN)
		{
			if(c->flag & COPEN){
				eventinuse = 0;
			}
		}
		break;
	}
}

static int
isset(void *)
{
	return eventvalue;
}

static long
gpioread(Chan *c, void *va, long n, vlong off)
{
	int type, scheme;
	uint pin;
	char *a;
	
	a = va;
	
	if(c->qid.type & QTDIR)
	{
		return devdirread(c, va, n, 0, 0, gpiogen);
	}

	type = FILE_TYPE(c->qid);
	scheme = SCHEME_TYPE(c->qid);
	
	if(scheme != Qgeneric && scheme != pinscheme)
	{
		error(nil);
	}

	switch(type)
	{
	case Qdata:
		pin = PIN_NUMBER(c->qid);
		a[0] = (gpioin(pin))?'1':'0';
		n = 1;
		break;
	case Qctl:
		break;
	case Qevent:
		if(off >= 4)
		{
			off %= 4;
			eventvalue = 0;
		}
		sleep(&rend, isset, 0);
			
		if(off + n > 4)
		{
			n = 4 - off;
		}
		memmove(a, &eventvalue + off, n);
	}

	return n;
}

static int
getpin(char *pinname)
{
	int i;
	char **pintable = getpintable();
	for(i = 0; i < PIN_TABLE_SIZE; i++)
	{
		if(!pintable[i])
		{
			continue;
		}
		if(strncmp(pintable[i], pinname, strlen(pintable[i])) == 0)
		{
			return i;
		}
	}
	return -1;
}

static long
gpiowrite(Chan *c, void *va, long n, vlong)
{
	int type, i, scheme;
	uint pin;
	char *arg;

	Cmdbuf *cb;
	Cmdtab *ct;

	if(c->qid.type & QTDIR)
	{
		error(Eisdir);
	}

	type = FILE_TYPE(c->qid);

	scheme = SCHEME_TYPE(c->qid);
	
	if(scheme != Qgeneric && scheme != pinscheme)
	{
		error(nil);
	}

	cb = parsecmd(va, n);
	if(waserror())
	{
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, gpiocmd,  nelem(gpiocmd));
	if(ct == nil)
	{
		error(Ebadctl);
	}
	
	switch(type)
	{
	case Qdata:
		pin = PIN_NUMBER(c->qid);

		switch(ct->index)
		{
		case CMzero:
			gpioout(pin, 0);
			break;
		case CMone:
			gpioout(pin, 1);
			break;
		default:
			error(Ebadctl);
		}
		break;
	case Qctl:
		switch(ct->index)
		{
		case CMscheme:
			arg = cb->f[1];
			for(i = 0; i < nelem(schemename); i++)
			{
				if(strncmp(schemename[i], arg, strlen(schemename[i])) == 0)
				{
					pinscheme = i;
					break;
				}
			}
			break;
		case CMfunc:
			pin = getpin(cb->f[2]);
			arg = cb->f[1];
			if(pin == -1) {
				error(Ebadctl);
			}
			for(i = 0; i < nelem(funcname); i++)
			{
				if(strncmp(funcname[i], arg, strlen(funcname[i])) == 0)
				{
					gpiosel(pin, i);
					break;
				}
			}
			break;
		case CMpull:
			pin = getpin(cb->f[2]);
			if(pin == -1) {
				error(Ebadctl);
			}
			arg = cb->f[1];
			for(i = 0; i < nelem(pudname); i++)
			{
				if(strncmp(pudname[i], arg, strlen(pudname[i])) == 0)
				{
					gpiopull(pin, i);
					break;
				}
			}
			break;
		case CMevent:
			pin = getpin(cb->f[3]);
			if(pin == -1) {
				error(Ebadctl);
			}
				
			arg = cb->f[1];
			for(i = 0; i < nelem(evtypename); i++)
			{
				if(strncmp(evtypename[i], arg, strlen(evtypename[i])) == 0)
				{
					gpioselevent(pin, i, (cb->f[2][0] == 'e'));
					break;
				}
			}
			break;
		default:
			error(Ebadctl);
		}
		break;
	}
	
	free(cb);

	poperror();
	return n;
}

Dev gpiodevtab = {
	'G',
	"gpio",

	devreset,
	gpioinit,
	gpioshutdown,
	gpioattach,
	gpiowalk,
	gpiostat,
	gpioopen,
	devcreate,
	gpioclose,
	gpioread,
	devbread,
	gpiowrite,
	devbwrite,
	devremove,
	devwstat,
};
