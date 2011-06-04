#define _PLAN9_SOURCE
#include "../plan9/lib.h"
#include <sys/types.h>
#include <time.h>
#include "../plan9/sys9.h"

int
nanosleep(struct timespec *req, struct timespec *rem)
{
	int ms;

	ms = req->tv_sec * 1000 + (req->tv_nsec + 999999) / 1000000;
	if(_SLEEP(ms) < 0) {
		if(rem) {
			rem->tv_sec = rem->tv_nsec = 0; /* needs better handling */
		}
		return -1;
	}
	if(rem)
		rem->tv_sec = rem->tv_nsec = 0;
	return 0;
}
