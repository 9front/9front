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
	uchar buf[20];
	char fmt[32], size[32];
	ushort chksum, ver;

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

	for(;;){
		b.type = get();
		if(b.type == 0)
			break;
		b.size = get3();

		switch(b.type){
		case 1:
			b.freq = 1000000 / (256 - get());
			b.codec = get();
			b.chan = 1;
			b.size -= 2;
			break;
		case 9:
			b.freq = get4();
			b.bits = get();
			b.chan = get();
			b.codec = get2();
			get(); /* reserved */
			b.size -= 4+1+1+2+1;
			break;
		default:
			sysfatal("unsupported blocktype");
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
