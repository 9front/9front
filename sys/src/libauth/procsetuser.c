#include <u.h>
#include <libc.h>
#include <auth.h>

int
procsetuser(char *user)
{
	int fd, n;

	fd = open("#c/user", OWRITE|OCEXEC);
	if(fd < 0)
		return -1;
	n = strlen(user);
	if(write(fd, user, n) != n){
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}
