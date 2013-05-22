#include "lib.h"
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "sys9.h"
#include "dir.h"

int
access(const char *name, int mode)
{
	int fd;
	Dir *db;
	struct stat st;
	static char omode[] = {
		0,
		3,
		1,
		2,
		0,
		2,
		2,
		2
	};
	if(mode == 0){
		db = _dirstat(name);
		if(db == nil){
			_syserrno();
			return -1;
		}
		free(db);
		return 0;
	}
	fd = open(name, omode[mode&7]);
	if(fd >= 0){
		close(fd);
		return 0;
	}
	else if(stat(name, &st)==0 && S_ISDIR(st.st_mode)){
		if(mode & (R_OK|X_OK)){
			fd = open(name, O_RDONLY);
			if(fd < 0)
				return -1;
			close(fd);
		}
		if(mode & W_OK){
			char *tname;
			int nname;
			nname = strlen(name);
			tname = malloc(nname+32);
			if(tname == 0)
				return -1;
			memset(tname, 0, nname+32);
			memcpy(tname, name, nname);
			memcpy(tname+nname, "/_AcChAcK", 9);
			_ultoa(tname+nname+9, getpid());
			fd = _CREATE(tname, ORCLOSE, 0666);
			if(fd < 0){
				_syserrno();
				free(tname);
				return -1;
			}
			_CLOSE(fd);
			free(tname);
		}
		return 0;
	}
	return -1;
}
