#include <u.h>
#include <libc.h>

typedef struct Desc Desc;
typedef struct Chan Chan;

struct Desc
{
	int	rate;
	int	channels;
	int	framesz;
	int	abits;	/* bits after input conversion */
	int	bits;	/* bits in input stream per sample */
	Rune	fmt;
};

struct Chan
{
	ulong	ρ;	/* factor */

	ulong	t;	/* time */

	ulong	tΔ;	/* output step */
	ulong	lΔ;	/* filter step */
	ulong	le;	/* filter end */

	int	u;	/* unity scale */
	int	*h;	/* filter coefficients */
	int	*hΔ;	/* coefficient deltas for interpolation */

	int	wx;	/* extra samples */
	int	ix;	/* buffer index */
	int	nx;	/* buffer size */
	int	*x;	/* the buffer */
};

enum {
	Nl	= 8,		/* 2^Nl samples per zero crossing in fir */
	Nη	= 8,		/* phase bits for filter interpolation */
	Np	= Nl+Nη,	/* phase bits (fract of fixed point) */
	One	= 1<<Np,
};

#define MAXINT	((int)(~0UL>>1))
#define MININT	(MAXINT+1)

int
clip(vlong v)
{
	if(v > MAXINT)
		return MAXINT;
	if(v < MININT)
		return MININT;
	return v;
}

int
chaninit(Chan *c, int irate, int orate, int count)
{
	static int h[] = {
#include "fir.h"
	};
	static int hΔ[nelem(h)], init = 0;
	int n;

	c->ρ = ((uvlong)orate<<Np)/irate;
	if(c->ρ == One)
		goto Done;

	c->tΔ = ((uvlong)irate<<Np)/orate;
	c->lΔ = 1<<(Nl+Nη);
	c->le = nelem(h)<<Nη;
	c->wx = 1 + (c->le / c->lΔ);
	c->u = 13128;	/* unity scale factor for fir */
	if(c->ρ < One){
		c->u *= c->ρ;
		c->u >>= Np;
		c->lΔ *= c->ρ;
		c->lΔ >>= Np;
		c->wx *= c->tΔ;
		c->wx >>= Np;
	}
	if(!init){
		init = 1;
		for(n=0; n<nelem(hΔ)-1; n++)
			hΔ[n] = h[n+1] - h[n];
	}
	c->h = h;
	c->hΔ = hΔ;
	c->ix = c->wx;
	c->t = c->ix<<Np;
	c->nx = c->wx*2 + count;
	c->x = sbrk(sizeof(c->x[0]) * c->nx);
	count += c->nx; /* account for buffer accumulation */
Done:
	return ((uvlong)count * c->ρ) >> Np;
}

int
filter(Chan *c)
{
	ulong l, lΔ, le, p, i;
	int *x, *h, *hΔ, a;
	vlong v;

	v = 0;

	h = c->h;
	hΔ = c->hΔ;
	lΔ = c->lΔ;
	le = c->le;

	/* left side */
	x = &c->x[c->t>>Np];
	p = c->t & ((1<<Np)-1);
	l = c->ρ < One ? (c->ρ * p)>>Np : p;
	while(l < le){
		i = l >> Nη;
		a = l & ((1<<Nη)-1);
		l += lΔ;
		a *= hΔ[i];
		a >>= Nη;
		a += h[i];
		v += (vlong)*(--x) * a;
	}

	/* right side */
	x = &c->x[c->t>>Np];
	p = (One - p) & ((1<<Np)-1);
	l = c->ρ < One ? (c->ρ * p)>>Np : p;
	if(p == 0) /* skip h[0] as it was already been summed above if p == 0 */
		l += c->lΔ;
	while(l < le){
		i = l >> Nη;
		a = l & ((1<<Nη)-1);
		l += lΔ;
		a *= hΔ[i];
		a >>= Nη;
		a += h[i];
		v += (vlong)*x++ * a;
	}

	/* scale */
	v >>= 2;
	v *= c->u;
	v >>= 27;

	return clip(v);
}

int*
resample(Chan *c, int *x, int *y, int count)
{
	ulong e;
	int i, n;

	if(c->ρ == One){
		/* no rate conversion needed */
		if(count > 0)
			memmove(y, x, count*sizeof(int));
		return y+count;
	}

	if(count == 0){
		/* stuff zeros to drain last samples out */
		while(c->ix < 2*c->wx)
			c->x[c->ix++] = 0;
	}

	do {
		/* fill buffer */
		for(i = c->ix, n = c->nx; count > 0 && i < n; count--, i++)
			c->x[i] = *x++;
		c->ix = i;

		/* need at least 2*wx samples in buffer for filter */
		if(i < 2*c->wx)
			break;

		/* process buffer */
		e = (i - c->wx)<<Np;
		while(c->t < e){
			*y++ = filter(c);
			c->t += c->tΔ;
		}

		/* check if we'r at the end of buffer and wrap it */
		e = c->t >> Np;
		if(e >= (c->nx - c->wx)){
			c->t -= e << Np;
			c->t += c->wx << Np;
			n = c->t >> Np;
			n -= c->wx;
			e -= c->wx;
			i -= e;
			if(i > 0){
				memmove(c->x + n, c->x + e, i*sizeof(int));
				n += i;
			}
			c->ix = n;
		}
	} while(count > 0);

	return y;
}

void
dither(int *y, int ibits, int obits, int count)
{
	static ulong prnd;

	if(ibits >= 32 || obits >= ibits)
		return;

	while(count--){
		prnd = (prnd*0x19660dL + 0x3c6ef35fL) & 0xffffffffL;
		*y = clip((vlong)*y + ((int)prnd >> ibits));
		y++;
	}
}

void
mixin(int *y, int *x, int count)
{
	while(count--){
		*y = clip((vlong)*y + *x++);
		y++;
	}
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
Siconv(int *dst, uchar *src, int bits, int skip, int count)
{
	int i, v, s, b;

	b = (bits+7)/8;
	s = sizeof(int)*8-bits;
	while(count--){
		v = 0;
		i = 0;
		switch(b){
		case 4:
			v = src[i++];
		case 3:
			v = (v<<8) | src[i++];
		case 2:
			v = (v<<8) | src[i++];
		case 1:
			v = (v<<8) | src[i];
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
Uiconv(int *dst, uchar *src, int bits, int skip, int count)
{
	int i, s, b;
	uint v;

	b = (bits+7)/8;
	s = sizeof(uint)*8-bits;
	while(count--){
		v = 0;
		i = 0;
		switch(b){
		case 4:
			v = src[i++];
		case 3:
			v = (v<<8) | src[i++];
		case 2:
			v = (v<<8) | src[i++];
		case 1:
			v = (v<<8) | src[i];
		}
		*dst++ = (v << s) - (~0UL>>1);
		src += skip;
	}
}

void
ficonv(int *dst, uchar *src, int bits, int skip, int count)
{
	if(bits == 32){
		while(count--){
			float f;

			f = *((float*)src), src += skip;
			if(f > 1.0)
				*dst++ = MAXINT;
			else if(f < -1.0)
				*dst++ = MININT;
			else
				*dst++ = f*((float)MAXINT);
		}
	} else {
		while(count--){
			double d;

			d = *((double*)src), src += skip;
			if(d > 1.0)
				*dst++ = MAXINT;
			else if(d < -1.0)
				*dst++ = MININT;
			else
				*dst++ = d*((double)MAXINT);
		}
	}
}

void
aiconv(int *dst, uchar *src, int, int skip, int count)
{
	int t, seg;
	uchar a;

	while(count--){
		a = *src, src += skip;
		a ^= 0x55;
		t = (a & 0xf) << 4;
		seg = (a & 0x70) >> 4;
		switch(seg){
		case 0:
			t += 8;
			break;
		case 1:
			t += 0x108;
			break;
		default:
			t += 0x108;
			t <<= seg - 1;
		}
		t = (a & 0x80) ? t : -t;
		*dst++ = t << (sizeof(int)*8 - 16);
	}
}

void
µiconv(int *dst, uchar *src, int, int skip, int count)
{
	int t;
	uchar u;

	while(count--){
		u = *src, src += skip;
		u = ~u;
		t = ((u & 0xf) << 3) + 0x84;
		t <<= (u & 0x70) >> 4;
		t = u & 0x80 ? 0x84 - t: t - 0x84;
		*dst++ = t << (sizeof(int)*8 - 16);
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
Soconv(int *src, uchar *dst, int bits, int skip, int count)
{
	int i, v, s, b;

	b = (bits+7)/8;
	s = sizeof(int)*8-bits;
	while(count--){
		v = *src++ >> s;
		i = b;
		switch(b){
		case 4:
			dst[--i] = v, v >>= 8;
		case 3:
			dst[--i] = v, v >>= 8;
		case 2:
			dst[--i] = v, v >>= 8;
		case 1:
			dst[--i] = v;
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
Uoconv(int *src, uchar *dst, int bits, int skip, int count)
{
	int i, s, b;
	uint v;

	b = (bits+7)/8;
	s = sizeof(uint)*8-bits;
	while(count--){
		v = ((~0UL>>1) + *src++) >> s;
		i = b;
		switch(b){
		case 4:
			dst[--i] = v, v >>= 8;
		case 3:
			dst[--i] = v, v >>= 8;
		case 2:
			dst[--i] = v, v >>= 8;
		case 1:
			dst[--i] = v;
		}
		dst += skip;
	}
}

void
foconv(int *src, uchar *dst, int bits, int skip, int count)
{
	if(bits == 32){
		while(count--){
			*((float*)dst) = *src++ / ((float)MAXINT);
			dst += skip;
		}
	} else {
		while(count--){
			*((double*)dst) = *src++ / ((double)MAXINT);
			dst += skip;
		}
	}
}

Desc
mkdesc(char *f)
{
	Desc d;
	Rune r;
	char *p;

	memset(&d, 0, sizeof(d));
	p = f;
	while(*p != 0){
		p += chartorune(&r, p);
		switch(r){
		case L'r':
			d.rate = strtol(p, &p, 10);
			break;
		case L'c':
			d.channels = strtol(p, &p, 10);
			break;
		case L'm':
			r = L'µ';
		case L's':
		case L'S':
		case L'u':
		case L'U':
		case L'f':
		case L'a':
		case L'µ':
			d.fmt = r;
			d.bits = d.abits = strtol(p, &p, 10);
			break;
		default:
			goto Bad;
		}
	}
	if(d.rate <= 0)
		goto Bad;
	if(d.fmt == L'a' || d.fmt == L'µ'){
		if(d.bits != 8)
			goto Bad;
		d.abits = 16;
	} else if(d.fmt == L'f'){
		if(d.bits != 32 && d.bits != 64)
			goto Bad;
		d.abits = sizeof(int)*8;
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

int
cread(int fd, uchar *buf, int len, int mod)
{
	uchar *off = buf;

	len -= (len % mod);
Again:
	len = read(fd, off, len);
	if(len <= 0)
		return len;
	off += len;
	len = off - buf;
	if((len % mod) != 0){
		len = mod - (len % mod);
		goto Again;
	}
	return len;
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
	int k, n, m, nin, nout;
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

	/* check if same format */
	if(i.rate == o.rate
	&& i.bits == o.bits
	&& i.channels == o.channels
	&& i.framesz == o.framesz
	&& i.fmt == o.fmt){
		while((n = read(0, ibuf, sizeof(ibuf))) > 0){
			if(write(1, ibuf, n) != n)
				sysfatal("write: %r");
		}
		if(n < 0)
			sysfatal("read: %r");
		exits(0);
	}

	if(i.channels > nelem(ch))
		sysfatal("too many input channels: %d", i.channels);

	switch(i.fmt){
	case L's': iconv = siconv; break;
	case L'S': iconv = Siconv; break;
	case L'u': iconv = uiconv; break;
	case L'U': iconv = Uiconv; break;
	case L'f': iconv = ficonv; break;
	case L'a': iconv = aiconv; break;
	case L'µ': iconv = µiconv; break;
	default:
		sysfatal("unsupported input format: %C", i.fmt);
	}

	switch(o.fmt){
	case L's': oconv = soconv; break;
	case L'S': oconv = Soconv; break;
	case L'u': oconv = uoconv; break;
	case L'U': oconv = Uoconv; break;
	case L'f': oconv = foconv; break;
	default:
		sysfatal("unsupported output format: %C", o.fmt);
	}

	if(i.fmt == L'f' || o.fmt == L'f')
		setfcr(getfcr() & ~(FPINVAL|FPOVFL));

	nin = (sizeof(ibuf)-i.framesz)/i.framesz;
	in = sbrk(sizeof(int) * nin);

	nout = 0;
	memset(ch, 0, sizeof(ch));
	for(k=0; k < i.channels; k++)
		nout = chaninit(&ch[k], i.rate, o.rate, nin);

	/* out is also used for mixing before resampling, so needs to be max(nin, nout) */
	out = sbrk(sizeof(int) * (nout>nin? nout: nin));
	obuf = sbrk(o.framesz * nout);

	for(;;){
		n = nin * i.framesz;
		if(l >= 0 && l < n)
			n = l;
		n = cread(0, ibuf, n, i.framesz);
		if(n < 0)
			sysfatal("read: %r");
		if(l > 0)
			l -= n;
		n /= i.framesz;
		(*iconv)(in, ibuf, i.bits, i.framesz, n);
		if(i.channels > o.channels){
			for(k=1; k<i.channels; k++){
				(*iconv)(out, ibuf + k*((i.bits+7)/8), i.bits, i.framesz, n);
				mixin(in, out, n);
			}
		}
		dither(in, i.abits, o.abits, n);
		m = resample(&ch[0], in, out, n) - out;
		if(m < 1){
			if(n == 0)
				break;
		} else
			(*oconv)(out, obuf, o.bits, o.framesz, m);
		if(i.channels == o.channels){
			for(k=1; k<i.channels; k++){
				(*iconv)(in, ibuf + k*((i.bits+7)/8), i.bits, i.framesz, n);
				dither(in, i.abits, o.abits, n);
				resample(&ch[k], in, out, n);
				if(m > 0)
					(*oconv)(out, obuf + k*((o.bits+7)/8), o.bits, o.framesz, m);
			}
		} else if(m > 0){
			for(k=1; k<o.channels; k++)
				(*oconv)(out, obuf + k*((o.bits+7)/8), o.bits, o.framesz, m);
		}
		if(m > 0){
			m *= o.framesz;
			if(write(1, obuf, m) != m)
				sysfatal("write: %r");
		}
		if(n == 0)
			break;
	}
	exits(0);
}
