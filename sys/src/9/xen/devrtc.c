/*
 * Xen wall clock
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

enum{
	Qdir = 0,
	Qrtc,
};

Dirtab rtcdir[]={
	".",	{Qdir, 0, QTDIR},	0,	0555,
	"rtc",		{Qrtc, 0},	0,	0664,
};

static long
rtcread(Chan *c, void *a, long n, vlong offset)
{
	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, rtcdir, nelem(rtcdir), devgen);

	switch((ulong)c->qid.path){
	case Qrtc:
		return readnum((ulong)offset, a, n, xenwallclock(), 12);
	}
	error(Ebadarg);
}

static long
rtcwrite(Chan*c, void*, long n, vlong)
{
	switch((ulong)c->qid.path){
	case Qrtc:
		return n;
	}
	error(Eperm);
}

static Chan*
rtcattach(char* spec)
{
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
	return devopen(c, omode, rtcdir, nelem(rtcdir), devgen);
}

static void	 
rtcclose(Chan*)
{
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

