#include <u.h>
#include <libc.h>
#include <draw.h>

/* Connect us to new window, if possible */
int
newwindow(char *str)
{
	int fd;
	char *wsys;
	char buf[256];

	wsys = getenv("wsys");
	if(wsys == nil)
		return -1;
	fd = open(wsys, ORDWR);
	if(fd < 0){
		free(wsys);
		return -1;
	}
	rfork(RFNAMEG);
	unmount(wsys, "/dev");	/* drop reference to old window */
	free(wsys);
	if(str)
		snprint(buf, sizeof buf, "new %s", str);
	else
		strcpy(buf, "new");
	if(mount(fd, -1, "/mnt/wsys", MREPL, buf) < 0)
		return mount(fd, -1, "/dev", MBEFORE, buf);
	return bind("/mnt/wsys", "/dev", MBEFORE);
}

