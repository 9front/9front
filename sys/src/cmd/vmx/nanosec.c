#include <u.h>
#include <libc.h>
#include <tos.h>

#define Nsec 1000000000ULL

/*
 * nsec() is wallclock and can be adjusted by timesync
 * so need to use cycles() instead, but fall back to
 * nsec() in case we can't
 */
uvlong
nanosec(void)
{
	static uvlong fasthz, xstart;
	uvlong x;

	if(fasthz == ~0ULL)
		return uptime() - xstart;

	if(fasthz == 0){
		if(_tos->cyclefreq){
			fasthz = _tos->cyclefreq;
			cycles(&xstart);
		} else {
			fasthz = ~0ULL;
			xstart = uptime();
		}
		return 0;
	}
	cycles(&x);
	x -= xstart;

	uvlong q = x / fasthz;
	uvlong r = x % fasthz;

	return q*Nsec + r*Nsec/fasthz;
}
