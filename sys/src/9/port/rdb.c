#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"

static char*
getline(void)
{
	static char buf[128];
	int i, c;

	for(i = 0; i < sizeof(buf)-1 && (c=uartgetc()) != '\n'; i++)
		buf[i] = c;
	buf[i] = 0;
	return buf;
}

static void*
addr(char *s, Ureg *ureg, char **p)
{
	uvlong a;

	a = strtoull(s, p, 16);
	if(a < sizeof(Ureg))
		return ((uchar*)ureg)+a;
	return (void*)(uintptr)a;
}

static void
talkrdb(Ureg *ureg)
{
	uchar *a;
	char *p, *req;

	if(consuart == nil)
		return;

	if(serialoq != nil){
		qhangup(serialoq, nil);
		if(consuart->phys->disable != nil)
			consuart->phys->disable(consuart);
		if(consuart->phys->enable != nil)
			consuart->phys->enable(consuart, 0);
		serialoq = nil;
	}
	kprintoq = nil;		/* turn off /dev/kprint if active */

	iprint("Edebugger reset\n");
	for(;;){
		req = getline();
		switch(*req){
		case 'r':
			a = addr(req+1, ureg, nil);
			iprint("R%.8zux %.2ux %.2ux %.2ux %.2ux\n",
				(uintptr)a, a[0], a[1], a[2], a[3]);
			break;

		case 'w':
			a = addr(req+1, ureg, &p);
			*(ulong*)a = strtoul(p, nil, 16);
			iprint("W\n");
			break;

		default:
			iprint("Eunknown message\n");
			break;
		}
	}
}

void
rdb(void)
{
	splhi();
	iprint("rdb...");
	callwithureg(talkrdb);
}
