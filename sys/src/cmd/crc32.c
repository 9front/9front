#include <u.h>
#include <libc.h>
#include <libsec.h>

u32int table[256];
u32int xor = -1, init, poly = 0xedb88320;
char *error;

static void
dotable(void)
{
	u32int c;
	int n, k;

	for(n = 0; n < 256; n++){
		c = n;
		for(k = 0; k < 8; k++)
			if((c & 1) != 0)
				c = poly ^ c >> 1;
			else
				c >>= 1;
		table[n] = c;
	}
}

static u32int
calc(u32int init, uchar *buf, ulong len)
{
	u32int c;
	
	c = init;
	while(len-- != 0)
		c = table[(*buf++ ^ c) & 0xff] ^ c >> 8;
	return c;
}

static void
sum(int fd, char *name)
{
	int n;
	uchar buf[IOUNIT];
	u32int crc;

	crc = init ^ xor;
	while((n = read(fd, buf, sizeof buf)) > 0)
		crc = calc(crc, buf, n);
	if(n < 0){
		fprint(2, "reading %s: %r\n", name ? name : "stdin");
		error = "read";
		return;
	}
	crc ^= xor;
	if(name == nil)
		print("%ux\n", crc);
	else
		print("%ux\t%s\n", crc, name);
}

void
usage(void)
{
	fprint(2, "usage: %s [-x xorval] [-i initial] [-p poly] [file...]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int i, fd;

	ARGBEGIN{
	case 'x':
		xor = strtol(EARGF(usage()), 0, 0);
		break;
	case 'i':
		init = strtol(EARGF(usage()), 0, 0);
		break;
	case 'p':
		poly = strtol(EARGF(usage()), 0, 0);
		break;
	default:
		usage();
	}ARGEND

	dotable();
	if(argc == 0)
		sum(0, nil);
	else for(i = 0; i < argc; i++){
		fd = open(argv[i], OREAD);
		if(fd < 0){
			fprint(2, "crc32: can't open %s: %r\n", argv[i]);
			error = "open";
			continue;
		}
		sum(fd, argv[i]);
		close(fd);
	}
	exits(error);
}
