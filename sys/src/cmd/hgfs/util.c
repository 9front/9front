#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

int
getdothg(char *dothg, char *path)
{
	char buf[MAXPATH], *s;

	if(path != nil){
		snprint(buf, sizeof(buf), "%s", path);
		cleanname(buf);
	} else if(getwd(buf, sizeof(buf)) == nil)
		return -1;
	for(;;){
		snprint(dothg, MAXPATH, "%s/.hg", buf);
		if(access(dothg, AEXIST) == 0)
			return 0;
		if(path != nil)
			break;
		if((s = strrchr(buf, '/')) == nil)
			break;
		*s = 0;
	}
	return -1;
}
