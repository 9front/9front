#include <unistd.h>
#include <string.h>
#include <sys/limits.h>

/*
 * BUG: instead of looking at PATH env variable,
 * just try prepending /bin/ if name fails...
 */

extern char **environ;

int
execlp(const char *name, const char *arg0, ...)
{
	int n;
	char buf[PATH_MAX];

	if((n=execve(name, &arg0, environ)) < 0){
		if(strchr("/.", name[0]) != 0 || strlen(name) >= sizeof(buf)-5)
			return n;
		strcpy(buf, "/bin/");
		strcpy(buf+5, name);
		n = execve(buf, &arg0, environ);
	}
	return n;
}
