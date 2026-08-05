#include <u.h>
#include <libc.h>
#include <libsec.h>

/*
 * Encodes input (ulong long) into output (uchar).
 * Assumes len is a multiple of 8.
 */
static void
encode64(uchar *output, u64int *input, ulong len)
{
	u64int x;
	uchar *e;

	for(e = output + len; output < e;) {
		x = *input++;
		*output++ = x;
		*output++ = x >> 8;
		*output++ = x >> 16;
		*output++ = x >> 24;
		*output++ = x >> 32;
		*output++ = x >> 40;
		*output++ = x >> 48;
		*output++ = x >> 56;
	}
}

extern void _keccakfblock(u64int s[25], int rsize, uchar *in, int len);

static DigestState*
sha3_x(uchar *p, ulong len, uchar *digest, DigestState *s, int dlen, XOFState *xof)
{
	int i;
	int rsize;
	uchar buf[256];

	rsize = 200 - 2 * dlen;
	/* fill out the partial rsize byte block from previous calls */
	if(s->blen){
		i = rsize - s->blen;
		if(len < i)
			i = len;
		memmove(s->buf + s->blen, p, i);
		len -= i;
		s->blen += i;
		p += i;
		if(s->blen == rsize){
			_keccakfblock(s->bstate, rsize, s->buf, s->blen);
			s->len += s->blen;
			s->blen = 0;
		}
	}

	/* do rsize byte blocks */
	i = len/rsize;
	if(i){
		i *= rsize;
		_keccakfblock(s->bstate, rsize, p, i);
		s->len += i;
		len -= i;
		p += i;
	}

	/* save the left overs if not last call */
	if(xof == nil && digest == 0){
		if(len){
			memmove(s->buf, p, len);
			s->blen += len;
		}
		return s;
	}

	/*
	 * Last time through, do padding.
	 * We need to pad even if we're exactly at a word boundary.
	 */
	if(s->blen){
		p = s->buf;
		len = s->blen;
	} else {
		memmove(buf, p, len);
		p = buf;
	}
	s->len += len;
	p[rsize - 1] = 0;
	if(xof)
		p[len++] = 0x1F;
	else
		p[len++] = 0x06;
	p[rsize - 1] ^= 0x80;
	if(rsize != len)
		memset(p + len, 0, rsize - len - 1);
	_keccakfblock(s->bstate, rsize, p, rsize);

	if(xof)
		memcpy(xof->bstate, s->bstate, sizeof s->bstate);
	else
		encode64(digest, s->bstate, dlen);
	if(s->malloced == 1)
		free(s);
	return nil;
}

static void
shake_x(uchar *out, ulong n, XOFState *s, int dlen)
{
	static uchar empty[256] = { 0 };
	int i, rsize;
	ulong rem;
	uvlong x;

	if(out == nil){
		if(s->malloced == 1)
			free(s);
		return;
	}

	rsize = 200 - 2 * dlen;
	if(s->offset % rsize != 0){
		for(; s->offset % rsize != 0 && n > 0; s->offset++, n--){
			rem = s->offset % rsize;
			*out++ = s->bstate[rem / 8] >> ((rem % 8) * 8);
		}
		if(s->offset % rsize == 0)
			_keccakfblock(s->bstate, rsize, empty, rsize);
	}
	s->offset += n;

	while(n >= rsize){
		encode64(out, s->bstate, rsize);
		_keccakfblock(s->bstate, rsize, empty, rsize);
		n -= rsize;
		out += rsize;
	}

	i = n / 8;
	x = s->bstate[i];
	if(i){
		i *= 8;
		encode64(out, s->bstate, i);
		n -= i;
		out += i;
	}
	for(i = 0; i < n; i++)
		*out++ = (x >> (i*8));
}

DigestState*
sha3_224(uchar *p, ulong len, uchar *digest, DigestState *s)
{
	if(s == nil){
		s = mallocz(sizeof(*s), 1);
		if(s == nil)
			return nil;
		s->malloced = 1;
	}
	return sha3_x(p, len, digest, s, SHA3_224dlen, nil);
}

DigestState*
sha3_256(uchar *p, ulong len, uchar *digest, DigestState *s)
{
	if(s == nil){
		s = mallocz(sizeof(*s), 1);
		if(s == nil)
			return nil;
		s->malloced = 1;
	}
	return sha3_x(p, len, digest, s, SHA3_256dlen, nil);
}

DigestState*
sha3_384(uchar *p, ulong len, uchar *digest, DigestState *s)
{
	if(s == nil){
		s = mallocz(sizeof(*s), 1);
		if(s == nil)
			return nil;
		s->malloced = 1;
	}
	return sha3_x(p, len, digest, s, SHA3_384dlen, nil);
}

DigestState*
sha3_512(uchar *p, ulong len, uchar *digest, DigestState *s)
{
	if(s == nil){
		s = mallocz(sizeof(*s), 1);
		if(s == nil)
			return nil;
		s->malloced = 1;
	}
	return sha3_x(p, len, digest, s, SHA3_512dlen, nil);
}

DigestState*
shake_128_in(uchar *p, ulong len, DigestState *s)
{
	if(s == nil){
		s = mallocz(sizeof(*s), 1);
		if(s == nil)
			return nil;
		s->malloced = 1;
	}
	return sha3_x(p, len, nil, s, SHAKE_128dlen, nil);
}

XOFState*
shake_128_conv(XOFState *x, DigestState *d)
{
	if(x == nil){
		x = mallocz(sizeof(*x), 1);
		if(x == nil)
			return nil;
		x->malloced = 1;
	}
	sha3_x(nil, 0, nil, d, SHAKE_128dlen, x);
	return x;
}

void
shake_128_out(uchar *out, ulong len, XOFState *x)
{
	shake_x(out, len, x, SHAKE_128dlen);
}

void
shake_128(uchar *p, ulong len, uchar *digest, ulong olen)
{
	XOFState x = { 0 };
	DigestState d = { 0 };

	sha3_x(p, len, nil, &d, SHAKE_128dlen, &x);
	shake_x(digest, olen, &x, SHAKE_128dlen);
}

DigestState*
shake_256_in(uchar *p, ulong len, DigestState *s)
{
	if(s == nil){
		s = mallocz(sizeof(*s), 1);
		if(s == nil)
			return nil;
		s->malloced = 1;
	}
	return sha3_x(p, len, nil, s, SHAKE_256dlen, nil);
}

XOFState*
shake_256_conv(XOFState *x, DigestState *d)
{
	if(x == nil){
		x = mallocz(sizeof(*x), 1);
		if(x == nil)
			return nil;
		x->malloced = 1;
	}
	sha3_x(nil, 0, nil, d, SHAKE_256dlen, x);
	return x;
}

void
shake_256_out(uchar *out, ulong len, XOFState *x)
{
	shake_x(out, len, x, SHAKE_256dlen);
}

void
shake_256(uchar *p, ulong len, uchar *digest, ulong olen)
{
	XOFState x = { 0 };
	DigestState d = { 0 };

	sha3_x(p, len, nil, &d, SHAKE_256dlen, &x);
	shake_x(digest, olen, &x, SHAKE_256dlen);
}

DigestState*
hmac_sha3_224(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest,
	DigestState *s)
{
	return hmac_x(p, len, key, klen, digest, s, sha3_224, SHA3_224dlen, 144);
}

DigestState*
hmac_sha3_256(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest,
	DigestState *s)
{
	return hmac_x(p, len, key, klen, digest, s, sha3_256, SHA3_256dlen, 136);
}

DigestState*
hmac_sha3_384(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest,
	DigestState *s)
{
	return hmac_x(p, len, key, klen, digest, s, sha3_384, SHA3_384dlen, 104);
}

DigestState*
hmac_sha3_512(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest,
	DigestState *s)
{
	return hmac_x(p, len, key, klen, digest, s, sha3_512, SHA3_512dlen, 72);
}
