#include <u.h>
#include <libc.h>

typedef struct Wave Wave;
struct Wave
{
	int	rate;
	int	channels;
	int	framesz;
	int	bits;
	int	fmt;
};

ulong
get2(void)
{
	uchar buf[2];

	if(readn(0, buf, 2) != 2)
		sysfatal("read: %r");
	return buf[0] | buf[1]<<8;
}

ulong
get4(void)
{
	uchar buf[4];

	if(readn(0, buf, 4) != 4)
		sysfatal("read: %r");
	return buf[0] | buf[1]<<8 | buf[2]<<16 | buf[3]<<24;
}

char*
getcc(char tag[4])
{
	if(readn(0, tag, 4) != 4)
		sysfatal("read: %r");
	return tag;
}

void
usage(void)
{
	fprint(2, "usage: %s [ -s SECONDS ]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char buf[1024], fmt[32];
	double seekto;
	ulong len, n;
	Wave wav;

	seekto = 0.0;
	ARGBEGIN{
	case 's':
		seekto = atof(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND

	if(memcmp(getcc(buf), "RIFF", 4) != 0)
		sysfatal("no riff format");
	get4();
	if(memcmp(getcc(buf), "WAVE", 4) != 0)
		sysfatal("not a wave file");

	for(;;){
		getcc(buf);
		len = get4();
		if(memcmp(buf, "data", 4) == 0)
			break;
		if(memcmp(buf, "fmt ", 4) == 0){
			if(len < 2+2+4+4+2+2)
				sysfatal("format chunk too small");
			wav.fmt = get2();
			wav.channels = get2();
			wav.rate = get4();
			get4();
			wav.framesz = get2();
			wav.bits = get2();
			len -= 2+2+4+4+2+2;
		}
		while(len > 0){
			n = sizeof(buf);
			if(len < n)
				n = len;
			n = read(0, buf, n);
			if(n <= 0)
				sysfatal("read: %r");
			len -= n;
		}
	}
	switch(wav.fmt){
	case 1:
		snprint(fmt, sizeof(fmt), "%c%dr%dc%d", wav.bits == 8 ? 'u' : 's',
			wav.bits, wav.rate, wav.channels);
		break;
	case 3:
		snprint(fmt, sizeof(fmt), "f32r%dc%d", wav.rate, wav.channels);
		break;
	case 6:
		snprint(fmt, sizeof(fmt), "a8r%dc%d", wav.rate, wav.channels);
		break;
	case 7:
		snprint(fmt, sizeof(fmt), "µ8r%dc%d", wav.rate, wav.channels);
		break;
	default:
		sysfatal("wave format (0x%lux) not supported", (ulong)wav.fmt);
	}
	if(seekto != 0.0){
		if(seek(0, (ulong)seekto*wav.rate*wav.framesz & ~wav.framesz, 1) < 1)
			seekto = 0.0;
		fprint(2, "time: %g\n", seekto);
	}
	snprint(buf, sizeof(buf), "%lud", len);
	execl("/bin/audio/pcmconv", "pcmconv", "-i", fmt, "-l", buf, nil);
}
