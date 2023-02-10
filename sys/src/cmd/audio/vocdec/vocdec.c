#include <u.h>
#include <libc.h>

uchar
get(void)
{
	uchar b;

	if(read(0, &b, 1) != 1)
		sysfatal("read: %r");
	return b;
}

uint
get2(void)
{
	uchar b[2];

	if(readn(0, b, 2) != 2)
		sysfatal("read: %r");
	return (b[0]<<0) | (b[1]<<8);
}

uint
get3(void)
{
	uchar b[3];

	if(readn(0, b, 3) != 3)
		sysfatal("read: %r");
	return (b[0]<<0) | (b[1]<<8) | (b[2]<<16);
}

uint
get4(void)
{
	uchar b[4];

	if(readn(0, b, 4) != 4)
		sysfatal("read: %r");
	return (b[0]<<0) | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
}

typedef struct Block Block;
struct Block {
	uchar type;
	uint size;
	uint freq;
	uint codec;
	uchar chan;
	uint bits;
};

char*
codec(int c)
{
	switch(c){
	case 0x00:
		return "u8";
	case 0x04:
		return "s16";
	case 0x06:
		return "µ8";
	case 0x07:
		return "a8";
	default:
		sysfatal("unsupported");
	}
	return nil;
}

void
usage(void)
{
	fprint(2, "usage: %s", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	Block b;
	static uchar buf[2048];
	char fmt[32], size[32];
	ushort chksum, ver;
	int n;

	ARGBEGIN{
	default:
		usage();
		break;
	}ARGEND;

	if(readn(0, buf, 20) != 20 || memcmp(buf, "Creative Voice File\x1a", 20) != 0)
		sysfatal("not a voc file");

	get2(); /* gulp */
	ver = get2();
	chksum = get2();
	if(~ver + 0x1234 != chksum)
		sysfatal("invalid checksum");

	memset(&b, 0, sizeof b);
	for(;;){
		/* files may end without a proper block */
		if(read(0, &b.type, 1) != 1 || b.type == 0)
			break;
		b.size = get3();

		switch(b.type){
		case 1:
			b.freq = 1000000 / (256 - get());
			b.codec = get();
			b.chan = 1;
			b.size -= 2;
			break;
		case 2:
			if(b.freq == 0)
				sysfatal("block 2 without defined codec");
			break;
		case 9:
			b.freq = get4();
			b.bits = get();
			b.chan = get();
			b.codec = get2();
			get4(); /* reserved */
			b.size -= 4+1+1+2+4;
			break;
		default:
			while(b.size != 0){
				n = b.size;
				if(n > sizeof buf)
					n = sizeof buf;
				if(readn(0, buf, n) != n)
					break;
				b.size -= n;
			}
			break;
		}

		snprint(fmt, sizeof fmt, "%sc%dr%d", codec(b.codec), b.chan, b.freq);
		snprint(size, sizeof size, "%d", b.size);
		if(fork() == 0){
			execl("/bin/audio/pcmconv", "pcmconv", "-i", fmt, "-l", size, nil);
			sysfatal("exec: %r");
		}
		waitpid();
	}
}
