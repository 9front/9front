#ifndef __SYSTIME_H
#define __SYSTIME_H
#pragma lib "/$M/lib/ape/libap.a"

#ifndef __TIMEVAL__
#define __TIMEVAL__
struct timeval {
	long	tv_sec;
	long	tv_usec;
};

struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};

#ifdef _BSD_EXTENSION
struct timezone {
	int	tz_minuteswest;
	int	tz_dsttime;
};
#endif
#endif /* __TIMEVAL__ */

#define ITIMER_REAL 0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF 3

extern int gettimeofday(struct timeval *, struct timezone *);
int getitimer(int, struct itimerval *);
int setitimer(int, const struct itimerval *, struct itimerval *);

#endif /* __SYSTIME_H */
