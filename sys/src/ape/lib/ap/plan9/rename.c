#include "lib.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "sys9.h"
#include "dir.h"

int
rename(const char *from, const char *to)
{
	int n, ffd, tfd;
	char *f, *t;
	Dir *d, nd;

	if(access(to, 0) >= 0){
		if(_REMOVE(to) < 0){
			_syserrno();
			return -1;
		}
	}
	if((d = _dirstat(to)) != nil){
		free(d);
		errno = EEXIST;
		return -1;
	}
	if((d = _dirstat(from)) == nil){
		_syserrno();
		return -1;
	}
	f = strrchr(from, '/');
	t = strrchr(to, '/');
	f = f? f+1 : (char*)from;
	t = t? t+1 : (char*)to;
	if(f-from==t-to && strncmp(from, to, f-from)==0){
		/* from and to are in same directory (we miss some cases) */
		_nulldir(&nd);
		nd.name = t;
		if(_dirwstat(from, &nd) < 0){
			_syserrno();
			return -1;
		}
	}else{
		/* different directories: have to copy */
		char buf[8192];


		if((ffd = _OPEN(from, OREAD)) == -1)
			goto err1;
		if((tfd = _CREATE(to, OWRITE, d->mode)) == -1)
			goto err2;
		n = 0;
		while(n>=0){
			if((n = _READ(ffd, buf, sizeof(buf))) == -1)
				goto err2;
			if(_WRITE(tfd, buf, n) != n)
				goto err2;
		}
		_CLOSE(ffd);
		_CLOSE(tfd);
		if(_REMOVE(from) < 0)
			goto err2;
	}
	free(d);
	return 0;

err2:
	_CLOSE(tfd);
err1:
	_CLOSE(ffd);
	_syserrno();
	free(d);
	return -1;
}
