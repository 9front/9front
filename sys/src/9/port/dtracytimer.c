#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<dtracy.h>

static DTProbe *timerprobe;

static void
dtracytimer(void *)
{
	DTTrigInfo info;

	memset(&info, 0, sizeof(info));
	for(;;){
		tsleep(&up->sleep, return0, nil, 1000);
		dtptrigger(timerprobe, &info);
	}
}

static void
timerprovide(DTProvider *prov)
{
	timerprobe = dtpnew("timer::1s", prov, nil);
}

static int
timerenable(DTProbe *)
{
	static int gotkproc;
	
	if(!gotkproc){
		kproc("dtracytimer", dtracytimer, nil);
		gotkproc=1;
	}
	return 0;
}

static void
timerdisable(DTProbe *)
{
}

DTProvider dtracytimerprov = {
	.name = "timer",
	.provide = timerprovide,
	.enable = timerenable,
	.disable = timerdisable,
};
