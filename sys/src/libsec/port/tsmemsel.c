#include "os.h"
#include <libsec.h>

void
tsmemsel(void *dst, void *src1, void *src2, ulong len, ulong cond)
{
	uchar *d, *s1, *s2;

	d = dst;
	s1 = src1;
	s2 = src2;

	// clamp to 0 or 1
	cond = (cond | -cond) >> 31;
	// sign extend either 0 or -1 for a mask
	cond = -cond;

	assert((((s1 < d) & (s1+len > d)) | ((s2 < d) & (s2+len > d))) == 0);

	for(; len != 0; d++, s1++, s2++, len--)
		*d = *s1 ^ (cond & (*s2 ^ *s1));
}
