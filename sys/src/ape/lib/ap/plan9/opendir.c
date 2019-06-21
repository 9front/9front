#include "lib.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "sys9.h"
#include "dir.h"

#define DBLOCKSIZE 20

DIR *
opendir(const char *filename)
{
	int f, n;
	DIR *d;
	char *s;

	n = strlen(filename);
	if(n > 0 && filename[n-1] != '/'){
		s = malloc(n+2);
		if(s == NULL)
			goto Nomem;
		memcpy(s, filename, n);
		s[n++] = '/';
		s[n] = 0;
	} else
		s = (char*)filename;
	f = open(s, O_RDONLY);
	if(s != filename)
		free(s);
	if(f < 0){
		_syserrno();
		return NULL;
	}
	_fdinfo[f].flags |= FD_CLOEXEC;
	d = (DIR *)malloc(sizeof(DIR) + DBLOCKSIZE*sizeof(struct dirent));
	if(d == NULL){
Nomem:
		errno = ENOMEM;
		return NULL;
	}
	d->dd_buf = (char *)d + sizeof(DIR);
	d->dd_fd = f;
	d->dd_loc = 0;
	d->dd_size = 0;
	d->dirs = NULL;
	d->dirsize = 0;
	d->dirloc = 0;
	return d;
}

int
closedir(DIR *d)
{
	int fd;

	if(d == NULL){
		errno = EBADF;
		return -1;
	}
	fd = d->dd_fd;
	free(d->dirs);
	free(d);
	return close(fd);
}

void
rewinddir(DIR *d)
{
	if(d == NULL)
		return;
	d->dd_loc = 0;
	d->dd_size = 0;
	d->dirsize = 0;
	d->dirloc = 0;
	free(d->dirs);
	d->dirs = NULL;
	if(_SEEK(d->dd_fd, 0, 0) < 0){
		_syserrno();
		return;
	}
}

struct dirent *
readdir(DIR *d)
{
	int i;
	struct dirent *dr;
	Dir *dirs, *dir;

	if(d == NULL){
		errno = EBADF;
		return NULL;
	}
	if(d->dd_loc >= d->dd_size){
		if(d->dirloc >= d->dirsize){
			free(d->dirs);
			d->dirs = NULL;
			d->dirsize = _dirread(d->dd_fd, &d->dirs);
			d->dirloc = 0;
		}
		if(d->dirsize < 0) {	/* malloc or read failed in _dirread? */
			free(d->dirs);
			d->dirs = NULL;
		}
		if(d->dirs == NULL)
			return NULL;

		dr = (struct dirent *)d->dd_buf;
		dirs = d->dirs;
		for(i=0; i<DBLOCKSIZE && d->dirloc < d->dirsize; i++){
			dir = &dirs[d->dirloc++];
			strncpy(dr[i].d_name, dir->name, MAXNAMLEN);
			dr[i].d_name[MAXNAMLEN] = 0;
			_dirtostat(&dr[i].d_stat, dir, NULL);
		}
		d->dd_loc = 0;
		d->dd_size = i*sizeof(struct dirent);
	}
	dr = (struct dirent*)(d->dd_buf+d->dd_loc);
	d->dd_loc += sizeof(struct dirent);
	return dr;
}
