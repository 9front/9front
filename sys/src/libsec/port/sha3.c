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
sha3_x(uchar *p, ulong len, uchar *digest, DigestState *s, int dlen, ulong xoflen)
{
	int i;
	int rsize;
	uchar buf[256];
	u64int x;

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
	if(digest == 0){
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
	if(xoflen)
		p[len++] = 0x1F;
	else
		p[len++] = 0x06;
	p[rsize - 1] ^= 0x80;
	if(rsize != len)
		memset(p + len, 0, rsize - len - 1);
	_keccakfblock(s->bstate, rsize, p, rsize);

	if(xoflen){
		memset(buf, 0, rsize);
		while(xoflen >= rsize){
			encode64(digest, s->bstate, rsize);
			_keccakfblock(s->bstate, rsize, buf, rsize);
			xoflen -= rsize;
			digest += rsize;
		}

		i = xoflen / 8;
		x = s->bstate[i];
		if(i){
			i *= 8;
			encode64(digest, s->bstate, i);
			xoflen -= i;
			digest += i;
		}
		for(i = 0; i < xoflen; i++)
			*digest++ = (x >> (i*8));
	} else
		encode64(digest, s->bstate, dlen);
	if(s->malloced == 1)
		free(s);
	return nil;
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
	return sha3_x(p, len, digest, s, SHA3_224dlen, 0);
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
	return sha3_x(p, len, digest, s, SHA3_256dlen, 0);
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
	return sha3_x(p, len, digest, s, SHA3_384dlen, 0);
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
	return sha3_x(p, len, digest, s, SHA3_512dlen, 0);
}

DigestState*
shake_128(uchar *p, ulong len, uchar *digest, ulong xoflen, DigestState *s)
{
	if(s == nil){
		s = mallocz(sizeof(*s), 1);
		if(s == nil)
			return nil;
		s->malloced = 1;
	}
	return sha3_x(p, len, digest, s, SHAKE_128dlen, xoflen);
}

DigestState*
shake_256(uchar *p, ulong len, uchar *digest, ulong xoflen, DigestState *s)
{
	if(s == nil){
		s = mallocz(sizeof(*s), 1);
		if(s == nil)
			return nil;
		s->malloced = 1;
	}
	return sha3_x(p, len, digest, s, SHAKE_256dlen, xoflen);
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
