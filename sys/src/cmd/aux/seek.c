#include <u.h>
#include <libc.h>

void
main(int argc, char **argv)
{
	char buf[512];
	vlong size;
	vlong pos;
	vlong ns;
	int fd;
	int i;

	if(argc != 2) {
		fprint(2, "usage: %s /dev/sd??/data\n", argv[0]);
		exits("usage");
	}
	
	srand(time(0));
	fd = open(argv[1], OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	size = seek(fd, 0, 2) / 512;
	ns = nsec();
	for(i=0;i<100;i++) {
		pos = (vlong)(frand() * size);
		if(pread(fd, buf, 512, 512 * pos) < 512)
			sysfatal("read: %r");
	}
	ns = nsec() - ns;
	print("%.3g\n", ((double)ns)/100000000);
	exits(nil);
}
