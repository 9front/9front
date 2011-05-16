#include <u.h>
#include </386/include/ureg.h>
typedef struct Ureg Ureg;
#include <libc.h>

void
main()
{
	Ureg ureg;
	int fd;
	
	fd = open("/dev/apm", OWRITE);
	if(fd < 0) sysfatal("%r");
	memset(&ureg, 0, sizeof ureg);
	ureg.ax = 0x5307;
	ureg.bx = 0x0001;
	ureg.cx = 0x0003;
	ureg.trap = 0x15;
	write(fd, &ureg, sizeof ureg);
}
