#include <sys/types.h>
#include <time.h>
#include <sys/time.h>

/* ap/plan9/9nsec.c */
extern long long _NSEC(void);

int
gettimeofday(struct timeval *tp, struct timezone *tzp)
{
	long long t;

	t = _NSEC();
	tp->tv_sec = t/1000000000;
	tp->tv_usec = (t/1000)%1000000;

	if(tzp) {
		tzp->tz_minuteswest = 4*60;	/* BUG */
		tzp->tz_dsttime = 1;
	}

	return 0;
}
