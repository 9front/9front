/*
 *  PL031 RTC driver
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

static u32int *regs = (u32int *)(VIRTIO + 0x01010000);

Dirtab rtcdir[]={
	".",	{Qdir, 0, QTDIR},	0,	0555,
	"rtc",	{Qrtc, 0},		0,	0664,
};

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

long	 
rtctime(void)
{
	return *regs;
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

static long	 
rtcwrite(Chan*, void*, long, vlong)
{
	error(Eperm);
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
