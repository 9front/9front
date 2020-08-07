#include <u.h>
#include <libc.h>
#include <tos.h>

/*
 * nsec() is wallclock and can be adjusted by timesync
 * so need to use cycles() instead, but fall back to
 * nsec() in case we can't
 */
uvlong
nanosec(void)
{
	static uvlong fasthz, xstart;
	uvlong x, div;

	if(fasthz == ~0ULL)
		return nsec() - xstart;

	if(fasthz == 0){
		if((fasthz = _tos->cyclefreq) == 0){
			fasthz = ~0ULL;
			xstart = nsec();
			fprint(2, "cyclefreq not available, falling back to nsec()\n");
			fprint(2, "you might want to disable aux/timesync\n");
			return 0;
		}else{
			cycles(&xstart);
		}
	}
	cycles(&x);
	x -= xstart;

	/* this is ugly */
	for(div = 1000000000ULL; x < 0x1999999999999999ULL && div > 1 ; div /= 10ULL, x *= 10ULL);

	return x / (fasthz / div);
}
