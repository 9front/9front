#include <unistd.h>
#include <errno.h>

int
chroot(const char*)
{
	errno = EIO;
	return -1;
}
