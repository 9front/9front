#include <u.h>

/*
 * returns 0 if the the len bytes in x are equal to len bytes in y,
 * otherwise returns -1.
 */
int
constcmp(uchar *x, uchar *y, int len)
{
	uint z;
	int i;

	for(z = 0, i = 0; i < len; i++) {
		z |= x[i] ^ y[i];
	}

	return (1 & ((z - 1) >> 8)) - 1;
}
