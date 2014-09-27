#include <u.h>
#include <libc.h>

void main()
{
	int fd;
	Dir *d;
	int n, i;
	
	fd = open(".", OREAD);
	n = dirreadall(fd, &d);
	for(i = 0; i < n; i++)
		if(d[i].name[0] == '\xef')
			remove(d[i].name);
}