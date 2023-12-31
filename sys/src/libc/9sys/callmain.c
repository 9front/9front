#include <u.h>
#include <libc.h>

void **_privates;
int _nprivates;

enum{
	NPRIVATES=16,
};

#pragma profile off

void
_callmain(void (*main)(int, char**), int argc, char *arg0)
{
	void *privates[NPRIVATES];

	_privates = privates;
	_nprivates = NPRIVATES;
	main(argc, &arg0);
	exits("main");
}

#pragma profile on
