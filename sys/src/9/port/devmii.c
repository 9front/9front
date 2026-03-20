/* ethernet MII/SMI/MDIO phy bus debug driver */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../port/ethermii.h"

enum {
	Qdir = 0,	/* #Φ */
	Qbus,		/* #Φ/busN */
	Qphy,		/* #Φ/busN/phyN */
	Qctl,		/* #Φ/busN/phyN/ctl */
	Qmii,		/* #Φ/busN/phyN/mii (clause22) */
	Qmmd,		/* #Φ/busN/phyN/mmd (clause45) */
};

#define TYPE(q)		((ulong)(q).path & 0xF)
#define BUS(q)		(((ulong)(q).path>>4) & 0xFF)
#define PHY(q)		(((ulong)(q).path>>12) & 0x1F)
#define QID(b, p, t)	(((p)<<12)|((b)<<4)|(t))

static Lock buseslock;
static Mii *buses[32];

static void
addbus(Mii *mii)
{
	int i;

	if(mii == nil || mii->name == nil)
		return;

	lock(&buseslock);
	for(i = 0; i < nelem(buses); i++){
		if(buses[i] == nil)
			continue;
		if(buses[i] == mii || strcmp(buses[i]->name, mii->name) == 0){
			buses[i] = mii;
			goto out;
		}
	}
	for(i = 0; i < nelem(buses); i++){
		if(buses[i] == nil){
			buses[i] = mii;
			goto out;
		}
	}
out:
	unlock(&buseslock);
	return;
}

static void
delbus(Mii *mii)
{
	int i;

	lock(&buseslock);
	for(i = 0; i < nelem(buses); i++){
		if(buses[i] == mii){
			buses[i] = nil;
			break;
		}
	}
	unlock(&buseslock);
}

static Mii*
getbus(int x)
{
	Mii *mii;

	if((uint)x >= nelem(buses))
		return nil;

	lock(&buseslock);
	mii = buses[x];
	unlock(&buseslock);

	if(mii != nil && mii->name == nil)
		return nil;

	return mii;
}

static MiiPhy*
getphy(Chan *c)
{
	Mii *mii;
	MiiPhy *phy;

	if(TYPE(c->qid) < Qphy)
		error(Egreg);
	mii = getbus(BUS(c->qid));
	if(mii == nil)
		error(Egreg);
	phy = mii->phy[PHY(c->qid)];
	if(phy == nil)
		error(Egreg);
	return phy;
}

static void
linkage(void)
{
	addmiibus = addbus;
	delmiibus = delbus;
}

static int
miigen(Chan *c, char *, Dirtab*, int, int s, Dir *dp)
{
	Mii *mii;
	Qid q;

	switch(TYPE(c->qid)){
	case Qdir:
		if(s == DEVDOTDOT){
		Top:
			mkqid(&q, QID(0, 0, Qdir), 0, QTDIR);
			devdir(c, q, "#Φ", 0, eve, 0500, dp);
			return 1;
		}
		if((uint)s >= nelem(buses))
			return -1;
		mii = getbus(s);
		if(mii == nil)
			return 0;	/* continue search */
		mkqid(&q, QID(s, 0, Qbus), 0, QTDIR);
		devdir(c, q, mii->name, 0, eve, 0500, dp);
		return 1;
	case Qbus:
		if(s == DEVDOTDOT)
			goto Top;
		if((uint)s >= nelem(mii->phy))
			return -1;
		mii = getbus(BUS(c->qid));
		if(mii == nil)
			return -1;
		if(mii->phy[s] == nil)
			return 0;	/* continue search */
		mkqid(&q, QID(BUS(c->qid), s, Qphy), 0, QTDIR);
		snprint(up->genbuf, sizeof up->genbuf, "%d", s);
		devdir(c, q, up->genbuf, 0, eve, 0500, dp);
		return 1;
	case Qphy:
		if(s == DEVDOTDOT){
			mkqid(&q, QID(BUS(c->qid), 0, Qbus), 0, QTDIR);
			mii = getbus(BUS(c->qid));
			if(mii == nil)
				return -1;
			devdir(c, q, mii->name, 0, eve, 0500, dp);
			return 1;
		}
		if(s == 0) {
			mkqid(&q, QID(BUS(c->qid), PHY(c->qid), Qctl), 0, 0);
			devdir(c, q, "ctl", 0, eve, 0600, dp);
			return 1;
		}
		if(s == 1) {
			mkqid(&q, QID(BUS(c->qid), PHY(c->qid), Qmii), 0, 0);
			devdir(c, q, "mii", 0x20, eve, 0600, dp);
			return 1;
		}
		if(s == 2) {
			mkqid(&q, QID(BUS(c->qid), PHY(c->qid), Qmmd), 0, 0);
			devdir(c, q, "mmd", 0x200000, eve, 0600, dp);
			return 1;
		}
		break;
	}
	return -1;
}

static Chan*
miiattach(char *spec)
{
	return devattach(L'Φ', spec);
}

static Chan*
miiopen(Chan *c, int mode)
{
	return devopen(c, mode, nil, 0, miigen);
}

static int
getword(uchar *data)
{
	return data[0] | (int)data[1] << 8;
}

static void
putword(uchar *data, int w)
{
	data[0] = w & 0xFF;
	data[1] = w >> 8;	
}

static char*
phystatus(MiiPhy *phy, char *s, char *e)
{
	int i;

	s = seprint(s, e, "id %.8uX\n", phy->id);
	s = seprint(s, e, "oui %.5X\n", phy->oui);
	s = seprint(s, e, "link %d\n", phy->link);
	s = seprint(s, e, "speed %d\n", phy->speed);
	s = seprint(s, e, "fd %d\n", phy->fd);

	s = seprint(s, e, "dump");
	for(i = 0; i < NMiiPhyr; i++){
		if((i & 0x07) == 0)
			s = seprint(s, e, "\n\t");
		s = seprint(s, e, " %4.4uX", miimir(phy, i) & 0xFFFF);
	}
	s = seprint(s, e, "\n");

	return s;
}

enum {
	CMreset,
	CMstatus,
	CMautoneg,
};

static Cmdtab phyctlmsg[] =
{
	CMreset,	"reset",	1,
	CMstatus,	"status",	1,
	CMautoneg,	"autoneg",	0,
};

static long
phyctl(MiiPhy *phy, void *data, long len)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	int a[3];

	cb = parsecmd(data, len);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, phyctlmsg, nelem(phyctlmsg));
	switch(ct->index){
	case CMreset:
		miireset(phy);
		break;
	case CMstatus:
		miistatus(phy);
		break;
	case CMautoneg:
		a[0] = a[1] = a[2] = ~0;
		switch(cb->nf){
		case 4:	a[2] = strtoul(cb->f[3], nil, 0);
		case 3:	a[1] = strtoul(cb->f[2], nil, 0);
		case 2:	a[0] = strtoul(cb->f[1], nil, 0);
		}
		miiane(phy, a[0], a[1], a[2]);
		break;
	default:
		cmderror(cb, Ebadarg);
	}
	free(cb);
	poperror();
	return len;
}

static long
miiwrite(Chan *c, void *data, long len, vlong offset)
{
	switch(TYPE(c->qid)){
	case Qctl:
		return phyctl(getphy(c), data, len);
	case Qmii:
		if(len != 2)
			error(Eshort);
		if(offset & ~0xFFFF1FULL)
			return 0;
		miimiw(getphy(c), (int)offset, getword(data));
		return len;
	case Qmmd:
		if(len != 2)
			error(Eshort);
		if(offset >= 0x200000)
			return 0;
		miimmdw(getphy(c), (offset >> 16) & 0x1F, offset & 0xFFFF, getword(data));
		return len;
	}
	error(Egreg);
}

static long
miiread(Chan *c, void *data, long len, vlong offset)
{
	char *buf;
	int w;

	if(c->qid.type == QTDIR)
		return devdirread(c, data, len, nil, 0, miigen);

	switch(TYPE(c->qid)){
	case Qctl:
		buf = smalloc(READSTR);
		if(waserror()){
			free(buf);
			nexterror();
		}
		phystatus(getphy(c), buf, buf+READSTR);
		len = readstr((ulong)offset, data, len, buf);
		poperror();
		return len;
	case Qmii:
		if(len != 2)
			error(Eshort);
		if(offset & ~0xFFFF1FULL)
			return 0;
		w = miimir(getphy(c), (int)offset);
		if(w == -1)
			error(Eio);
		putword(data, w);
		return len;
	case Qmmd:
		if(len != 2)
			error(Eshort);
		if(offset >= 0x200000)
			return 0;
		w = miimmdr(getphy(c), (offset >> 16) & 0x1F, offset & 0xFFFF);
		if(w == -1)
			error(Eio);
		putword(data, w);
		return len;
	}
	error(Egreg);
}

static void
miiclose(Chan*)
{
}

static Walkqid*
miiwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, miigen);
}

static int
miistat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, nil, 0, miigen);
}

Dev miidevtab = {
	L'Φ',
	"mii",

	linkage,
	devinit,
	devshutdown,
	miiattach,
	miiwalk,
	miistat,
	miiopen,
	devcreate,
	miiclose,
	miiread,
	devbread,
	miiwrite,
	devbwrite,
	devremove,
	devwstat,
	devpower,
};
