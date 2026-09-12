#include "os.h"
#include <libsec.h>

ulong
tslsel(ulong a, ulong b, ulong cond)
{
	// clamp to 0 or 1
	cond = (cond | -cond) >> 31;
	// sign extend either 0 or -1 for a mask
	cond = -cond;
	return b ^ (cond & (a ^ b));
}
