#include <u.h>
#include <libc.h>

typedef struct Desc Desc;
typedef struct Chan Chan;

struct Desc
{
	int	rate;
	int	channels;
	int	framesz;
	int	bits;
	int	fmt;
};

struct Chan
{
	ulong	phase;
	int	last;
};

int*
resample(Chan *c, int *src, int *dst, ulong delta, ulong count)
{
	int last, val;
	ulong phase, pos;
	vlong v;

	if(delta == 0x10000){
		/* same frequency */
		memmove(dst, src, count*sizeof(int));
		return dst + count;
	}

	val = 0;
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
		v >>= 16;
		v += last;

		/* clipping */
		if(v > 0x7fffffffLL)
			v = 0x7fffffff;
		else if(v < -0x80000000LL)
			v = -0x80000000;

		*dst++ = v;

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
siconv(int *dst, uchar *src, int bits, int skip, int count)
{
	int i, v, s, b;

	b = (bits+7)/8;
	s = sizeof(int)*8-bits;
	while(count--){
		v = 0;
		i = b;
		switch(b){
		case 4:
			v = src[--i];
		case 3:
			v = (v<<8) | src[--i];
		case 2:
			v = (v<<8) | src[--i];
		case 1:
			v = (v<<8) | src[--i];
		}
		*dst++ = v << s;
		src += skip;
	}
}

void
uiconv(int *dst, uchar *src, int bits, int skip, int count)
{
	int i, s, b;
	uint v;

	b = (bits+7)/8;
	s = sizeof(uint)*8-bits;
	while(count--){
		v = 0;
		i = b;
		switch(b){
		case 4:
			v = src[--i];
		case 3:
			v = (v<<8) | src[--i];
		case 2:
			v = (v<<8) | src[--i];
		case 1:
			v = (v<<8) | src[--i];
		}
		*dst++ = (v << s) - (~0UL>>1);
		src += skip;
	}
}

void
ficonv(int *dst, uchar *src, int bits, int skip, int count)
{
	while(count--){
		if(bits == 32)
			*dst++ = *((float*)src) * 2147483647.f;
		else
			*dst++ = *((double*)src) * 2147483647.f;
		src += skip;
	}
}

void
soconv(int *src, uchar *dst, int bits, int skip, int count)
{
	int i, v, s, b;

	b = (bits+7)/8;
	s = sizeof(int)*8-bits;
	while(count--){
		v = *src++ >> s;
		i = 0;
		switch(b){
		case 4:
			dst[i++] = v, v >>= 8;
		case 3:
			dst[i++] = v, v >>= 8;
		case 2:
			dst[i++] = v, v >>= 8;
		case 1:
			dst[i] = v;
		}
		dst += skip;
	}
}

void
uoconv(int *src, uchar *dst, int bits, int skip, int count)
{
	int i, s, b;
	uint v;

	b = (bits+7)/8;
	s = sizeof(uint)*8-bits;
	while(count--){
		v = ((~0UL>>1) + *src++) >> s;
		i = 0;
		switch(b){
		case 4:
			dst[i++] = v, v >>= 8;
		case 3:
			dst[i++] = v, v >>= 8;
		case 2:
			dst[i++] = v, v >>= 8;
		case 1:
			dst[i] = v;
		}
		dst += skip;
	}
}

void
foconv(int *src, uchar *dst, int bits, int skip, int count)
{
	while(count--){
		if(bits == 32)
			*((float*)dst) = *src++ / 2147483647.f;
		else
			*((double*)dst) = *src++ / 2147483647.f;
		dst += skip;
	}
}

Desc
mkdesc(char *f)
{
	Desc d;
	int c;
	char *p;

	memset(&d, 0, sizeof(d));
	p = f;
	while(c = *p++){
		switch(c){
		case 'r':
			d.rate = strtol(p, &p, 10);
			break;
		case 'c':
			d.channels = strtol(p, &p, 10);
			break;
		case 's':
		case 'u':
		case 'f':
			d.fmt = c;
			d.bits = strtol(p, &p, 10);
			break;
		default:
			goto Bad;
		}
	}
	if(d.rate <= 0)
		goto Bad;
	if(d.fmt == 'f'){
		if(d.bits != 32 && d.bits != 64)
			goto Bad;
	} else if(d.bits <= 0 || d.bits > 32)
		goto Bad;
	d.framesz = ((d.bits+7)/8) * d.channels;
	if(d.framesz <= 0)
		goto Bad;
	return d;
Bad:
	sysfatal("bad format: %s", f);
	return d;
}

void
usage(void)
{
	fprint(2, "usage: %s [-i fmt] [-o fmt] [-l length]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	uchar ibuf[8*1024], *obuf;
	int *out, *in;
	Chan ch[8];
	Desc i, o;
	ulong delta;
	int k, r, n, m;
	vlong l;

	void (*oconv)(int *, uchar *, int, int, int) = nil;
	void (*iconv)(int *, uchar *, int, int, int) = nil;

	o = mkdesc("s16c2r44100");
	i = o;
	l = -1LL;
	ARGBEGIN {
	case 'i':
		i = mkdesc(EARGF(usage()));
		break;
	case 'o':
		o = mkdesc(EARGF(usage()));
		break;
	case 'l':
		l = atoll(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND;

	switch(i.fmt){
	case 's': iconv = siconv; break;
	case 'u': iconv = uiconv; break;
	case 'f': iconv = ficonv; break;
	}

	switch(o.fmt){
	case 's': oconv = soconv; break;
	case 'u': oconv = uoconv; break;
	case 'f': oconv = foconv; break;
	}

	delta = ((uvlong)i.rate << 16) / o.rate;
	memset(ch, 0, sizeof(ch));
	n = (sizeof(ibuf)-i.framesz)/i.framesz;
	r = n*i.framesz;
	m = (i.rate + n*o.rate)/i.rate;
	in = sbrk(sizeof(int) * n);
	out = sbrk(sizeof(int) * m);
	obuf = sbrk(o.framesz * m);
	if(in == nil || out == nil || obuf == nil)
		sysfatal("out of memory");

	for(;;){
		if(l >= 0 && l < r)
			r = l;
		n = read(0, ibuf, r);
		if(n < 0)
			sysfatal("read: %r");
		if(n == 0)
			break;
		if(l > 0)
			l -= n;
		n /= i.framesz;
		(*iconv)(in, ibuf, i.bits, i.framesz, n);
		m = resample(&ch[0], in, out, delta, n) - out;
		if(m < 1)
			continue;
		(*oconv)(out, obuf, o.bits, o.framesz, m);
		if(i.channels == o.channels){
			for(k=1; k<i.channels; k++){
				(*iconv)(in, ibuf + k*((i.bits+7)/8), i.bits, i.framesz, n);
				resample(&ch[k], in, out, delta, n);
				(*oconv)(out, obuf + k*((o.bits+7)/8), o.bits, o.framesz, m);
			}
		} else {
			for(k=1; k<o.channels; k++)
				(*oconv)(out, obuf + k*((o.bits+7)/8), o.bits, o.framesz, m);
		}
		m *= o.framesz;
		write(1, obuf, m);
	}
	exits(0);
}
