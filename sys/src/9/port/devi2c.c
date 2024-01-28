/* I²C bus driver */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../port/i2c.h"

enum {
	Qdir = 0,	/* #J */
	Qbus,		/* #J/bus */
	Qctl,		/* #J/bus/i2c.n.ctl */
	Qdata,		/* #J/bus/i2c.n.data */
};

#define TYPE(q)		((ulong)(q).path & 0x0F)
#define BUS(q)		(((ulong)(q).path>>4) & 0xFF)
#define DEV(q)		(((ulong)(q).path>>12) & 0xFFF)
#define QID(d, b, t)	(((d)<<12)|((b)<<4)|(t))

static I2Cbus *buses[16];

static I2Cdev *devs[1024];
static Lock devslock;

static void probebus(I2Cbus *bus);

void
addi2cbus(I2Cbus *bus)
{
	int i;

	if(bus == nil)
		return;

	for(i = 0; i < nelem(buses); i++){
		if(buses[i] == nil
		|| buses[i] == bus
		|| strcmp(bus->name, buses[i]->name) == 0){
			buses[i] = bus;
			break;
		}
	}
}

I2Cbus*
i2cbus(char *name)
{
	I2Cbus *bus;
	int i;

	for(i = 0; i < nelem(buses); i++){
		bus = buses[i];
		if(bus == nil)
			break;
		if(strcmp(bus->name, name) == 0){
			probebus(bus);
			return bus;
		}
	}
	return nil;
}

void
addi2cdev(I2Cdev *dev)
{
	int i;

	if(dev == nil || dev->bus == nil)
		return;

	lock(&devslock);
	for(i = 0; i < nelem(devs); i++){
		if(devs[i] == nil
		|| devs[i] == dev
		|| devs[i]->addr == dev->addr && devs[i]->bus == dev->bus){
			devs[i] = dev;
			unlock(&devslock);
			return;
		}
	}
	unlock(&devslock);
}

I2Cdev*
i2cdev(I2Cbus *bus, int addr)
{
	I2Cdev *dev;
	int i;

	if(bus == nil || addr < 0 || addr >= 1<<10)
		return nil;

	lock(&devslock);
	for(i = 0; i < nelem(devs) && (dev = devs[i]) != nil; i++){
		if(dev->addr == addr && dev->bus == bus){
			unlock(&devslock);
			return dev;
		}
	}
	unlock(&devslock);

	return nil;
}


static int
enterbus(I2Cbus *bus)
{
	if(up != nil && islo()){
		eqlock(bus);
		return 1;
	} else {
		while(!canqlock(bus))
			;
		return 0;
	}
}

static void
leavebus(I2Cbus *bus)
{
	qunlock(bus);
}

int
i2cbusio(I2Cdev *dev, uchar *pkt, int olen, int ilen)
{
	I2Cbus *bus = dev->bus;
	int user, n;

	user =  enterbus(bus);
	if(!bus->probed){
		leavebus(bus);
		return -1;
	}
	if(user && waserror()){
		(*bus->io)(dev, nil, 0, 0);
		leavebus(bus);
		nexterror();
	}
// iprint("%s: <- %.*H\n", bus->name, olen, pkt);
	n = (*bus->io)(dev, pkt, olen, ilen);
// if(n > olen) iprint("%s: -> %.*H\n", bus->name, n - olen, pkt+olen);

	leavebus(bus);
	if(user) poperror();

	return n;
}

static int
putaddr(I2Cdev *dev, int isread, uchar *pkt, vlong addr)
{
	int n, o = 0;

	if(dev->a10){
		pkt[o++] = 0xF0 | (dev->addr>>(8-1))&6 | (isread != 0);
		pkt[o++] = dev->addr;
	} else
		pkt[o++] = dev->addr<<1 | (isread != 0);

	if(addr >= 0){
		for(n=0; n<dev->subaddr; n++)
			pkt[o++] = addr >> (n*8);
	}

	return o;
}

int
i2csend(I2Cdev *dev, void *data, int len, vlong addr)
{
	uchar pkt[138];
	int o;

	o = putaddr(dev, 0, pkt, addr);
	if(o+len > sizeof(pkt))
		len = sizeof(pkt)-o;

	if(len > 0)
		memmove(pkt+o, data, len);

	return i2cbusio(dev, pkt, o + len, 0) - o;
}
	
int
i2crecv(I2Cdev *dev, void *data, int len, vlong addr)
{
	uchar pkt[138];
	int o;

	o = putaddr(dev, 1, pkt, addr);
	if(o+len > sizeof(pkt))
		len = sizeof(pkt)-o;

	len = i2cbusio(dev, pkt, o, len) - o;
	if(len > 0)
		memmove(data, pkt+o, len);

	return  len;
}

int
i2cquick(I2Cdev *dev, int rw)
{
	uchar pkt[2];
	int o = putaddr(dev, rw, pkt, -1);
	if(i2cbusio(dev, pkt, o, 0) != o)
		return -1;
	return rw != 0;
}
int
i2crecvbyte(I2Cdev *dev)
{
	uchar pkt[2+1];
	int o = putaddr(dev, 1, pkt, -1);
	if(i2cbusio(dev, pkt, o, 1) - o != 1)
		return -1;
	return pkt[o];
}
int
i2csendbyte(I2Cdev *dev, uchar b)
{
	uchar pkt[2+1];
	int o = putaddr(dev, 0, pkt, -1);
	pkt[o] = b;
	if(i2cbusio(dev, pkt, o+1, 0) - o != 1)
		return -1;
	return b;
}
int
i2creadbyte(I2Cdev *dev, ulong addr)
{
	uchar pkt[2+4+1];
	int o = putaddr(dev, 1, pkt, addr);
	if(i2cbusio(dev, pkt, o, 1) - o != 1)
		return -1;
	return pkt[o];
}
int
i2cwritebyte(I2Cdev *dev, ulong addr, uchar b)
{
	uchar pkt[2+4+1];
	int o = putaddr(dev, 0, pkt, addr);
	pkt[o] = b;
	if(i2cbusio(dev, pkt, o+1, 0) - o != 1)
		return -1;
	return b;
}
int
i2creadword(I2Cdev *dev, ulong addr)
{
	uchar pkt[2+4+2];
	int o = putaddr(dev, 1, pkt, addr);
	if(i2cbusio(dev, pkt, o, 2) - o != 2)
		return -1;
	return pkt[o] | (ushort)pkt[o+1]<<8;
}
int
i2cwriteword(I2Cdev *dev, ulong addr, ushort w)
{
	uchar pkt[2+4+2];
	int o = putaddr(dev, 0, pkt, addr);
	pkt[o+0] = w;
	pkt[o+1] = w>>8;
	if(i2cbusio(dev, pkt, o+2, 0) - o != 2)
		return -1;
	return w;
}
vlong
i2cread32(I2Cdev *dev, ulong addr)
{
	uchar pkt[2+4+4];
	int o = putaddr(dev, 1, pkt, addr);
	if(i2cbusio(dev, pkt, o, 4) - o != 4)
		return -1;
	return pkt[o] | (ulong)pkt[o+1]<<8 | (ulong)pkt[o+2]<<16 | (ulong)pkt[o+3]<<24;
}
vlong
i2cwrite32(I2Cdev *dev, ulong addr, ulong u)
{
	uchar pkt[2+4+4];
	int o = putaddr(dev, 0, pkt, addr);
	pkt[o+0] = u;
	pkt[o+1] = u>>8;
	pkt[o+2] = u>>16;
	pkt[o+3] = u>>24;
	if(i2cbusio(dev, pkt, o+4, 0) - o != 4)
		return -1;
	return u;
}

static void
probeddev(I2Cdev *dummy)
{
	I2Cdev *dev = smalloc(sizeof(I2Cdev));
	memmove(dev, dummy, sizeof(I2Cdev));
	addi2cdev(dev);
}

static void
probebus(I2Cbus *bus)
{
	I2Cdev dummy;
	uchar pkt[2];
	int user, n;

	if(bus->probed)
		return;

	user = enterbus(bus);
	if(bus->probed){
		leavebus(bus);
		return;
	}
	if(user && waserror()
	|| (*bus->init)(bus)){
		leavebus(bus);
		if(user) nexterror();
		return;
	}

	memset(&dummy, 0, sizeof(dummy));
	dummy.bus = bus;

	dummy.a10 = 0;
	for(dummy.addr = 8; dummy.addr < 0x78; dummy.addr++) {
		if(i2cdev(bus, dummy.addr) != nil)
			continue;
		if(user && waserror()){
			(*bus->io)(&dummy, nil, 0, 0);
			continue;
		}
		n = putaddr(&dummy, 0, pkt, -1);
		if((*bus->io)(&dummy, pkt, n, 0) == n)
			probeddev(&dummy);
		if(user) poperror();
	}

	dummy.a10 = 1;
	for(dummy.addr = 0; dummy.addr < (1<<10); dummy.addr++) {
		if(i2cdev(bus, dummy.addr) != nil)
			continue;
		if(user && waserror()){
			(*bus->io)(&dummy, nil, 0, 0);
			continue;
		}
		n = putaddr(&dummy, 0, pkt, -1);
		if((*bus->io)(&dummy, pkt, n, 0) == n)
			probeddev(&dummy);
		if(user) poperror();
	}

	bus->probed = 1;
	leavebus(bus);
	if(user) poperror();
}

static int
i2cgen(Chan *c, char *, Dirtab*, int, int s, Dir *dp)
{
	I2Cbus *bus;
	I2Cdev *dev;

	Qid q;

	switch(TYPE(c->qid)){
	case Qdir:
		if(s == DEVDOTDOT){
		Gendir:
			mkqid(&q, QID(0, 0, Qdir), 0, QTDIR);
			snprint(up->genbuf, sizeof up->genbuf, "#J");
			devdir(c, q, up->genbuf, 0, eve, 0500, dp);
			return 1;
		}
		if(s >= nelem(buses))
			return -1;
		bus = buses[s];
		if(bus == nil)
			return -1;
		mkqid(&q, QID(0, s, Qbus), 0, QTDIR);
		devdir(c, q, bus->name, 0, eve, 0500, dp);
		return 1;
	case Qbus:
		if(s == DEVDOTDOT)
			goto Gendir;
		if((s/2) >= nelem(devs))
			return -1;

		bus = buses[BUS(c->qid)];
		probebus(bus);

		lock(&devslock);
		dev = devs[s/2];
		unlock(&devslock);

		if(dev == nil)
			return -1;
		if(dev->bus != bus)
			return 0;
		if(s & 1){
			mkqid(&q, QID(dev->addr, BUS(c->qid), Qdata), 0, 0);
			goto Gendata;
		}
		mkqid(&q, QID(dev->addr, BUS(c->qid), Qctl), 0, 0);
		goto Genctl;
	case Qctl:
		q = c->qid;
	Genctl:
		snprint(up->genbuf, sizeof up->genbuf, "i2c.%lux.ctl", DEV(q));
		devdir(c, q, up->genbuf, 0, eve, 0600, dp);
		return 1;
	case Qdata:
		q = c->qid;
		bus = buses[BUS(q)];
		dev = i2cdev(bus, DEV(q));
		if(dev == nil)
			return -1;
	Gendata:
		snprint(up->genbuf, sizeof up->genbuf, "i2c.%lux.data", DEV(q));
		devdir(c, q, up->genbuf, dev->size, eve, 0600, dp);
		return 1;
	}
	return -1;
}

static Chan*
i2cattach(char *spec)
{
	return devattach('J', spec);
}

static Chan*
i2copen(Chan *c, int mode)
{
	c = devopen(c, mode, nil, 0, i2cgen);
	switch(TYPE(c->qid)){
	case Qctl:
	case Qdata:
		c->aux = i2cdev(buses[BUS(c->qid)], DEV(c->qid));
		if(c->aux == nil)
			error(Enonexist);
		break;
	}
	return c;
}

enum {
	CMsize,
	CMsubaddress,
};

static Cmdtab i2cctlmsg[] =
{
	CMsize,		"size",		2,
	CMsubaddress,	"subaddress",	2,
};

static long
i2cwrctl(I2Cdev *dev, void *data, long len)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	ulong u;

	cb = parsecmd(data, len);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, i2cctlmsg, nelem(i2cctlmsg));
	switch(ct->index){
	case CMsize:
		dev->size = strtoul(cb->f[1], nil, 0);
		break;
	case CMsubaddress:
		u = strtoul(cb->f[1], nil, 0);
		if(u > 4)
			cmderror(cb, Ebadarg);
		dev->subaddr = u;
		break;
	default:
		cmderror(cb, Ebadarg);
	}
	free(cb);
	poperror();
	return len;
}

static long
i2crdctl(I2Cdev *dev, void *data, long len, vlong offset)
{
	char cfg[64];

	snprint(cfg, sizeof(cfg), "size %lud\nsubaddress %d\n", dev->size, dev->subaddr);
	return readstr((ulong)offset, data, len, cfg);
}

static long
i2cwrite(Chan *c, void *data, long len, vlong offset)
{
	I2Cdev *dev;

	switch(TYPE(c->qid)){
	default:
		error(Egreg);
	case Qctl:
		dev = c->aux;
		return i2cwrctl(dev, data, len);
	case Qdata:
		break;
	}
	dev = c->aux;
	if(dev->size){
		if(offset+len > dev->size){
			if(offset >= dev->size)
				return 0;
			len = dev->size - offset;
		}
	}
	len = i2csend(dev, data, len, offset);
	if(len < 0)
		error(Eio);
	return len;
}

static long
i2cread(Chan *c, void *data, long len, vlong offset)
{
	I2Cdev *dev;

	if(c->qid.type == QTDIR)
		return devdirread(c, data, len, nil, 0, i2cgen);

	switch(TYPE(c->qid)){
	default:
		error(Egreg);
	case Qctl:
		dev = c->aux;
		return i2crdctl(dev, data, len, offset);
	case Qdata:
		break;
	}
	dev = c->aux;
	if(dev->size){
		if(offset+len > dev->size){
			if(offset >= dev->size)
				return 0;
			len = dev->size - offset;
		}
	}
	len = i2crecv(dev, data, len, offset);
	if(len < 0)
		error(Eio);
	return len;
}

void
i2cclose(Chan*)
{
}

static Walkqid*
i2cwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, i2cgen);
}

static int
i2cstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, nil, 0, i2cgen);
}

Dev i2cdevtab = {
	'J',
	"i2c",

	devreset,
	devinit,
	devshutdown,
	i2cattach,
	i2cwalk,
	i2cstat,
	i2copen,
	devcreate,
	i2cclose,
	i2cread,
	devbread,
	i2cwrite,
	devbwrite,
	devremove,
	devwstat,
	devpower,
};
