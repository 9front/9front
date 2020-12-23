#include "lib.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "sys9.h"
#include "dir.h"

char *
getlogin_r(char *user, int len)
{
	char name[32];
	Dir *dir;

	snprintf(name, sizeof(name), "/proc/%d/status", getpid());
	if((dir = _dirstat(name)) == nil){
		_syserrno();
		return NULL;
	}
	snprintf(user, len, "%s", dir->uid);
	free(dir);
	return user;
}

char *
getlogin(void)
{
	static char buf[NAME_MAX+1];

	return getlogin_r(buf, sizeof buf);
}
