/*
 * USB device driver framework.
 *
 * This is in charge of providing access to actual HCIs
 * and providing I/O to the various endpoints of devices.
 * A separate user program (usbd) is in charge of
 * enumerating the bus, setting up endpoints and
 * starting devices (also user programs).
 *
 * The interface provided is a violation of the standard:
 * you're welcome.
 *
 * The interface consists of a root directory with several files
 * plus a directory (epN.M) with two files per endpoint.
 * A device is represented by its first endpoint, which
 * is a control endpoint automatically allocated for each device.
 * Device control endpoints may be used to create new endpoints.
 * Devices corresponding to hubs may also allocate new devices,
 * perhaps also hubs. Initially, a hub device is allocated for
 * each controller present, to represent its root hub. Those can
 * never be removed.
 *
 * All endpoints refer to the first endpoint (epN.0) of the device,
 * which keeps per-device information, and also to the HCI used
 * to reach them. Although all endpoints cache that information.
 *
 * epN.M/data files permit I/O and are considered DMEXCL.
 * epN.M/ctl files provide status info and accept control requests.
 *
 * Endpoints may be given file names to be listed also at #u,
 * for those drivers that have nothing to do after configuring the
 * device and its endpoints.
 *
 * Drivers for different controllers are kept at usb[oue]hci.c
 * It's likely we could factor out much from controllers into
 * a generic controller driver, the problem is that details
 * regarding how to handle toggles, tokens, Tds, etc. will
 * get in the way. Thus, code is probably easier the way it is.
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/usb.h"

typedef struct Hcitype Hcitype;

enum
{
	/* Qid numbers */
	Qdir = 0,		/* #u */
	Qusbdir,			/* #u/usb */
	Qctl,			/* #u/usb/ctl - control requests */

	Qep0dir,			/* #u/usb/ep0.0 - endpoint 0 dir */
	Qep0io,			/* #u/usb/ep0.0/data - endpoint 0 I/O */
	Qep0ctl,		/* #u/usb/ep0.0/ctl - endpoint 0 ctl. */
	Qep0dummy,		/* give 4 qids to each endpoint */

	Qepdir = 0,		/* (qid-qep0dir)&3 is one of these */
	Qepio,			/* to identify which file for the endpoint */
	Qepctl,

	/* ... */

	/* Usb ctls. */
	CMdebug = 0,		/* debug on|off */

	/* Ep. ctls */
	CMnew = 0,		/* new nb ctl|bulk|intr|iso r|w|rw (endpoint) */
	CMnewdev,		/* newdev full|low|high|super portnb (allocate new devices) */
	CMdetach,		/* detach (abort I/O forever on this ep). */
	CMpreset,		/* reset the port */
	CMdebugep,		/* debug n (set/clear debug for this ep) */
	CMclrhalt,		/* clrhalt (halt was cleared on endpoint) */
	CMaddress,		/* address (address is assigned) */
	CMhub,			/* hub (set the device as a hub) */
	CMmaxpkt,		/* maxpkt size */
	CMntds,			/* ntds nb (max nb. of tds per µframe) */
	CMpollival,		/* pollival interval (interrupt/iso) */
	CMtmout,		/* timeout n (activate timeouts for ep) */
	CMhz,			/* hz n (samples/sec; iso) */
	CMsamplesz,		/* samplesz n (sample size; iso) */
	CMsampledelay,		/* maximum delay introduced by buffering (iso) */
	CMuframes,		/* set uframe mode (iso) */
	CMname,			/* name str (show up as #u/name as well) */
	CMinfo,			/* info infostr (ke.ep info for humans) */

	/* Hub feature selectors */
	Rportenable	= 1,
	Rportreset	= 4,
	Rportpower	= 8,
	Rbhportreset	= 28,

};

struct Hcitype
{
	char*	type;
	int	(*reset)(Hci*);
};

#define QID(q)	((int)(q).path)

static char Edetach[] = "device is detached";
static char Edisabled[] = "device is not enabled";
static char Enotconf[] = "endpoint not configured";
static char Ebadtype[] = "bad endpoint type";
static char Ebadport[] = "bad hub port number";
static char Enotahub[] = "not a hub";

char Estalled[] = "endpoint stalled";

static Cmdtab usbctls[] =
{
	{CMdebug,	"debug",	2},
};

static Cmdtab epctls[] =
{
	{CMnew,		"new",		4},
	{CMnewdev,	"newdev",	3},
	{CMdetach,	"detach",	1},
	{CMpreset,	"reset",	1},
	{CMdebugep,	"debug",	2},
	{CMclrhalt,	"clrhalt",	1},
	{CMaddress,	"address",	1},
	{CMhub,		"hub",		0},
	{CMmaxpkt,	"maxpkt",	2},
	{CMntds,	"ntds",		2},
	{CMpollival,	"pollival",	2},
	{CMtmout,	"timeout",	2},
	{CMhz,		"hz",		2},
	{CMsamplesz,	"samplesz",	2},
	{CMsampledelay,	"sampledelay",	2},
	{CMuframes,	"uframes",	2},
	{CMname,	"name",		2},
	{CMinfo,	"info",		0},
};

static Dirtab usbdir[] =
{
	"ctl",		{Qctl},		0,	0666,
};

char *usbmodename[] =
{
	[OREAD]	"r",
	[OWRITE]	"w",
	[ORDWR]	"rw",
};

static char *ttname[] =
{
	[Tnone]	"none",
	[Tctl]	"control",
	[Tiso]	"iso",
	[Tintr]	"interrupt",
	[Tbulk]	"bulk",
};

static char *spname[] =
{
	[Superspeed]	"super",
	[Fullspeed]	"full",
	[Lowspeed]	"low",
	[Highspeed]	"high",
	[Nospeed]	"no",
};

static int	debug;
static Hcitype	hcitypes[Nhcis];
static Hci*	hcis[Nhcis];
static QLock	epslck;		/* add, del, lookup endpoints */
static Ep*	eps[Neps];	/* all endpoints known */
static int	epmax;		/* 1 + last endpoint index used  */

/*
 * Is there something like this in a library? should it be?
 */
char*
seprintdata(char *s, char *se, uchar *d, int n)
{
	int i, l;

	s = seprint(s, se, " %#p[%d]: ", d, n);
	l = n;
	if(l > 10)
		l = 10;
	for(i=0; i<l; i++)
		s = seprint(s, se, " %2.2ux", d[i]);
	if(l < n)
		s = seprint(s, se, "...");
	return s;
}

static int
name2speed(char *name)
{
	int i;

	for(i = 0; i < nelem(spname); i++)
		if(strcmp(name, spname[i]) == 0)
			return i;
	return Nospeed;
}

static int
name2ttype(char *name)
{
	int i;

	for(i = 0; i < nelem(ttname); i++)
		if(strcmp(name, ttname[i]) == 0)
			return i;
	/* may be a std. USB ep. type */
	i = strtol(name, nil, 0);
	switch(i+1){
	case Tctl:
	case Tiso:
	case Tbulk:
	case Tintr:
		return i+1;
	default:
		return Tnone;
	}
}

static int
name2mode(char *mode)
{
	int i;

	for(i = 0; i < nelem(usbmodename); i++)
		if(strcmp(mode, usbmodename[i]) == 0)
			return i;
	return -1;
}

static int
qid2epidx(int q)
{
	q = (q-Qep0dir)/4;
	if(q < 0 || q >= epmax || eps[q] == nil)
		return -1;
	return q;
}

static int
isqtype(int q, int type)
{
	if(q < Qep0dir)
		return 0;
	q -= Qep0dir;
	return (q & 3) == type;
}

void
addhcitype(char* t, int (*r)(Hci*))
{
	static int ntype;

	if(ntype == Nhcis)
		panic("too many USB host interface types");
	hcitypes[ntype].type = t;
	hcitypes[ntype].reset = r;
	ntype++;
}

static int
rootport(Ep *ep, int port)
{
	Hci *hp;
	Udev *hub;
	uint mask;
	int rootport;

	hp = ep->hp;
	hub = ep->dev;
	if(hub->depth >= 0)
		return hub->rootport;

	mask = hp->superspeed;
	if(hub->speed != Superspeed)
		mask = (1<<hp->nports)-1 & ~mask;

	for(rootport = 1; mask != 0; rootport++){
		if(mask & 1){
			if(--port == 0)
				return rootport;
		}
		mask >>= 1;
	}

	return 0;
}

static char*
seprintep(char *s, char *se, Ep *ep, int all)
{
	static char* dsnames[] = { "config", "enabled", "detached", "reset" };
	Udev *d;
	int i;
	int di;

	d = ep->dev;

	eqlock(ep);
	if(waserror()){
		qunlock(ep);
		nexterror();
	}
	di = ep->dev->nb;
	if(all)
		s = seprint(s, se, "dev %d ep %d ", di, ep->nb);
	s = seprint(s, se, "%s", dsnames[ep->dev->state]);
	s = seprint(s, se, " %s", ttname[ep->ttype]);
	assert(ep->mode == OREAD || ep->mode == OWRITE || ep->mode == ORDWR);
	s = seprint(s, se, " %s", usbmodename[ep->mode]);
	s = seprint(s, se, " speed %s", spname[d->speed]);
	s = seprint(s, se, " maxpkt %ld", ep->maxpkt);
	s = seprint(s, se, " ntds %d", ep->ntds);
	s = seprint(s, se, " pollival %ld", ep->pollival);
	s = seprint(s, se, " samplesz %ld", ep->samplesz);
	s = seprint(s, se, " hz %ld", ep->hz);
	s = seprint(s, se, " uframes %d", ep->uframes);
	s = seprint(s, se, " hub %d", ep->dev->hub? ep->dev->hub->nb: 0);
	s = seprint(s, se, " port %d", ep->dev->port);
	s = seprint(s, se, " rootport %d", ep->dev->rootport);
	s = seprint(s, se, " addr %d", ep->dev->addr);
	if(ep->inuse)
		s = seprint(s, se, " busy");
	else
		s = seprint(s, se, " idle");
	if(all){
		s = seprint(s, se, " load %uld", ep->load);
		s = seprint(s, se, " ref %ld addr %#p", ep->ref, ep);
		s = seprint(s, se, " idx %d", ep->idx);
		if(ep->name != nil)
			s = seprint(s, se, " name '%s'", ep->name);
		if(ep->tmout != 0)
			s = seprint(s, se, " tmout");
		if(ep == ep->ep0){
			s = seprint(s, se, " ctlrno %#x", ep->hp->ctlrno);
			s = seprint(s, se, " eps:");
			for(i = 0; i < nelem(d->eps); i++)
				if(d->eps[i] != nil)
					s = seprint(s, se, " ep%d.%d", di, i);
		}
	}
	if(ep->info != nil)
		s = seprint(s, se, "\n%s %s\n", ep->info, ep->hp->type);
	else
		s = seprint(s, se, "\n");
	qunlock(ep);
	poperror();
	return s;
}

static Ep*
epalloc(Hci *hp)
{
	Ep *ep;
	int i;

	for(i = 0; i < Neps; i++)
		if(eps[i] == nil)
			break;
	if(i == Neps)
		error("out of endpoints");

	ep = malloc(sizeof(Ep));
	if(ep == nil)
		error(Enomem);
	ep->ref = 1;
	ep->idx = i;
	if(epmax <= i)
		epmax = i+1;
	eps[i] = ep;
	ep->hp = hp;
	ep->maxpkt = 8;
	ep->ntds = 1;
	ep->uframes = ep->samplesz = ep->pollival = ep->hz = 0; /* make them void */
	ep->toggle[0] = ep->toggle[1] = 0;
	return ep;
}

static Ep*
getep(int i)
{
	Ep *ep;

	if(i < 0 || i >= epmax || eps[i] == nil)
		return nil;
	qlock(&epslck);
	ep = eps[i];
	if(ep != nil && incref(ep) == 1){
		/* race with putep() below, its dead. */
		decref(ep);
		ep = nil;
	}
	qunlock(&epslck);
	return ep;
}

static void
putep(Ep *ep)
{
	Udev *d;
	Ep *next;

	for(; ep != nil; ep = next){
		if(decref(ep) > 0)
			return;
		assert(ep->inuse == 0);
		d = ep->dev;
		deprint("usb: ep%d.%d %#p released\n", d->nb, ep->nb, ep);
		qlock(ep->ep0);
		qlock(&epslck);
		assert(d->eps[ep->nb] == ep);
		d->eps[ep->nb] = nil;
		assert(eps[ep->idx] == ep);
		eps[ep->idx] = nil;
		if(ep->idx == epmax-1){
			while(epmax > 0 && eps[epmax-1] == nil)
				epmax--;
		}
		qunlock(&epslck);
		qunlock(ep->ep0);
		if(ep->ep0 != ep)
			next = ep->ep0;
		else {
			next = d->hub != nil? d->hub->eps[0]: nil;
			if(ep->hp->devclose != nil)
				(*ep->hp->devclose)(d);
			free(d);
		}
		free(ep->info);
		free(ep->name);
		free(ep);
	}
}

static int
newusbid(Hci *)
{
	Ep *ep;
	Udev *d;
	int i, id;
	char map[128];

	memset(map, 0, sizeof(map));
	map[0] = 1;	/* dont use */

	id = 1;
	for(i = 0; i < epmax; i++) {
		if((ep = eps[i]) == nil)
			continue;

		d = ep->dev;
		map[d->nb % nelem(map)] = 1;

		/* try use largest id + 1 */
		if(d->nb >= id)
			id = d->nb + 1;
	}

	/* once we ran out, use lowest possible free id */
	if(id >= nelem(map)){
		for(i = 0; i < nelem(map); i++)
			if(map[i] == 0)
				return i;
	}

	return id;
}

/*
 * Create endpoint 0 for a new device on hub.
 * hub must be locked.
 */
static Ep*
newdev(Hci *hp, Ep *hub, int port, int speed)
{
	Ep *ep;
	Udev *d;

	d = malloc(sizeof(Udev));
	if(d == nil)
		error(Enomem);
	if(waserror()){
		free(d);
		nexterror();
	}

	d->state = Dconfig;		/* address not yet set */
	d->addr = 0;
	d->port = port;
	d->speed = speed;

	d->nports = 0;
	d->rootport = d->routestr = 0;
	d->ttport = d->ttt = d->mtt = 0;
	d->tthub = nil;

	d->rhrepl = -1;
	d->rhresetport = 0;

	if(hub != nil){
		d->hub = hub->dev;
		d->depth = d->hub->depth+1;
		if(d->depth > 0){
			assert(d->depth <= 5);
			d->routestr = d->hub->routestr | (d->port<15? d->port: 15) << 4*(d->depth-1);
			if(speed < Highspeed){
				if(d->hub->speed == Highspeed){
					d->tthub = d->hub;
					d->ttport = port;
				}else {
					d->tthub = d->hub->tthub;
					d->ttport = d->hub->ttport;
				}
			}
		}
		d->rootport = rootport(hub, port);
		if(d->rootport <= 0)
			error(Ebadport);
	} else {
		d->hub = nil;
		d->depth = -1;
	}

	eqlock(&epslck);
	if(waserror()){
		qunlock(&epslck);
		nexterror();
	}
	d->nb = newusbid(hp);

	ep = epalloc(hp);
	ep->mode = ORDWR;
	ep->ttype = Tctl;
	ep->tmout = Xfertmout;

	if(speed == Superspeed)
		ep->maxpkt = 512;
	else if(speed != Lowspeed)
		ep->maxpkt = 64;	/* assume full speed */
	else
		ep->maxpkt = 8;

	ep->nb = 0;
	ep->ep0 = ep;			/* no ref counted here */
	ep->dev = d;
	d->eps[0] = ep;
	if(hub != nil)
		incref(hub);
	incref(ep);
	qunlock(&epslck);
	poperror();
	poperror();

	dprint("newdev %#p ep%d.%d %#p\n", d, d->nb, ep->nb, ep);
	return ep;
}

/*
 * Create a new endpoint for the device
 * accessed via the given endpoint 0.
 * ep0 must be locked.
 */
static Ep*
newdevep(Ep *ep0, int nb, int tt, int mode)
{
	Ep *ep;
	Udev *d;

	eqlock(&epslck);
	if(waserror()){
		qunlock(&epslck);
		nexterror();
	}
	d = ep0->dev;
	if(d->eps[nb] != nil)
		error(Einuse);
	ep = epalloc(ep0->hp);
	ep->mode = mode;
	ep->ttype = tt;
	/* set defaults */
	switch(tt){
	case Tctl:
		ep->tmout = Xfertmout;
		break;
	case Tintr:
		ep->pollival = 10;
		break;
	case Tiso:
		ep->tmout = Xfertmout;
		ep->pollival = 10;
		ep->samplesz = 1;
		break;
	}
	ep->nb = nb;
	ep->ep0 = ep0;
	ep->dev = d;
	d->eps[nb] = ep;
	incref(ep0);
	incref(ep);
	qunlock(&epslck);
	poperror();

	deprint("newdevep ep%d.%d %#p\n", d->nb, ep->nb, ep);
	return ep;
}

static int
epdataperm(int mode)
{
	switch(mode){
	case OREAD:
		return 0440|DMEXCL;
		break;
	case OWRITE:
		return 0220|DMEXCL;
		break;
	default:
		return 0660|DMEXCL;
	}
}

static int
usbgen(Chan *c, char *, Dirtab*, int, int s, Dir *dp)
{
	Qid q;
	Dirtab *dir;
	int perm;
	Ep *ep;
	int nb;
	int mode;

	if(0)ddprint("usbgen q %#x s %d...", QID(c->qid), s);
	if(s == DEVDOTDOT){
		if(QID(c->qid) <= Qusbdir){
			mkqid(&q, Qdir, 0, QTDIR);
			devdir(c, q, "#u", 0, eve, 0555, dp);
		}else{
			mkqid(&q, Qusbdir, 0, QTDIR);
			devdir(c, q, "usb", 0, eve, 0555, dp);
		}
		if(0)ddprint("ok\n");
		return 1;
	}

	switch(QID(c->qid)){
	case Qdir:				/* list #u */
		if(s == 0){
			mkqid(&q, Qusbdir, 0, QTDIR);
			devdir(c, q, "usb", 0, eve, 0555, dp);
			if(0)ddprint("ok\n");
			return 1;
		}
		s--;
		if(s < 0 || s >= epmax)
			goto Fail;
		ep = getep(s);
		if(ep == nil || ep->name == nil){
			putep(ep);
			if(0)ddprint("skip\n");
			return 0;
		}
		if(waserror()){
			putep(ep);
			nexterror();
		}
		mkqid(&q, Qep0io+s*4, 0, QTFILE);
		snprint(up->genbuf, sizeof(up->genbuf), "%s", ep->name);
		devdir(c, q, up->genbuf, 0, eve, epdataperm(ep->mode), dp);
		putep(ep);
		poperror();
		if(0)ddprint("ok\n");
		return 1;

	case Qusbdir:				/* list #u/usb */
	Usbdir:
		if(s < nelem(usbdir)){
			dir = &usbdir[s];
			mkqid(&q, dir->qid.path, 0, QTFILE);
			devdir(c, q, dir->name, dir->length, eve, dir->perm, dp);
			if(0)ddprint("ok\n");
			return 1;
		}
		s -= nelem(usbdir);
		if(s < 0 || s >= epmax)
			goto Fail;
		ep = getep(s);
		if(ep == nil){
			if(0)ddprint("skip\n");
			return 0;
		}
		if(waserror()){
			putep(ep);
			nexterror();
		}
		snprint(up->genbuf, sizeof(up->genbuf), "ep%d.%d", ep->dev->nb, ep->nb);
		putep(ep);
		poperror();
		mkqid(&q, Qep0dir+4*s, 0, QTDIR);
		devdir(c, q, up->genbuf, 0, eve, 0775, dp);
		if(0)ddprint("ok\n");
		return 1;

	case Qctl:
		s = 0;
		goto Usbdir;

	default:				/* list #u/usb/epN.M */
		nb = qid2epidx(QID(c->qid));
		ep = getep(nb);
		if(ep == nil)
			goto Fail;
		mode = ep->mode;
		putep(ep);
		if(isqtype(QID(c->qid), Qepdir)){
		Epdir:
			switch(s){
			case 0:
				mkqid(&q, Qep0io+nb*4, 0, QTFILE);
				perm = epdataperm(mode);
				devdir(c, q, "data", 0, eve, perm, dp);
				break;
			case 1:
				mkqid(&q, Qep0ctl+nb*4, 0, QTFILE);
				devdir(c, q, "ctl", 0, eve, 0664, dp);
				break;
			default:
				goto Fail;
			}
		}else if(isqtype(QID(c->qid), Qepctl)){
			s = 1;
			goto Epdir;
		}else{
			s = 0;
			goto Epdir;
		}
		if(0)ddprint("ok\n");
		return 1;
	}
Fail:
	if(0)ddprint("fail\n");
	return -1;
}

static Hci*
hciprobe(int cardno, int ctlrno)
{
	Hci *hp;
	char *type;

	ddprint("hciprobe %d %d\n", cardno, ctlrno);
	hp = malloc(sizeof(Hci));
	if(hp == nil){
		print("hciprobe: out of memory\n");
		return nil;
	}

	hp->ctlrno = ctlrno;
	hp->tbdf = BUSUNKNOWN;

	if(cardno < 0){
		if(isaconfig("usb", ctlrno, hp) == 0){
			free(hp);
			return nil;
		}
		for(cardno = 0; cardno < Nhcis; cardno++){
			if(hcitypes[cardno].type == nil)
				break;
			type = hp->type;
			if(type==nil || *type==0)
				type = "uhci";
			if(cistrcmp(hcitypes[cardno].type, type) == 0)
				break;
		}
	}

	if(cardno >= Nhcis || hcitypes[cardno].type == nil){
		free(hp);
		return nil;
	}
	dprint("%s...", hcitypes[cardno].type);
	if((*hcitypes[cardno].reset)(hp) < 0){
		free(hp);
		return nil;
	}
	return hp;
}

static void
usbreset(void)
{
	int cardno, ctlrno;
	Hci *hp;

	if(getconf("*nousbprobe"))
		return;
	dprint("usbreset\n");

	for(ctlrno = 0; ctlrno < Nhcis; ctlrno++)
		if((hp = hciprobe(-1, ctlrno)) != nil)
			hcis[ctlrno] = hp;
	cardno = ctlrno = 0;
	while(cardno < Nhcis && ctlrno < Nhcis && hcitypes[cardno].type != nil)
		if(hcis[ctlrno] != nil)
			ctlrno++;
		else{
			hp = hciprobe(cardno, ctlrno);
			if(hp == nil)
				cardno++;
			hcis[ctlrno++] = hp;
		}
	if(hcis[Nhcis-1] != nil)
		print("usbreset: bug: Nhcis (%d) too small\n", Nhcis);
}

static int
numbits(uint n)
{
	int c = 0;
	while(n != 0){
		c++;
		n = (n-1) & n;
	}
	return c;
}

static void
usbinit(void)
{
	Hci *hp;
	Ep *d;
	int ctlrno, n;

	dprint("usbinit\n");
	for(ctlrno = 0; ctlrno < Nhcis; ctlrno++){
		hp = hcis[ctlrno];
		if(hp == nil)
			continue;

		if(hp->init != nil){
			if(waserror()){
				print("usbinit: %s %d: %s\n", hp->type, ctlrno, up->errstr);
				continue;
			}
			(*hp->init)(hp);
			poperror();
		}

		hp->superspeed &= (1<<hp->nports)-1;
		n = hp->nports - numbits(hp->superspeed);
		if(n > 0){
			d = newdev(hp, nil, 0, hp->highspeed?Highspeed:Fullspeed);		/* new LS/FS/HS root hub */
			d->dev->nports = n;
			d->dev->state = Denabled;	/* although addr == 0 */
			snprint(up->genbuf, sizeof(up->genbuf), "roothub ports %d", n);
			kstrdup(&d->info, up->genbuf);
			putep(d);
		}
		n = numbits(hp->superspeed);
		if(n > 0){
			d = newdev(hp, nil, 0, Superspeed);	/* new SS root hub */
			d->dev->nports = n;
			d->dev->state = Denabled;	/* although addr == 0 */
			snprint(up->genbuf, sizeof(up->genbuf), "roothub ports %d", n);
			kstrdup(&d->info, up->genbuf);
			putep(d);
		}
	}
}

static Chan*
usbattach(char *spec)
{
	return devattach(L'u', spec);
}

static Walkqid*
usbwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, usbgen);
}

static int
usbstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, usbgen);
}

/*
 * µs for the given transfer, for bandwidth allocation.
 * This is a very rough worst case for what 5.11.3
 * of the usb 2.0 spec says.
 * Also, we are using maxpkt and not actual transfer sizes.
 * Only when we are sure we
 * are not exceeding b/w might we consider adjusting it.
 */
static ulong
usbload(int speed, int maxpkt)
{
	enum{ Hostns = 1000, Hubns = 333 };
	ulong l;
	ulong bs;

	l = 0;
	bs = 10UL * maxpkt;
	switch(speed){
	case Highspeed:
		l = 55*8*2 + 2 * (3 + bs) + Hostns;
		break;
	case Fullspeed:
		l = 9107 + 84 * (4 + bs) + Hostns;
		break;
	case Lowspeed:
		l = 64107 + 2 * Hubns + 667 * (3 + bs) + Hostns;
		break;
	default:
		print("usbload: bad speed %d\n", speed);
		/* let it run */
	}
	return l / 1000UL;	/* in µs */
}

static void
isotiming(Ep *ep)
{
	long spp, max;

	switch(ep->dev->speed){
	case Fullspeed:
		max = 1024;
		break;
	case Highspeed:
		max = 3*1024;
		break;
	case Superspeed:
		max = 48*1024;
		break;
	default:
		error(Egreg);
	}

	if(ep->ntds <= 0)
		error(Egreg);
	max /= ep->ntds;
	if(max < ep->samplesz)
		error(Egreg);
	if(ep->pollival <= 0)
		error(Egreg);

	if(ep->hz <= 0){
		spp = ep->maxpkt / ep->samplesz;
		spp *= ep->ntds;
		if(ep->dev->speed == Fullspeed || ep->dev->speed == Lowspeed)
			ep->hz = (1000 * spp) / ep->pollival;
		else
			ep->hz = (8000 * spp) / ep->pollival;
		if(ep->hz <= 0)
			error(Egreg);
	}
	if(ep->dev->speed == Fullspeed || ep->dev->speed == Lowspeed)
		spp = (ep->hz * ep->pollival + 999) / 1000;
	else
		spp = (ep->hz * ep->pollival + 7999) / 8000;
	spp /= ep->ntds;
	ep->maxpkt = spp * ep->samplesz;
	if(ep->maxpkt > max){
		print("ep%d.%d: maxpkt %ld > %ld for %s, truncating\n",
			ep->dev->nb, ep->nb,
			ep->maxpkt, max, spname[ep->dev->speed]);
		ep->maxpkt = max;
	}
}

static Chan*
usbopen(Chan *c, int omode)
{
	int q, mode;
	Ep *ep;

	mode = openmode(omode);
	q = QID(c->qid);
	if(q >= Qep0dir && qid2epidx(q) < 0)
		error(Eio);
	if(q < Qep0dir || isqtype(q, Qepctl) || isqtype(q, Qepdir))
		return devopen(c, omode, nil, 0, usbgen);
	ep = getep(qid2epidx(q));
	if(ep == nil)
		error(Eio);
	deprint("usbopen q %#x fid %d omode %d\n", q, c->fid, mode);
	if(waserror()){
		putep(ep);
		nexterror();
	}
	eqlock(ep);
	if(waserror()){
		qunlock(ep);
		nexterror();
	}
	if(ep->ttype == Tnone)
		error(Enotconf);
	if(mode != OREAD && ep->mode == OREAD)
		error(Eperm);
	if(mode != OWRITE && ep->mode == OWRITE)
		error(Eperm);
	if(ep->inuse)
		error(Einuse);
	if(ep->dev->state == Ddetach)
		error(Edetach);
	ep->clrhalt = 0;
	if(ep->ttype == Tctl)
		ep->dev->rhrepl = -1;
	if(ep->ttype == Tiso)
		isotiming(ep);
	if(ep->load == 0 && ep->dev->speed != Superspeed)
		ep->load = usbload(ep->dev->speed, ep->maxpkt);
	(*ep->hp->epopen)(ep);
	ep->inuse = 1;
	qunlock(ep);
	poperror();	/* ep */
	poperror();	/* don't putep(): ref kept for fid using the ep. */

	c->mode = mode;
	c->flag |= COPEN;
	c->offset = 0;
	c->aux = nil;	/* paranoia */
	return c;
}

static void
usbclose(Chan *c)
{
	int q;
	Ep *ep;

	q = QID(c->qid);
	if(q < Qep0dir || isqtype(q, Qepctl) || isqtype(q, Qepdir))
		return;

	if((c->flag & COPEN) == 0)
		return;

	free(c->aux);
	c->aux = nil;
	c->flag &= ~COPEN;

	ep = getep(qid2epidx(q));
	if(ep == nil)
		return;

	deprint("usbclose q %#x fid %d ref %ld\n", q, c->fid, ep->ref);
	qlock(ep);
	if(!ep->inuse)
		qunlock(ep);
	else {
		if(!waserror()){
			if(ep->hp->epstop != nil)
				(*ep->hp->epstop)(ep);
			if(ep->hp->epclose != nil)
				(*ep->hp->epclose)(ep);
			poperror();
		}
		ep->inuse = 0;
		ep->aux = nil;
		qunlock(ep);
		putep(ep);	/* release ref from usbopen() */
	}
	putep(ep);		/* release ref of getep() above */
}

static long
ctlread(Chan *c, void *a, long n, vlong offset)
{
	int q;
	char *s;
	char *us;
	char *se;
	Ep *ep;
	int i;

	q = QID(c->qid);
	us = s = smalloc(READSTR);
	se = s + READSTR;
	if(waserror()){
		free(us);
		nexterror();
	}
	if(q == Qctl)
		for(i = 0; i < epmax; i++){
			ep = getep(i);
			if(ep != nil){
				s = seprint(s, se, "ep%d.%d ", ep->dev->nb, ep->nb);
				s = seprintep(s, se, ep, 0);
				putep(ep);
			}
		}
	else{
		ep = getep(qid2epidx(q));
		if(ep == nil)
			error(Eio);
		if(c->aux != nil){
			/* After a new endpoint request we read
			 * the new endpoint name back.
			 */
			strecpy(s, se, c->aux);
			free(c->aux);
			c->aux = nil;
		}else
			seprintep(s, se, ep, 0);
		putep(ep);
	}
	n = readstr(offset, a, n, us);
	free(us);
	poperror();
	return n;
}

/*
 * Fake root hub emulation.
 */
static long
rhubread(Ep *ep, void *a, long n)
{
	uchar b[8];
	Udev *dev;

	dev = ep->dev;

	if(dev->depth >= 0 || ep->nb != 0 || n < 2 || dev->rhrepl == -1)
		return -1;

	b[0] = dev->rhrepl;
	b[1] = dev->rhrepl>>8;
	b[2] = dev->rhrepl>>16;
	b[3] = dev->rhrepl>>24;
	b[4] = dev->rhrepl>>32;
	b[5] = dev->rhrepl>>40;
	b[6] = dev->rhrepl>>48;
	b[7] = dev->rhrepl>>56;

	dev->rhrepl = -1;

	if(n > sizeof(b))
		n = sizeof(b);
	memmove(a, b, n);

	return n;
}

static long
rhubwrite(Ep *ep, void *a, long n)
{
	uchar *s;
	int cmd;
	int feature;
	int port;
	Hci *hp;
	Udev *dev;

	hp = ep->hp;
	dev = ep->dev;

	if(dev->depth >= 0 || ep->nb != 0)
		return -1;
	if(n != Rsetuplen)
		error("root hub is a toy hub");
	s = a;
	if(s[Rtype] != (Rh2d|Rclass|Rother) && s[Rtype] != (Rd2h|Rclass|Rother))
		error("root hub is a toy hub");

	/* terminate previous port reset */
	port = dev->rhresetport;
	if(port > 0){
		dev->rhresetport = 0;

		/*
		 * Some controllers have to clear reset and set enable manually.
		 * We assume that clearing reset will transition the bus from
		 * SE0 to idle state, and setting enable starts transmitting
		 * SOF packets (keep alive).
		 *
		 * The delay between clearing reset and setting enable must
		 * not exceed 3ms as this makes devices suspend themselfs.
		 */
		if(hp->portreset != nil){
			(*hp->portreset)(hp, port, 0);
			tsleep(&up->sleep, return0, nil, 2);
		}
		if(hp->portenable != nil){
			(*hp->portenable)(hp, port, 1);
			tsleep(&up->sleep, return0, nil, 2);
		}
	}

	cmd = s[Rreq];
	feature = GET2(s+Rvalue);
	port = rootport(ep, GET2(s+Rindex));
	if(port <= 0)
		error(Ebadport);

	dev->rhrepl = 0;
	switch(feature){
	case Rportpower:
		if(hp->portpower == nil)
			break;
		(*hp->portpower)(hp, port, cmd == Rsetfeature);
		break;
	case Rportenable:
		if(cmd != Rclearfeature || hp->portenable == nil)
			break;
		(*hp->portenable)(hp, port, 0);
		break;
	case Rbhportreset:
		if(cmd != Rsetfeature || hp->bhportreset == nil)
			break;
		(*hp->bhportreset)(hp, port, 1);
		break;
	case Rportreset:
		if(cmd != Rsetfeature || hp->portreset == nil)
			break;
		(*hp->portreset)(hp, port, 1);
		/* port reset in progress */
		dev->rhresetport = port;
		break;
	case Rgetstatus:
		if(hp->portstatus == nil)
			break;
		dev->rhrepl = (*hp->portstatus)(hp, port);
		break;
	}
	return n;
}

static long
usbread(Chan *c, void *a, long n, vlong offset)
{
	int q;
	Ep *ep;
	int nr;

	q = QID(c->qid);

	if(c->qid.type == QTDIR)
		return devdirread(c, a, n, nil, 0, usbgen);

	if(q == Qctl || isqtype(q, Qepctl))
		return ctlread(c, a, n, offset);

	ep = getep(qid2epidx(q));
	if(ep == nil)
		error(Eio);
	if(waserror()){
		putep(ep);
		nexterror();
	}
	if(ep->mode == OWRITE || ep->inuse == 0)
		error(Ebadusefd);
	if(ep->dev->state == Ddetach)
		error(Edetach);

	switch(ep->ttype){
	case Tnone:
		error(Enotconf);
	case Tctl:
		nr = rhubread(ep, a, n);
		if(nr >= 0)
			break;
		/* else fall */
	default:
		ddeprint("\nusbread q %#x fid %d cnt %ld off %lld\n",q,c->fid,n,offset);
	Again:
		nr = (*ep->hp->epread)(ep, a, n);
		if(nr == 0 && ep->ttype == Tiso && ep->dev->state != Ddetach){
			tsleep(&up->sleep, return0, nil, 2*ep->pollival);
			goto Again;
		}
		break;
	}
	putep(ep);
	poperror();
	return nr;
}

/*
 * Many endpoint ctls. simply update the portable representation
 * of the endpoint. The actual controller driver will look
 * at them to setup the endpoints as dictated.
 */
static long
epctl(Ep *ep, Chan *c, void *a, long n)
{
	int i, l, port, mode, nb, tt;
	char *b, *s;
	Cmdbuf *cb;
	Cmdtab *ct;
	Ep *nep;
	Udev *d;

	cb = parsecmd(a, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, epctls, nelem(epctls));
	deprint("usb epctl %s\n", cb->f[0]);
	i = ct->index;
	if(i == CMnew || i == CMnewdev || i == CMhub || i == CMpreset || i == CMaddress)
		if(ep != ep->ep0)
			error("allowed only on a setup endpoint");
	eqlock(ep);
	if(waserror()){
		qunlock(ep);
		nexterror();
	}
	d = ep->dev;
	if(d->state == Ddetach)
		error(Edetach);
	switch(i){
	case CMnew:
		nb = strtol(cb->f[1], nil, 0);
		if(nb < 0 || nb >= Ndeveps)
			error("bad endpoint number");
		tt = name2ttype(cb->f[2]);
		if(tt == Tnone)
			error(Ebadtype);
		mode = name2mode(cb->f[3]);
		if(mode < 0)
			error("unknown i/o mode");
		nep = newdevep(ep, nb, tt, mode);
		nep->debug = ep->debug;
		putep(nep);
		break;
	case CMnewdev:
		if(d->state != Denabled)
			error(Edisabled);
		l = name2speed(cb->f[1]);
		if(l == Nospeed)
			error("speed must be full|low|high|super");
		if(l != d->speed)
			switch(d->speed){
			case Highspeed:
				if(l == Fullspeed)
					break;
				/* no break */
			case Fullspeed:
				if(l == Lowspeed)
					break;
				/* no break */
			default:
				error("wrong speed for hub/device");
			}
		port = strtoul(cb->f[2], nil, 0);
		if(port < 1 || port > d->nports)
			error(Ebadport);
		nep = newdev(ep->hp, ep, port, l);
		/* next read request will read
		 * the name for the new endpoint
		 */
		snprint(up->genbuf, sizeof(up->genbuf), "ep%d.%d", nep->dev->nb, nep->nb);
		kstrdup(&c->aux, up->genbuf);
		putep(nep);
		break;
	case CMdetach:
		if(d->depth < 0)
			error("can't detach a root hub");
		d->state = Ddetach;
		qunlock(ep);
		poperror();
		for(i = 0; i < nelem(d->eps); i++){
			ep = d->eps[i];
			if(ep != nil){
				qlock(ep);
				if(ep->inuse && ep->hp->epstop != nil && !waserror()){
					(*ep->hp->epstop)(ep);
					poperror();
				}
				qunlock(ep);
				putep(ep);
			}
		}
		goto Unlocked;
	case CMpreset:
		if(d->state != Denabled)
			error(Edisabled);
		d->state = Dreset;
		break;
	case CMdebugep:
		if(strcmp(cb->f[1], "on") == 0)
			ep->debug = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			ep->debug = 0;
		else
			ep->debug = strtoul(cb->f[1], nil, 0);
		print("usb: ep%d.%d debug %d\n", d->nb, ep->nb, ep->debug);
		break;
	case CMclrhalt:
		ep->clrhalt = 1;
		break;
	case CMaddress:
		if(d->state != Dconfig)
			error("address already set");
		if(d->addr == 0)
			d->addr = d->nb & Devmax;
		d->state = Denabled;
		break;
	case CMhub:
		if(cb->nf < 2)
			error(Ebadctl);
		if(ep->inuse)
			error(Einuse);
		if(d->depth >= 5)
			error("hub depth exceeded");
		port = strtoul(cb->f[1], nil, 0);
		if(port < 1 || port > 127
		|| port > 15 && d->speed == Superspeed)
			error(Ebadport);
		d->nports = port;
		if(d->speed == Highspeed && cb->nf >= 4){
			d->ttt = strtoul(cb->f[2], nil, 0) & 3;
			d->mtt = strtoul(cb->f[3], nil, 0) != 0;
		}
		break;
	case CMmaxpkt:
		if(ep->inuse)
			error(Einuse);
		l = strtoul(cb->f[1], nil, 0);
		if(l < 1 || l > 1024)
			error("maxpkt not in [1:1024]");
		ep->maxpkt = l;
		ep->hz = 0; /* recalculate */
		break;
	case CMntds:
		if(ep->inuse)
			error(Einuse);
		l = strtoul(cb->f[1], nil, 0);
		if(l < 1 || l > 3)
			error("ntds not in [1:3]");
		ep->ntds = l;
		ep->hz = 0; /* recalculate */
		break;
	case CMpollival:
		if(ep->ttype != Tintr && ep->ttype != Tiso)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = strtoul(cb->f[1], nil, 0);
		if(d->speed == Fullspeed || d->speed == Lowspeed){
			if(l < 1 || l > 255)
				error("pollival not in [1:255]");
		} else {
			if(l < 1 || l > 16)
				error("pollival power not in [1:16]");
			l = 1 << l-1;
		}
		ep->pollival = l;
		ep->hz = 0; /* recalculate */
		break;
	case CMtmout:
		if(ep->ttype == Tiso || ep->ttype == Tctl)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = strtoul(cb->f[1], nil, 0);
		if(l != 0 && l < Xfertmout)
			l = Xfertmout;
		ep->tmout = l;
		break;
	case CMhz:
		if(ep->ttype != Tiso)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = strtoul(cb->f[1], nil, 0);
		if(l <= 0 || l > 1000000000)
			error("hz not in [1:1000000000]");
		ep->hz = l;
		break;
	case CMsamplesz:
		if(ep->ttype != Tiso)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = strtoul(cb->f[1], nil, 0);
		if(l <= 0 || l > 8)
			error("samplesz not in [1:8]");
		ep->samplesz = l;
		break;
	case CMsampledelay:
		if(ep->ttype != Tiso)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = strtoul(cb->f[1], nil, 0);
		ep->sampledelay = l;
		break;
	case CMuframes:
		if(ep->ttype != Tiso)
			error(Ebadtype);
		if(ep->inuse)
			error(Einuse);
		l = strtoul(cb->f[1], nil, 0);
		if(l != 0 && l != 1)
			error("uframes not in [0:1]");
		ep->uframes = l;
		break;
	case CMname:
		if(ep->inuse)
			error(Einuse);
		s = cb->f[1];
		if(strlen(s) >= sizeof(up->genbuf))
			error(Etoolong);
		validname(s, 0);
		kstrdup(&ep->name, s);
		break;
	case CMinfo:
		s = a;
		s += 5, n -= 5;	/* "info " */
		if(n < 0)
			error(Ebadctl);
		b = smalloc(n+1);
		if(waserror()){
			free(b);
			nexterror();
		}
		memmove(b, s, n);
		poperror();
		b[n] = 0;
		s = strchr(b, '\n');
		if(s != nil)
			*s = 0;
		free(ep->info);
		ep->info = b;
		break;
	default:
		error(Ebadctl);
	}
	qunlock(ep);
	poperror();
Unlocked:
	free(cb);
	poperror();
	return n;
}

static long
usbctl(void *a, long n)
{
	Cmdtab *ct;
	Cmdbuf *cb;
	Ep *ep;
	int i;

	cb = parsecmd(a, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, usbctls, nelem(usbctls));
	dprint("usb ctl %s\n", cb->f[0]);
	switch(ct->index){
	case CMdebug:
		if(strcmp(cb->f[1], "on") == 0)
			debug = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			debug = 0;
		else
			debug = strtol(cb->f[1], nil, 0);
		for(i = 0; i < epmax; i++)
			if((ep = getep(i)) != nil){
				if(ep->hp->debug != nil)
					(*ep->hp->debug)(ep->hp, debug);
				putep(ep);
			}
		break;
	}
	free(cb);
	poperror();
	return n;
}

static long
ctlwrite(Chan *c, void *a, long n)
{
	int q;
	Ep *ep;

	q = QID(c->qid);
	if(q == Qctl)
		return usbctl(a, n);

	ep = getep(qid2epidx(q));
	if(ep == nil)
		error(Eio);
	if(waserror()){
		putep(ep);
		nexterror();
	}
	if(ep->dev->state == Ddetach)
		error(Edetach);

	if(isqtype(q, Qepctl) && c->aux != nil){
		/* Be sure we don't keep a cloned ep name */
		free(c->aux);
		c->aux = nil;
		error("read, not write, expected");
	}
	n = epctl(ep, c, a, n);
	putep(ep);
	poperror();
	return n;
}

static long
usbwrite(Chan *c, void *a, long n, vlong off)
{
	int nr, q;
	Ep *ep;

	if(c->qid.type == QTDIR)
		error(Eisdir);

	q = QID(c->qid);

	if(q == Qctl || isqtype(q, Qepctl))
		return ctlwrite(c, a, n);

	ep = getep(qid2epidx(q));
	if(ep == nil)
		error(Eio);
	if(waserror()){
		putep(ep);
		nexterror();
	}
	if(ep->mode == OREAD || ep->inuse == 0)
		error(Ebadusefd);
	if(ep->dev->state == Ddetach)
		error(Edetach);

	switch(ep->ttype){
	case Tnone:
		error(Enotconf);
	case Tctl:
		nr = rhubwrite(ep, a, n);
		if(nr >= 0){
			n = nr;
			break;
		}
		/* else fall */
	default:
		ddeprint("\nusbwrite q %#x fid %d cnt %ld off %lld\n",q, c->fid, n, off);
		(*ep->hp->epwrite)(ep, a, n);
	}
	putep(ep);
	poperror();
	return n;
}

void
usbshutdown(void)
{
	Hci *hp;
	int i;

	for(i = 0; i < Nhcis; i++){
		hp = hcis[i];
		if(hp == nil)
			continue;
		if(hp->shutdown == nil)
			print("#u: no shutdown function for %s\n", hp->type);
		else
			(*hp->shutdown)(hp);
	}
}

Dev usbdevtab = {
	L'u',
	"usb",

	usbreset,
	usbinit,
	usbshutdown,
	usbattach,
	usbwalk,
	usbstat,
	usbopen,
	devcreate,
	usbclose,
	usbread,
	devbread,
	usbwrite,
	devbwrite,
	devremove,
	devwstat,
};
