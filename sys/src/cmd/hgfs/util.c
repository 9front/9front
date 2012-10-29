#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

ulong
hashstr(char *s)
{
	ulong h, t;
	char c;

	h = 0;
	while(c = *s++){
		t = h & 0xf8000000;
		h <<= 5;
		h ^= t>>27;
		h ^= (ulong)c;
	}
	return h;
}

int
getworkdir(char *work, char *path)
{
	char buf[MAXPATH], *s;

	if(path != nil){
		snprint(work, MAXPATH, "%s", path);
		cleanname(work);
	} else if(getwd(work, MAXPATH) == nil)
		return -1;
	for(;;){
		snprint(buf, sizeof(buf), "%s/.hg", work);
		if(access(buf, AEXIST) == 0)
			return 0;
		if(path != nil)
			break;
		if((s = strrchr(work, '/')) == nil)
			break;
		*s = 0;
	}
	return -1;
}

int
readfile(char *path, char *buf, int nbuf)
{
	int fd, n;

	n = 0;
	if((fd = open(path, OREAD)) >= 0){
		if((n = read(fd, buf, nbuf-1)) < 0)
			n = 0;
		close(fd);
	}
	buf[n] = '\0';
	return n;
}
