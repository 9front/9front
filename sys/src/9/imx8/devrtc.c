/*
 *  NXP PCF8523 real time clock
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"../port/i2c.h"

enum {
	Seconds=	0x03,
	Minutes=	0x04,
	Hours=		0x05, 
	Mday=		0x06,
	Wday=		0x07,
	Month=		0x08,
	Year=		0x09,
	Nbcd=		1+Year-Seconds,
};

typedef struct Rtc	Rtc;
struct Rtc
{
	int	sec;
	int	min;
	int	hour;
	int	mday;
	int	wday;
	int	mon;
	int	year;
};

enum{
	Qdir = 0,
	Qrtc,
};

Dirtab rtcdir[]={
	".",	{Qdir, 0, QTDIR},	0,	0555,
	"rtc",	{Qrtc, 0},		0,	0664,
};

static ulong rtc2sec(Rtc*);
static void sec2rtc(ulong, Rtc*);
static I2Cdev *i2c;

static Chan*
rtcattach(char* spec)
{
	i2c = i2cdev(i2cbus("i2c3"), 0x68);
	if(i2c == nil)
		error(Enonexist);

	i2c->subaddr = 1;
	i2c->size = 0x14;

	return devattach('r', spec);
}

static Walkqid*	 
rtcwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, rtcdir, nelem(rtcdir), devgen);
}

static int	 
rtcstat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, rtcdir, nelem(rtcdir), devgen);
}

static Chan*
rtcopen(Chan* c, int omode)
{
	omode = openmode(omode);
	switch((ulong)c->qid.path){
	case Qrtc:
		if(strcmp(up->user, eve)!=0 && omode!=OREAD)
			error(Eperm);
		break;
	}
	return devopen(c, omode, rtcdir, nelem(rtcdir), devgen);
}

static void	 
rtcclose(Chan*)
{
}

#define GETBCD(o) (((bcdclock[o]&0xf)%10) + 10*((bcdclock[o]>>4)%10))

static long	 
_rtctime(void)
{
	uchar bcdclock[Nbcd];
	Rtc rtc;

	/* read clock values */
	i2crecv(i2c, bcdclock, Nbcd, Seconds);

	/*
	 *  convert from BCD
	 */
	rtc.sec = GETBCD(Seconds-Seconds) % 60;
	rtc.min = GETBCD(Minutes-Seconds) % 60;
	rtc.hour = GETBCD(Hours-Seconds) % 24;
	rtc.mday = GETBCD(Mday-Seconds);
	rtc.wday = GETBCD(Wday-Seconds) % 7;
	rtc.mon = GETBCD(Month-Seconds);
	rtc.year = GETBCD(Year-Seconds) % 100;

	/*
	 *  the world starts jan 1 1970
	 */
	if(rtc.year < 70)
		rtc.year += 2000;
	else
		rtc.year += 1900;
	return rtc2sec(&rtc);
}

long
rtctime(void)
{
	int i;
	long t, ot;

	/* loop till we get two reads in a row the same */
	t = _rtctime();
	for(i = 0; i < 100; i++){
		ot = t;
		t = _rtctime();
		if(ot == t)
			break;
	}
	if(i == 100) print("we are boofheads\n");

	return t;
}

static long	 
rtcread(Chan* c, void* buf, long n, vlong off)
{
	ulong offset = off;

	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, rtcdir, nelem(rtcdir), devgen);

	switch((ulong)c->qid.path){
	case Qrtc:
		return readnum(offset, buf, n, rtctime(), 12);
	}
	error(Ebadarg);
}

#define PUTBCD(n,o) bcdclock[o] = (n % 10) | (((n / 10) % 10)<<4)

static long	 
rtcwrite(Chan* c, void* buf, long n, vlong off)
{
	Rtc rtc;
	ulong secs;
	char *cp, sbuf[32];
	uchar bcdclock[Nbcd];
	ulong offset = off;

	if(offset!=0)
		error(Ebadarg);


	switch((ulong)c->qid.path){
	case Qrtc:
		if(n >= sizeof(sbuf))
			error(Ebadarg);
		strncpy(sbuf, buf, n);
		sbuf[n] = '\0';
		for(cp = sbuf; *cp != '\0'; cp++)
			if(*cp >= '0' && *cp <= '9')
				break;
		secs = strtoul(cp, 0, 0);
	
		/*
		 *  convert to bcd
		 */
		sec2rtc(secs, &rtc);
		
		PUTBCD(rtc.sec, Seconds-Seconds);
		PUTBCD(rtc.min, Minutes-Seconds);
		PUTBCD(rtc.hour, Hours-Seconds);
		PUTBCD(rtc.mday, Mday-Seconds);
		PUTBCD(rtc.wday, Wday-Seconds);
		PUTBCD(rtc.mon, Month-Seconds);
		PUTBCD(rtc.year, Year-Seconds);

		/*
		 *  write the clock
		 */
		i2csend(i2c, bcdclock, Nbcd, Seconds);

		return n;
	}
	error(Ebadarg);
}

Dev rtcdevtab = {
	'r',
	"rtc",

	devreset,
	devinit,
	devshutdown,
	rtcattach,
	rtcwalk,
	rtcstat,
	rtcopen,
	devcreate,
	rtcclose,
	rtcread,
	devbread,
	rtcwrite,
	devbwrite,
	devremove,
	devwstat,
};

#define SEC2MIN 60L
#define SEC2HOUR (60L*SEC2MIN)
#define SEC2DAY (24L*SEC2HOUR)

/*
 *  days per month plus days/year
 */
static	int	dmsize[] =
{
	365, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static	int	ldmsize[] =
{
	366, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 *  return the days/month for the given year
 */
static int*
yrsize(int y)
{
	if((y%4) == 0 && ((y%100) != 0 || (y%400) == 0))
		return ldmsize;
	else
		return dmsize;
}

/*
 *  compute seconds since Jan 1 1970
 */
static ulong
rtc2sec(Rtc *rtc)
{
	ulong secs;
	int i;
	int *d2m;

	secs = 0;

	/*
	 *  seconds per year
	 */
	for(i = 1970; i < rtc->year; i++){
		d2m = yrsize(i);
		secs += d2m[0] * SEC2DAY;
	}

	/*
	 *  seconds per month
	 */
	d2m = yrsize(rtc->year);
	for(i = 1; i < rtc->mon; i++)
		secs += d2m[i] * SEC2DAY;

	secs += (rtc->mday-1) * SEC2DAY;
	secs += rtc->hour * SEC2HOUR;
	secs += rtc->min * SEC2MIN;
	secs += rtc->sec;

	return secs;
}

/*
 *  compute rtc from seconds since Jan 1 1970
 */
static void
sec2rtc(ulong secs, Rtc *rtc)
{
	int d;
	long hms, day;
	int *d2m;

	/*
	 * break initial number into days
	 */
	hms = secs % SEC2DAY;
	day = secs / SEC2DAY;
	if(hms < 0) {
		hms += SEC2DAY;
		day -= 1;
	}

	/*
	 * 19700101 was thursday
	 */
	rtc->wday = (day + 7340036L) % 7;

	/*
	 * generate hours:minutes:seconds
	 */
	rtc->sec = hms % 60;
	d = hms / 60;
	rtc->min = d % 60;
	d /= 60;
	rtc->hour = d;

	/*
	 * year number
	 */
	if(day >= 0)
		for(d = 1970; day >= *yrsize(d); d++)
			day -= *yrsize(d);
	else
		for (d = 1970; day < 0; d--)
			day += *yrsize(d-1);
	rtc->year = d;

	/*
	 * generate month
	 */
	d2m = yrsize(rtc->year);
	for(d = 1; day >= d2m[d]; d++)
		day -= d2m[d];
	rtc->mday = day + 1;
	rtc->mon = d;

	return;
}
