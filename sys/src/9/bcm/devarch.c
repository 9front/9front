#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

enum {
	Qdir = 0,
	Qbase,

	Qmax = 16,
};

typedef long Rdwrfn(Chan*, void*, long, vlong);

static Rdwrfn *readfn[Qmax];
static Rdwrfn *writefn[Qmax];

static Dirtab archdir[Qmax] = {
	".",		{ Qdir, 0, QTDIR },	0,	0555,
};

Lock archwlock;	/* the lock is only for changing archdir */
int narchdir = Qbase;

/*
 * Add a file to the #P listing.  Once added, you can't delete it.
 * You can't add a file with the same name as one already there,
 * and you get a pointer to the Dirtab entry so you can do things
 * like change the Qid version.  Changing the Qid path is disallowed.
 */
Dirtab*
addarchfile(char *name, int perm, Rdwrfn *rdfn, Rdwrfn *wrfn)
{
	int i;
	Dirtab d;
	Dirtab *dp;

	memset(&d, 0, sizeof d);
	strcpy(d.name, name);
	d.perm = perm;

	lock(&archwlock);
	if(narchdir >= Qmax){
		unlock(&archwlock);
		return nil;
	}

	for(i=0; i<narchdir; i++)
		if(strcmp(archdir[i].name, name) == 0){
			unlock(&archwlock);
			return nil;
		}

	d.qid.path = narchdir;
	archdir[narchdir] = d;
	readfn[narchdir] = rdfn;
	writefn[narchdir] = wrfn;
	dp = &archdir[narchdir++];
	unlock(&archwlock);

	return dp;
}

static Chan*
archattach(char* spec)
{
	return devattach('P', spec);
}

Walkqid*
archwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, archdir, narchdir, devgen);
}

static int
archstat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, archdir, narchdir, devgen);
}

static Chan*
archopen(Chan* c, int omode)
{
	return devopen(c, omode, archdir, narchdir, devgen);
}

static void
archclose(Chan*)
{
}

static long
archread(Chan *c, void *a, long n, vlong offset)
{
	Rdwrfn *fn;

	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, archdir, narchdir, devgen);

	default:
		if(c->qid.path < narchdir && (fn = readfn[c->qid.path]))
			return fn(c, a, n, offset);
		error(Eperm);
		break;
	}
}

static long
archwrite(Chan *c, void *a, long n, vlong offset)
{
	Rdwrfn *fn;

	if(c->qid.path < narchdir && (fn = writefn[c->qid.path]))
		return fn(c, a, n, offset);
	error(Eperm);
}

void archinit(void);

Dev archdevtab = {
	'P',
	"arch",

	devreset,
	archinit,
	devshutdown,
	archattach,
	archwalk,
	archstat,
	archopen,
	devcreate,
	archclose,
	archread,
	devbread,
	archwrite,
	devbwrite,
	devremove,
	devwstat,
};

static long
cputyperead(Chan*, void *a, long n, vlong offset)
{
	char name[64], str[128];

	cputype2name(name, sizeof name);
	snprint(str, sizeof str, "ARM %s %d\n", name, m->cpumhz);
	return readstr(offset, a, n, str);
}

static long
cputempread(Chan*, void *a, long n, vlong offset)
{
	char str[32];
	uint t = getcputemp();
	snprint(str, sizeof str, "%ud.%ud\n", t/1000, t%1000);
	return readstr(offset, a, n, str);
}

void
archinit(void)
{
	addarchfile("cputype", 0444, cputyperead, nil);
	addarchfile("cputemp", 0444, cputempread, nil);
}

void
uartconsinit(void)
{
	extern PhysUart *physuart[];
	char *p, *cmd;
	Uart *uart;
	int i, n;

	if((p = getconf("console")) == nil)
		return;
	i = strtoul(p, &cmd, 0);
	if(p == cmd)
		return;
	/* we only have two possible uarts, the pl011 and aux */
	for(n = 0; physuart[n] != nil; n++)
		;
	if(i < 0 || i >= n)
		return;
	uart = physuart[i]->pnp();
	if(!uart->enabled)
		(*uart->phys->enable)(uart, 0);
	uartctl(uart, "l8 pn s1");
	if(*cmd != '\0')
		uartctl(uart, cmd);
	uart->console = 1;
	consuart = uart;
	uartputs(kmesg.buf, kmesg.n);
}

void
okay(int on)
{
	static int first;
	static int okled, polarity;
	char *p;

	if(!first++){
		p = getconf("bcm2709.disk_led_gpio");
		if(p == nil)
			p = getconf("bcm2708.disk_led_gpio");
		if(p != nil)
			okled = strtol(p, 0, 0);
		else
			okled = 'v';
		p = getconf("bcm2709.disk_led_active_low");
		if(p == nil)
			p = getconf("bcm2708.disk_led_active_low");
		polarity = (p == nil || *p == '1');
		if(okled != 'v')
			gpiosel(okled, Output);
	}
	if(okled == 'v')
		vgpset(0, on);
	else if(okled != 0)
		gpioout(okled, on^polarity);
}
