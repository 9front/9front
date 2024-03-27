#include <u.h>
#include <libc.h>
#include <pcm.h>

typedef struct Chan Chan;
typedef struct Pcmconv Pcmconv;

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

struct Pcmconv
{
	Pcmdesc	idesc;
	Pcmdesc	odesc;
	void	(*iconv)(int *, uchar *, int, int, int);
	void	(*oconv)(int *, uchar *, int, int, int);
	int	*in;
	int	*out;
	uchar	*buf;
	int	nbuf;
	ulong	prnd;
	int	flags;
	Chan	ch[];
};

enum {
	Nl	= 8,		/* 2^Nl samples per zero crossing in fir */
	Nη	= 8,		/* phase bits for filter interpolation */
	Np	= Nl+Nη,	/* phase bits (fract of fixed point) */
	One	= 1<<Np,
	Fcopy	= 1<<0,
	Nin	= 2048,
};

#define MAXINT	((int)(~0UL>>1))
#define MININT	(MAXINT+1)

#define clip(v)	((v) > MAXINT ? MAXINT : ((v) < MININT ? MININT : (v)))

static int
chaninit(Chan *c, int irate, int orate, int count, uintptr caller)
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
		for(n=0; n<nelem(hΔ)-1; n++)
			hΔ[n] = h[n+1] - h[n];
		init = 1;
	}
	c->h = h;
	c->hΔ = hΔ;
	c->ix = c->wx;
	c->t = c->ix<<Np;
	c->nx = c->wx*2 + count;
	c->x = mallocz(sizeof(c->x[0]) * c->nx, 1);
	if(c->x == nil){
		werrstr("memory");
		return -1;
	}
	setmalloctag(c->x, caller);
	count += c->nx; /* account for buffer accumulation */
Done:
	return ((uvlong)count * c->ρ) >> Np;
}

static int
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

static int*
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

static ulong
dither(int *y, int ibits, int obits, int count, ulong prnd)
{
	if(ibits >= 32 || obits >= ibits)
		return prnd;

	while(count--){
		prnd = (prnd*0x19660dL + 0x3c6ef35fL) & 0xffffffffL;
		*y = clip((vlong)*y + ((int)prnd >> ibits));
		y++;
	}
	return prnd;
}

static void
mixin(int *y, int *x, int count)
{
	while(count--){
		*y = clip((vlong)*y + *x++);
		y++;
	}
}

static void
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

static void
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

static void
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

static void
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

static void
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

static void
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

static void
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

static void
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

static void
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

static void
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

static void
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

static void
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

Pcmconv	*
allocpcmconv(Pcmdesc *i, Pcmdesc *o)
{
	uintptr caller;
	Pcmconv *c;
	int k, nout;

	if(i->channels < 1){
		werrstr("invalid number of input channels: %d", i->channels);
		return nil;
	}
	if(o->channels < 1){
		werrstr("invalid number of output channels: %d", o->channels);
		return nil;
	}

	caller = getcallerpc(&i);
	c = mallocz(sizeof(*c) + i->channels*sizeof(Chan) + i->framesz, 1);
	if(c == nil){
Nomem:
		werrstr("memory");
		goto Err;
	}
	setmalloctag(c, caller);
	memcpy(&c->idesc, i, sizeof(*i));
	memcpy(&c->odesc, o, sizeof(*o));
	c->buf = (uchar*)&c->ch[i->channels+1];

	switch(i->fmt){
	case L's': c->iconv = siconv; break;
	case L'S': c->iconv = Siconv; break;
	case L'u': c->iconv = uiconv; break;
	case L'U': c->iconv = Uiconv; break;
	case L'f': c->iconv = ficonv; break;
	case L'a': c->iconv = aiconv; break;
	case L'µ': c->iconv = µiconv; break;
	default:
		werrstr("unsupported input format: %C", i->fmt);
		goto Err;
	}

	switch(o->fmt){
	case L's': c->oconv = soconv; break;
	case L'S': c->oconv = Soconv; break;
	case L'u': c->oconv = uoconv; break;
	case L'U': c->oconv = Uoconv; break;
	case L'f': c->oconv = foconv; break;
	default:
		werrstr("unsupported output format: %C", o->fmt);
		goto Err;
	}

	/* if same format, just copy */
	c->flags = (
		i->rate == o->rate
		&& i->bits == o->bits
		&& i->channels == o->channels
		&& i->framesz == o->framesz
		&& i->fmt == o->fmt
	) ? Fcopy : 0;

	c->in = malloc(sizeof(int) * Nin);
	if(c->in == nil)
		goto Nomem;
	setmalloctag(c->in, caller);

	nout = 0;
	for(k=0; k < i->channels; k++){
		nout = chaninit(&c->ch[k], i->rate, o->rate, Nin, caller);
		if(nout < 0)
			goto Err;
	}

	/* out is also used for mixing before resampling, so needs to be max(Nin, nout) */
	c->out = malloc(sizeof(int) * (nout > Nin ? nout : Nin));
	if(c->out == nil)
		goto Nomem;
	setmalloctag(c->out, caller);

	return c;
Err:
	freepcmconv(c);
	return nil;
}

void
freepcmconv(Pcmconv *c)
{
	int k;

	if(c == nil)
		return;
	free(c->in);
	free(c->out);
	for(k=0; k < c->idesc.channels; k++)
		free(c->ch[k].x);
	free(c);
}

static int
conv(Pcmconv *c, uchar *in, uchar *out0, int insz)
{
	uchar *out;
	Pcmdesc *i, *o;
	int k, n, m, nin;

	i = &c->idesc;
	o = &c->odesc;
	out = out0;

	nin = insz / i->framesz;
	for(;;){
		if((n = nin) > Nin)
			n = Nin;
		nin -= n;

		c->iconv(c->in, in, i->bits, i->framesz, n);
		if(i->channels > o->channels){
			for(k=1; k<i->channels; k++){
				c->iconv(c->out, in + k*((i->bits+7)/8), i->bits, i->framesz, n);
				mixin(c->in, c->out, n);
			}
		}
		c->prnd = dither(c->in, i->abits, o->abits, n, c->prnd);
		m = resample(&c->ch[0], c->in, c->out, n) - c->out;
		if(m < 1){
			if(n == 0)
				break;
		} else
			c->oconv(c->out, out, o->bits, o->framesz, m);
		if(i->channels == o->channels){
			for(k=1; k<i->channels; k++){
				c->iconv(c->in, in + k*((i->bits+7)/8), i->bits, i->framesz, n);
				c->prnd = dither(c->in, i->abits, o->abits, n, c->prnd);
				resample(&c->ch[k], c->in, c->out, n);
				if(m > 0)
					c->oconv(c->out, out + k*((o->bits+7)/8), o->bits, o->framesz, m);
			}
		} else if(m > 0){
			for(k=1; k<o->channels; k++)
				c->oconv(c->out, out + k*((o->bits+7)/8), o->bits, o->framesz, m);
		}
		if(m > 0)
			out += m * o->framesz;
		if(n == 0)
			break;
		in += n * i->framesz;
	}

	return out - (uchar*)out0;
}

int
pcmconv(Pcmconv *c, void *in0, void *out0, int insz)
{
	uchar *in, *out;
	Pcmdesc *i;
	int n;

	if(c->flags & Fcopy){
		memmove(out0, in0, insz);
		return insz;
	}
	i = &c->idesc;
	in = in0;
	out = out0;
	if(c->nbuf > 0){
		n = i->framesz - c->nbuf;
		if(n > insz)
			n = insz;
		memcpy(c->buf+c->nbuf, in, n);
		c->nbuf += n;
		in += n;
		insz -= n;
		if(c->nbuf < i->framesz)
			return 0;
		out += conv(c, c->buf, out, i->framesz);
		c->nbuf = 0;
	}

	n = insz % i->framesz;
	insz -= n;
	out += conv(c, in, out, insz);
	if(n > 0){
		memcpy(c->buf, in+insz, n);
		c->nbuf += n;
	}

	return out - (uchar*)out0;
}

int
pcmratio(Pcmconv *c, int insz)
{
	int outsz;

	if(insz < c->idesc.framesz)
		goto Bad;
	insz /= c->idesc.framesz;
	outsz = ((uvlong)insz * ((uvlong)c->odesc.rate<<Np)/c->idesc.rate) >> Np;
	if(outsz > 1)
		return outsz;
Bad:
	werrstr("invalid buffer size: %d", insz);
	return -1;
}
