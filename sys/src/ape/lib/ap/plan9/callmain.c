#include "lib.h"
#include "sys9.h"
#include <stdlib.h>

extern	void _envsetup(void);
extern	char **environ;
extern	int *_errnoloc;

void **_privates;
int _nprivates;
char *_plan9err;

enum{
	NPRIVATES=16,
};

#pragma profile off

void
_callmain(int (*f)(int, char**), int argc, char *arg0)
{
	int errno;
	char err[ERRMAX];
	void *privates[NPRIVATES];

	memset(privates, 0, sizeof(privates));
	err[0] = '\0';
	_privates = privates;
	_nprivates = NPRIVATES;
	_errnoloc = &errno;
	_plan9err = &err[0];
	exit(f(argc, &arg0));
}

int
_apemain(int argc, char **argv)
{
	_envsetup();
	extern int main(int, char**, char**);
	return main(argc, argv, environ);
}

#pragma profile on
