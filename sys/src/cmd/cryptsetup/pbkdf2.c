/*	$OpenBSD: pbkdf2.c,v 1.1 2008/06/14 06:28:27 djm Exp $	*/

/*-
 * Copyright (c) 2008 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#define DS DigestState /* only to abbreviate SYNOPSIS */
#define SHA1_DIGEST_LENGTH 20
#define MIN(a,b) ((a < b) ? a : b)

/*
 * Password-Based Key Derivation Function 2 (PKCS #5 v2.0).
 * Code based on IEEE Std 802.11-2007, Annex H.4.2.
 */
int
pkcs5_pbkdf2(const unsigned char *pass, int pass_len, const unsigned char *salt, int salt_len,
    unsigned char *key, int key_len, int rounds)
{
	unsigned char *asalt, obuf[SHA1_DIGEST_LENGTH];
	unsigned char d1[SHA1_DIGEST_LENGTH], d2[SHA1_DIGEST_LENGTH];
	unsigned i, j;
	unsigned count;
	unsigned r;

	if (rounds < 1 || key_len == 0)
		return -1;
	if (salt_len == 0)
		return -1;
	if ((asalt = malloc(salt_len + 4)) == nil)
		return -1;

	memcpy(asalt, salt, salt_len);

	for (count = 1; key_len > 0; count++) {
		asalt[salt_len + 0] = (count >> 24) & 0xff;
		asalt[salt_len + 1] = (count >> 16) & 0xff;
		asalt[salt_len + 2] = (count >> 8) & 0xff;
		asalt[salt_len + 3] = count & 0xff;
		hmac_sha1(asalt, salt_len + 4, pass, pass_len, d1, nil);
		memcpy(obuf, d1, sizeof(obuf));

		for (i = 1; i < rounds; i++) {
			hmac_sha1(d1, sizeof(d1), pass, pass_len, d2, nil);
			memcpy(d1, d2, sizeof(d1));
			for (j = 0; j < sizeof(obuf); j++)
				obuf[j] ^= d1[j];
		}

		r = MIN(key_len, SHA1_DIGEST_LENGTH);
		memcpy(key, obuf, r);
		key += r;
		key_len -= r;
	};
	memset(asalt, 0, salt_len + 4);
	free(asalt);
	memset(d1, 0, sizeof(d1));
	memset(d2, 0, sizeof(d2));
	memset(obuf, 0, sizeof(obuf));

	return 0;
}
