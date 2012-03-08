#include <u.h>
#include <libc.h>

int debug = 0;
int rate = 44100;

typedef struct Wave Wave;
typedef struct Chan Chan;

struct Chan
{
	ulong	phase;
	int	last;
};

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

uchar*
getcc(uchar tag[4])
{
	if(readn(0, tag, 4) != 4)
		sysfatal("read: %r");
	return tag;
}

uchar*
resample(Chan *c, int *src, uchar *dst, int mono, ulong delta, ulong count)
{
	int last, val, out;
	ulong phase, pos;
	vlong v;

	last = c->last;
	phase = c->phase;
	pos = phase >> 16;
	while(pos < count){
		val = src[pos];
		if(pos)
			last = src[pos-1];

		/* interpolate */
		v = val;
		v -= last;
		v *= (phase & 0xFFFF);
		out = (last + (v >> 16)) >> (sizeof(int)*8 - 16);

		*dst++ = out;
		*dst++ = out >> 8;
		if(mono){
			*dst++ = out;
			*dst++ = out >> 8;
		} else
			dst += 2;
		phase += delta;
		pos = phase >> 16;
	}
	c->last = val;
	if(delta < 0x10000)
		c->phase = phase & 0xFFFF;
	else
		c->phase = phase - (count << 16);

	return dst;
}

void
conv(int *dst, uchar *src, int bits, int skip, int n)
{
	int i, v;

	while(n--){
		if(bits == 8)
			v = (int)src[0] - 127;
		else {
			v = 0;
			switch(i = bits/8){
			case 4:
				v = src[--i];
			case 3:
				v = (v<<8) | src[--i];
			case 2:
				v = (v<<8) | src[--i];
			case 1:
				v = (v<<8) | src[--i];
			}
		}
		v <<= sizeof(int)*8-bits;
		*dst++ = v;
		src += skip;
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [ -d ]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	uchar buf[8*1024], *out, *p;
	int *samples;
	Chan c0, c1;
	Wave wav;
	ulong delta, len;
	int n, z;

	ARGBEGIN {
	case 'd':
		debug++;
		break;
	default:
		usage();
	} ARGEND;

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
			if(len < sizeof(buf))
				n = len;
			else
				n = sizeof(buf);
			if(readn(0, buf, n) != n)
				sysfatal("read: %r");
			len -= n;
		}
	}

	if(wav.fmt != 1)
		sysfatal("compressed format (0x%x) not supported", wav.fmt);
	if(wav.framesz <= 0 || wav.bits <= 0 || wav.framesz != wav.channels*wav.bits/8)
		sysfatal("bad format");
	if(debug)
		fprint(2, "wave: PCM %d Hz, %d ch, %d bits\n",
			wav.rate, wav.channels, wav.bits);

	delta = (wav.rate << 16) / rate;
	n = sizeof(buf)/wav.framesz;
	samples = malloc(sizeof(int) * n);
	out = malloc(4 * ((wav.rate + n*rate)/wav.rate));
	if(samples == nil || out == nil)
		sysfatal("out of memory");

	while(len % wav.framesz)
		--len;
	while(len){
		if(len < sizeof(buf))
			n = len;
		else
			n = sizeof(buf);
		while(n % wav.framesz)
			--n;
		if(readn(0, buf, n) != n)
			sysfatal("read: %r");
		len -= n;
		n /= wav.framesz;
		if(wav.channels == 2){
			conv(samples, buf + wav.bits/8, wav.bits, wav.framesz, n);
			resample(&c1, samples, out+2, 0, delta, n);
		}
		conv(samples, buf, wav.bits, wav.framesz, n);
		p = resample(&c0, samples, out, wav.channels == 1, delta, n);
		write(1, out, p-out);
	}
	exits(0);
}
