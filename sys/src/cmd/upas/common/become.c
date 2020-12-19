#include "common.h"
#include <auth.h>
#include <ndb.h>

/*
 *  become powerless user
 */
int
become(char **, char *who)
{
	if(strcmp(who, "none") == 0) {
		if(procsetuser("none") < 0) {
			werrstr("can't become none: %r");
			return -1;
		}
		if(newns("none", nil) < 0) {
			werrstr("can't set new namespace: %r");
			return -1;
		}
	}
	return 0;
}

