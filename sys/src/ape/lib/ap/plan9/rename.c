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
	char buf[8192], *f, *t;
	Dir *s, *d, nd;
	int n, ffd, tfd;

	f = strrchr(from, '/');
	t = strrchr(to, '/');
	f = f != nil ? f+1 : (char*)from;
	t = t != nil ? t+1 : (char*)to;

	if(*f == '\0' || strcmp(f, ".") == 0 || strcmp(f, "..") == 0
	|| *t == '\0' || strcmp(t, ".") == 0 || strcmp(t, "..") == 0){
		errno = EINVAL;
		return -1;
	}

	if((s = _dirstat(from)) == nil){
		_syserrno();
		return -1;
	}
	if((d = _dirstat(to)) != nil){
		if(d->qid.type == s->qid.type
		&& d->qid.path == s->qid.path
		&& d->qid.vers == s->qid.vers
		&& d->type == s->type
		&& d->dev == s->dev)
			goto out;	/* same file */

		if((d->mode ^ s->mode) & DMDIR){
			errno = (d->mode & DMDIR) ? EISDIR : ENOTDIR;
			goto err;
		}
	}

	/* from and to are in same directory (we miss some cases) */
	if(f-from==t-to && strncmp(from, to, f-from)==0){
		if(d != nil && _REMOVE(to) < 0){
			_syserrno();
			goto err;
		}
		_nulldir(&nd);
		nd.name = t;
		if(_dirwstat(from, &nd) < 0){
			_syserrno();
			goto err;
		}
		goto out;
	}

	/* different directories: have to copy */
	if((ffd = _OPEN(from, OREAD)) < 0){
		_syserrno();
		goto err;
	}
	if((s->mode & DMDIR) != 0 && _READ(ffd, buf, sizeof(buf)) > 0){
		/* cannot copy non-empty directories */
		errno = EXDEV;
		_CLOSE(ffd);
		goto err;
	}
	if(d != nil && _REMOVE(to) < 0){
		_syserrno();
		_CLOSE(ffd);
		goto err;
	}
	if((tfd = _CREATE(to, OWRITE, s->mode)) < 0){
		_syserrno();
		_CLOSE(ffd);
		goto err;
	}
	while((n = _READ(ffd, buf, sizeof(buf))) > 0){
		if(_WRITE(tfd, buf, n) != n)
			break;
	}
	_CLOSE(ffd);
	_CLOSE(tfd);
	if(n != 0 || _REMOVE(from) < 0){
		_syserrno();
		_REMOVE(to);	/* cleanup */
		goto err;
	}
out:
	free(s);
	free(d);
	return 0;
err:
	free(s);
	free(d);
	return -1;
}
