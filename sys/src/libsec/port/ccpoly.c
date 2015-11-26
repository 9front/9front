#include <u.h>
#include <libc.h>
#include <libsec.h>

static void
ccpolyotk(Chachastate *cs, DigestState *ds)
{
	uchar otk[ChachaBsize];

	memset(ds, 0, sizeof(*ds));
	memset(otk, 0, 32);
	chacha_setblock(cs, 0);
	chacha_encrypt(otk, ChachaBsize, cs);
	poly1305(nil, 0, otk, 32, nil, ds);
}

static void
ccpolymac(uchar *buf, ulong nbuf, DigestState *ds)
{
	static uchar zeros[16] = {0};
	ulong npad;

	if(nbuf == 0)
		return;
	poly1305(buf, nbuf, nil, 0, nil, ds);
	npad = nbuf % 16;
	if(npad == 0)
		return;
	poly1305(zeros, 16 - npad, nil, 0, nil, ds);
}

static void
ccpolytag(ulong ndat, ulong naad, uchar tag[16], DigestState *ds)
{
	uchar info[16];

	info[0] = naad;
	info[1] = naad>>8;
	info[2] = naad>>16;
	info[3] = naad>>24;
	info[4] = 0;
	info[5] = 0;
	info[6] = 0;
	info[7] = 0;

	info[8]  = ndat;
	info[9]  = ndat>>8;
	info[10] = ndat>>16;
	info[11] = ndat>>24;
	info[12] = 0;
	info[13] = 0;
	info[14] = 0;
	info[15] = 0;

	poly1305(info, 16, nil, 0, tag, ds);
}

void
ccpoly_encrypt(uchar *dat, ulong ndat, uchar *aad, ulong naad, uchar tag[16], Chachastate *cs)
{
	DigestState ds;

	ccpolyotk(cs, &ds);
	ccpolymac(aad, naad, &ds);
	chacha_encrypt(dat, ndat, cs);
	ccpolymac(dat, ndat, &ds);
	ccpolytag(ndat, naad, tag, &ds);
}

int
ccpoly_decrypt(uchar *dat, ulong ndat, uchar *aad, ulong naad, uchar tag[16], Chachastate *cs)
{
	DigestState ds;
	uchar tmp[16];

	ccpolyotk(cs, &ds);
	ccpolymac(aad, naad, &ds);
	ccpolymac(dat, ndat, &ds);
	ccpolytag(ndat, naad, tmp, &ds);
	if(tsmemcmp(tag, tmp, 16) != 0)
		return -1;
	chacha_encrypt(dat, ndat, cs);
	return 0;
}
