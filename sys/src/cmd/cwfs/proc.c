#include "all.h"
#include "io.h"

void
newproc(void (*f)(void *), void *arg, char *text)
{
	int kid = rfork(RFPROC|RFMEM|RFNOWAIT);

	if (kid < 0)
		sysfatal("can't fork: %r");
	if (kid == 0) {
		procsetname("%s", text);
		(*f)(arg);
		exits("child returned");
	}
}
