#include <u.h>
#include <libc.h>
#include <auth.h>

int
procsetuser(char *user)
{
	char name[32];
	Dir dir;

	nulldir(&dir);
	dir.uid = user;
	snprint(name, sizeof(name), "/proc/%lud/ctl", (ulong)getpid());
	if(dirwstat(name, &dir) < 0){
		/*
		 * this is backwards compatibility code as
		 * devproc initially didnt allow changing
		 * the user to none.
		 */
		int fd;

		if(strcmp(user, "none") != 0)
			return -1;
		fd = open("#c/user", OWRITE|OCEXEC);
		if(fd < 0)
			return -1;
		if(write(fd, "none", 4) != 4){
			close(fd);
			return -1;
		}
		close(fd);
	}
	return 0;
}
