#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"ureg.h"

#include	<dtracy.h>

static DTProbe *timerprobe;
static int	running;

void
dtracytick(Ureg *ur)
{
	DTTrigInfo info;

	if(!running)
		return;
	memset(&info, 0, sizeof(info));
	info.arg[0] = ur->pc;
	info.arg[1] = userureg(ur);
	dtptrigger(timerprobe, &info);
}

static void
timerprovide(DTProvider *prov)
{
	if(timerprobe == nil)
		timerprobe = dtpnew("timer::1tk", prov, nil);
}

static int
timerenable(DTProbe *)
{
	running = 1;
	return 0;
}

static void
timerdisable(DTProbe *)
{
	running = 0;
}

DTProvider dtracytimerprov = {
	.name = "timer",
	.provide = timerprovide,
	.enable = timerenable,
	.disable = timerdisable,
};
