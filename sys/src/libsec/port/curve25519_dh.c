#include "os.h"
#include <mp.h>
#include <libsec.h>

static uchar nine[32] = {9};
static uchar zero[32] = {0};

void
curve25519_dh_new(uchar x[32], uchar y[32])
{
	uchar b;

	/* new public/private key pair */
	genrandom(x, 32);
	b = x[31];
	x[0] &= ~7;			/* clear bit 0,1,2 */
	x[31] = 0x40 | (b & 0x7f);	/* set bit 254, clear bit 255 */
	curve25519(y, x, nine);

	/* bit 255 is always 0, so make it random */
	y[31] |= b & 0x80;
}

int
curve25519_dh_finish(uchar x[32], uchar y[32], uchar z[32])
{
	/* remove the random bit */
	y[31] &= 0x7f;

	/* calculate dhx key */
	curve25519(z, x, y);

	memset(x, 0, 32);
	memset(y, 0, 32);

	return tsmemcmp(z, zero, 32) != 0;
}
