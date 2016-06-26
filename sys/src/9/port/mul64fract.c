#include <u.h>

/* mul64fract(uvlong*r, uvlong a, uvlong b)
 *
 * Multiply two 64 numbers and return the middle 64 bits of the 128 bit result.
 *
 * The assumption is that one of the numbers is a 
 * fixed point number with the integer portion in the
 * high word and the fraction in the low word.
 *
 * There should be an assembler version of this routine
 * for each architecture.  This one is intended to
 * make ports easier.
 *
 *	ignored		r0 = lo(a0*b0)
 *	lsw of result	r1 = hi(a0*b0) +lo(a0*b1) +lo(a1*b0)
 *	msw of result	r2 = 		hi(a0*b1) +hi(a1*b0) +lo(a1*b1)
 *	ignored		r3 = hi(a1*b1)
 */

void
mul64fract(uvlong *r, uvlong a, uvlong b)
{
	ulong bh, bl, ah, al;

	bl = b;
	bh = b >> 32;
	al = a;
	ah = a >> 32;

	*r = (((uvlong)al*(uvlong)bl)>>32)
	   + ((uvlong)al*(uvlong)bh)
	   + ((uvlong)ah*(uvlong)bl)
	   + (((uvlong)ah*(uvlong)bh)<<32);
}
