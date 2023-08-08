#include <u.h>
#include <libc.h>
#include <fcall.h>

enum{
	Hdrsz = 2+2+4+16,
};

void
main(int argc, char **argv)
{
	int n, rate, fd;
	uchar buf[Hdrsz], *p;
	char fmt[16], len[16];

	fd = 0;
	if(argc > 1 && (fd = open(argv[1], OREAD)) < 0)
		sysfatal("open: %r");
	if(read(fd, buf, sizeof buf) != sizeof buf)
		sysfatal("short read: %r");
	p = buf;
	n = GBIT16(p);
	if(n != 3)			/* format number */
		sysfatal("invalid dmx file");
	p += 2;
	rate = GBIT16(p);	/* sample rate (usually 11025Hz) */
	p += 2;
	n = GBIT32(p);
	snprint(fmt, sizeof fmt, "u8c1r%d", rate);
	snprint(len, sizeof len, "%d", n);
	execl("/bin/audio/pcmconv", "pcmconv", "-i", fmt, "-o", "s16c2r44100", "-l", len, nil);
	sysfatal("execl: %r");
}
