#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "ip.h"

static void
readloopback(void *x)
{
	Ipifc *ifc = (Ipifc*)x;
	Fs *f = ifc->conv->p->f;
	Block *bp;

	while(waserror())
		;
	while((bp = qbread(ifc->loopback, IP_MAX)) != nil){
		rlock(ifc);
		if(waserror()){
			runlock(ifc);
			continue;
		}
		ipiput4(f, ifc, bp);
		runlock(ifc);
		poperror();
	}
	pexit("hangup", 1);
}

static void
loopbackbind(Ipifc *ifc, int, char**)
{
	if(ifc->loopback != nil)
		return;

	ifc->loopback = qopen(1024*1024, Qmsg, 0, 0);
	if(ifc->loopback == nil)
		error(Enomem);
	kproc("loopbackread", readloopback, ifc);

}

static void
loopbackbwrite(Ipifc *ifc, Block *bp, int, uchar*, Routehint*)
{
	qbwrite(ifc->loopback, bp);
}

Medium loopbackmedium =
{
.hsize=		0,
.mintu=		0,
.maxtu=		IP_MAX,
.maclen=	0,
.name=		"loopback",
.bind=		loopbackbind,
.bwrite=	loopbackbwrite,
};

void
loopbackmediumlink(void)
{
	addipmedium(&loopbackmedium);
}
