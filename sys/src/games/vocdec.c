#include <u.h>
#include <libc.h>

#define GET16(p) ((u16int)(p)[0] | (u16int)(p)[1]<<8)
#define GET24(p) ((u32int)(p)[0] | (u32int)(p)[1]<<8 | (u32int)(p)[2]<<16)
#define GET32(p) ((u32int)(p)[0] | (u32int)(p)[1]<<8 | (u32int)(p)[2]<<16 | (u32int)(p)[3]<<24)

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
}

void
usage(void)
{
	fprint(2, "usage: %s", argv0);
	exits("usage");
}

Block b;

void
main(int argc, char **argv)
{
	uchar buf[8192];
	char fmt[32], size[32];
	ushort chksum, ver;
	int n;

	ARGBEGIN{
	default:
		usage();
		break;
	}ARGEND;

	if(readn(0, buf, 20+2+2+2) != 20+2+2+2 || memcmp(buf, "Creative Voice File\x1a", 20) != 0)
		sysfatal("not a voc file");

	ver = GET16(buf+20+2);
	chksum = GET16(buf+20+2+2);
	if(~ver + 0x1234 != chksum)
		sysfatal("invalid checksum");

	for(;;){
		if(readn(0, buf, 4) != 4)
			break;
		b.type = buf[0];
		b.size = GET24(buf+1);

		switch(b.type){
		case 0:
			exits(nil);
		case 1:
			if(readn(0, buf, 2) != 2)
				sysfatal("truncated block");
			b.codec = buf[0];
			b.freq = 1000000 / (256 - buf[1]);
			b.chan = 1;
			b.size -= 2;
			break;
		case 2:
			if(b.freq == 0)
				sysfatal("block 2 without defined codec");
			break;
		case 9:
			if(readn(0, buf, 4+1+1+2+4) != 4+1+1+2+4)
				sysfatal("truncated block");
			b.freq = GET32(buf);
			b.bits = buf[4];
			b.chan = buf[5];
			b.codec = buf[6];
			b.size -= 4+1+1+2+4;
			break;
		default:
			while(b.size != 0){
				n = b.size;
				if(n > sizeof buf)
					n = sizeof buf;
				if(read(0, buf, n) <= 0)
					sysfatal("truncated block");
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
	exits(nil);
}
