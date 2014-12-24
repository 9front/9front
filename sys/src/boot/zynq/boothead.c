#include <u.h>
#include <libc.h>

char *data;
uchar head[0x8c0];

void
usage(void)
{
	fprint(2, "usage: %s file\n", argv0);
	exits("usage");
}

void
u32(int n, u32int p)
{
	head[n] = p;
	head[n+1] = p >> 8;
	head[n+2] = p >> 16;
	head[n+3] = p >> 24;
}

u32int
gu32(int n)
{
	return head[n] | head[n+1] << 8 | head[n+2] << 16 | head[n+3] << 24;
}

void
main(int argc, char **argv)
{
	int fd, sz, i;
	u32int ck;

	ARGBEGIN {
	default:
		usage();
	} ARGEND;

	if(argc != 1)
		usage();
	fd = open(argv[0], OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	sz = seek(fd, 0, 2);
	if(sz < 0)
		sysfatal("seek: %r");
	data = malloc(sz);
	if(data == nil)
		sysfatal("malloc: %r");
	seek(fd, 0, 0);
	if(readn(fd, data, sz) < sz)
		sysfatal("read: %r");
	close(fd);
	memset(head, 0, sizeof(head));
	
	u32(0x20, 0xaa995566);
	u32(0x24, 0x584C4E58);
	u32(0x30, sizeof(head));
	u32(0x34, sz);
	u32(0x40, sz);
	ck = 0;
	for(i = 0x20; i < 0x48; i += 4)
		ck += gu32(i);
	u32(0x48, ~ck);
	u32(0xa0, -1);
	
	write(1, head, sizeof(head));
	write(1, data, sz);
	exits(nil);
}
