#include <u.h>
#include <libc.h>

int
putenv(char *name, char *val)
{
	char ename[100];
	int f, n;

	if(name[0]=='\0' || strcmp(name, ".")==0 || strcmp(name, "..")==0 || strchr(name, '/')!=nil
	|| strlen(name) >= sizeof(ename)-5){
		werrstr("bad env name: %s", name);
		return -1;
	}
	snprint(ename, sizeof(ename), "/env/%s", name);
	f = create(ename, OWRITE, 0664);
	if(f < 0)
		return -1;
	n = strlen(val);
	if(write(f, val, n) != n){
		close(f);
		return -1;
	}
	close(f);
	return 0;
}
