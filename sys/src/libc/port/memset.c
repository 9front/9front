#include	<u.h>
#include	<libc.h>

void*
memset(void *ap, int c, usize n)
{
	char *p;

	p = ap;
	while(n > 0) {
		*p++ = c;
		n--;
	}
	return ap;
}
