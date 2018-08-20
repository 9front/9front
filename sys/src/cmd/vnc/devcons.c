#include	<u.h>
#include	<libc.h>
#include	"compat.h"
#include	"kbd.h"
#include	"error.h"

Snarf	snarf = {
	.vers =	1
};

enum{
	Qdir,
	Qcons,
	Qconsctl,
	Qsnarf,
};

static Dirtab consdir[]={
	".",		{Qdir, 0, QTDIR},	0,		DMDIR|0555,
	"cons",		{Qcons},	0,		0660,
	"consctl",	{Qconsctl},	0,		0220,
	"snarf",	{Qsnarf},	0,		0600,
};

static Chan*
consattach(char *spec)
{
	return devattach('c', spec);
}

static Walkqid*
conswalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name,nname, consdir, nelem(consdir), devgen);
}

static int
consstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, consdir, nelem(consdir), devgen);
}

static Chan*
consopen(Chan *c, int omode)
{
	c->aux = nil;
	c = devopen(c, omode, consdir, nelem(consdir), devgen);
	switch((ulong)c->qid.path){
	case Qconsctl:
		break;
	case Qsnarf:
		if((c->mode&3) == OWRITE || (c->mode&3) == ORDWR)
			c->aux = smalloc(sizeof(Snarf));
		break;
	}
	return c;
}

void
setsnarf(char *buf, int n, int *vers)
{
	int i;

	qlock(&snarf);
	snarf.vers++;
	if(vers)
		*vers = snarf.vers;	
	for(i = 0; i < nelem(consdir); i++){
		if(consdir[i].qid.type == Qsnarf){
			consdir[i].qid.vers = snarf.vers;
			break;
		}
	}
	free(snarf.buf);
	snarf.n = n;
	snarf.buf = buf;
	qunlock(&snarf);
}

static void
consclose(Chan *c)
{
	Snarf *t;

	switch((ulong)c->qid.path){
	/* last close of control file turns off raw */
	case Qconsctl:
		break;
	/* odd behavior but really ok: replace snarf buffer when /dev/snarf is closed */
	case Qsnarf:
		t = c->aux;
		if(t == nil)
			break;
		setsnarf(t->buf, t->n, 0);
		t->buf = nil;	/* setsnarf took it */
		free(t);
		c->aux = nil;
		break;
	}
}

static long
consread(Chan *c, void *buf, long n, vlong off)
{
	if(n <= 0)
		return n;
	switch((ulong)c->qid.path){
	case Qsnarf:
		qlock(&snarf);
		if(off < snarf.n){
			if(off + n > snarf.n)
				n = snarf.n - off;
			memmove(buf, snarf.buf+off, n);
		}else
			n = 0;
		qunlock(&snarf);
		return n;

	case Qdir:
		return devdirread(c, buf, n, consdir, nelem(consdir), devgen);

	case Qcons:
		error(Egreg);
		return -1;

	default:
		print("consread 0x%llux\n", c->qid.path);
		error(Egreg);
	}
	return -1;		/* never reached */
}

static long
conswrite(Chan *c, void *va, long n, vlong)
{
	Snarf *t;
	char *a;

	switch((ulong)c->qid.path){
	case Qcons:
		screenputs(va, n);
		break;

	case Qconsctl:
		error(Egreg);
		break;

	case Qsnarf:
		t = c->aux;
		/* always append only */
		if(t->n > MAXSNARF)	/* avoid thrashing when people cut huge text */
			error("snarf buffer too big");
		a = realloc(t->buf, t->n + n + 1);
		if(a == nil)
			error("snarf buffer too big");
		t->buf = a;
		memmove(t->buf+t->n, va, n);
		t->n += n;
		t->buf[t->n] = '\0';
		break;
	default:
		print("conswrite: 0x%llux\n", c->qid.path);
		error(Egreg);
	}
	return n;
}

Dev consdevtab = {
	'c',
	"cons",

	devreset,
	devinit,
	consattach,
	conswalk,
	consstat,
	consopen,
	devcreate,
	consclose,
	consread,
	devbread,
	conswrite,
	devbwrite,
	devremove,
	devwstat,
};
