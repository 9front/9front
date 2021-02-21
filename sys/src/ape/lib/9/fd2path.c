#include <lib9.h>

extern	int	_FD2PATH(int, char*, int);

int
fd2path(int fd, char *buf, int nbuf)
{
	return _FD2PATH(fd, buf, nbuf);
}
