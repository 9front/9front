#include <u.h>
#include <libc.h>

ulong
get4(void)
{
	uchar buf[4];

	if(readn(0, buf, 4) != 4)
		sysfatal("read: %r");
	return buf[0]<<24 | buf[1]<<16 | buf[2]<<8 | buf[3];
}

char *fmttab[] = {
	[1] "µ8",	/* 8-bit G.711 µ-law */
	[2] "S8",	/* 8-bit linear PCM */
	[3] "S16",	/* 16-bit linear PCM */
	[4] "S24",	/* 24-bit linear PCM */
	[5] "S32",	/* 32-bit linear PCM */
	[6] "f32",	/* 32-bit IEEE floating point */
	[7] "f64",	/* 64-bit IEEE floating point */
	[27] "a8",	/* 8-bit G.711 A-law */
};

void
main(int, char *argv[])
{
	char buf[64], fmt[32];
	ulong enc, rate, chans, len, off;
	int n;

	argv0 = argv[0];
	if(get4() != 0x2e736e64UL)
		sysfatal("no sun format");
	off = get4();
	if(off < 24)
		sysfatal("bad data ofset");
	off -= 24;
	len = get4();
	if(len == 0xffffffffUL)
		len = 0;
	enc = get4();
	rate = get4();
	chans = get4();
	if(enc >= nelem(fmttab) || fmttab[enc] == 0)
		sysfatal("unsupported encoding: %lux", enc);
	snprint(fmt, sizeof(fmt), "%sc%ludr%lud", fmttab[enc], chans, rate);
	while(off > 0){
		n = sizeof(buf);
		if(off < n)
			n = off;
		n = read(0, buf, n);
		if(n <= 0)
			sysfatal("read: %r");
		off -= n;
	}
	if(len > 0){
		snprint(buf, sizeof(buf), "%lud", len);
		execl("/bin/audio/pcmconv", "pcmconv", "-i", fmt, "-l", buf, nil);
	} else
		execl("/bin/audio/pcmconv", "pcmconv", "-i", fmt, nil);
}
