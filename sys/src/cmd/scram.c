#include <u.h>
#include </386/include/ureg.h>
#include <libc.h>

struct Ureg u;
int fd;

void
apm(void)
{
	seek(fd, 0, 0);
	if(write(fd, &u, sizeof u) < 0)
		sysfatal("write: %r");
	seek(fd, 0, 0);
	if(read(fd, &u, sizeof u) < 0)
		sysfatal("read: %r");
	if(u.flags & 1)
		sysfatal("apm: %lux", (u.ax>>8) & 0xFF);
}

void
main()
{
	if((fd = open("/dev/apm", ORDWR)) < 0)
		if((fd = open("#P/apm", ORDWR)) < 0)
			sysfatal("open: %r");

	u.ax = 0x530E;
	u.bx = 0x0000;
	u.cx = 0x0102;
	apm();
	u.ax = 0x5307;
	u.bx = 0x0001;
	u.cx = 0x0003;
	apm();
}
