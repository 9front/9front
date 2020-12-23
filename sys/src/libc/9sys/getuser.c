#include <u.h>
#include <libc.h>

char *
getuser(void)
{
	static char user[64];
	char name[32];
	Dir *dir;

	snprint(name, sizeof(name), "/proc/%lud/status", (ulong)getpid());
	if((dir = dirstat(name)) == nil)
		return "none";
	snprint(user, sizeof(user), "%s", dir->uid);
	free(dir);
	return user;
}
