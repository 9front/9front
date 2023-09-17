#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "ip.h"

static void	netdevbind(Ipifc *ifc, int argc, char **argv);
static void	netdevunbind(Ipifc *ifc);
static void	netdevbwrite(Ipifc *ifc, Block *bp, int version, uchar *ip, Routehint*);
static void	netdevread(void *a);

typedef struct	Netdevrock Netdevrock;
struct Netdevrock
{
	Fs	*f;		/* file system we belong to */
	Proc	*readp;		/* reading process */
	Chan	*mchan;		/* Data channel */
};

Medium netdevmedium =
{
.name=		"netdev",
.hsize=		0,
.mintu=	0,
.maxtu=	64000,
.maclen=	0,
.bind=		netdevbind,
.unbind=	netdevunbind,
.bwrite=	netdevbwrite,
.unbindonclose=	0,
};

/*
 *  called to bind an IP ifc to a generic network device
 */
static void
netdevbind(Ipifc *ifc, int argc, char **argv)
{
	Chan *mchan;
	Netdevrock *er;

	if(argc < 2)
		error(Ebadarg);

	mchan = namec(argv[2], Aopen, ORDWR, 0);

	er = smalloc(sizeof(*er));
	er->readp = (void*)-1;
	er->mchan = mchan;
	er->f = ifc->conv->p->f;

	ifc->arg = er;

	kproc("netdevread", netdevread, ifc);
}

static void
netdevunbind(Ipifc *ifc)
{
	Netdevrock *er = ifc->arg;

	while(waserror())
		;
	/* wait for reader to start */
	while(er->readp == (void*)-1)
		tsleep(&up->sleep, return0, 0, 300);

	if(er->readp != nil && er->readp != up)
		postnote(er->readp, 1, "unbind", 0);
	poperror();

	while(waserror())
		;
	/* wait for reader to die */
	while(er->readp != nil && er->readp != up)
		tsleep(&up->sleep, return0, 0, 300);
	poperror();

	if(er->mchan != nil)
		cclose(er->mchan);

	free(er);
}

/*
 *  called by ipoput with a single block to write
 */
static void
netdevbwrite(Ipifc *ifc, Block *bp, int, uchar*, Routehint*)
{
	Netdevrock *er = ifc->arg;

	devtab[er->mchan->type]->bwrite(er->mchan, bp, 0);
	ifc->out++;
}

/*
 *  process to read from the device
 */
static void
netdevread(void *a)
{
	Ipifc *ifc;
	Block *bp;
	Netdevrock *er;

	ifc = a;
	er = ifc->arg;
	er->readp = up;	/* hide identity under a rock for unbind */
	if(!waserror())
	for(;;){
		bp = devtab[er->mchan->type]->bread(er->mchan, ifc->maxtu, 0);
		if(bp == nil){
			poperror();
			break;
		}
		rlock(ifc);
		if(waserror()){
			runlock(ifc);
			nexterror();
		}
		ifc->in++;
		if(ifc->lifc == nil)
			freeb(bp);
		else
			ipiput4(er->f, ifc, bp);
		runlock(ifc);
		poperror();
	}
	if(mediumunbindifc(ifc) != nil)
		er->readp = nil;	/* someone else is doing the unbind */
	pexit("hangup", 1);
}

void
netdevmediumlink(void)
{
	addipmedium(&netdevmedium);
}
