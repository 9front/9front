#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

static QLock mousectlqlock;
static int accelerated;

enum
{
	CMaccelerated,
	CMlinear,
};

static Cmdtab mousectlmsg[] =
{
	CMaccelerated,		"accelerated",		0,
	CMlinear,		"linear",		1,
};


static void
setaccelerated(int x)
{
	accelerated = x;
	mouseaccelerate(x);
}

static void
setlinear(void)
{
	accelerated = 0;
	mouseaccelerate(0);
}

void
mousectl(Cmdbuf *cb)
{
	Cmdtab *ct;

	qlock(&mousectlqlock);
	if(waserror()){
		qunlock(&mousectlqlock);
		nexterror();
	}
	ct = lookupcmd(cb, mousectlmsg, nelem(mousectlmsg));
	switch(ct->index){
	case CMaccelerated:
		setaccelerated(cb->nf == 1? 1: atoi(cb->f[1]));
		break;
	case CMlinear:
		setlinear();
		break;
	}
	qunlock(&mousectlqlock);
	poperror();
}
