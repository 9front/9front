#include <stdlib.h>
#include <string.h>

void *
calloc(size_t n, size_t s)
{
	void *v;

	if(n > 1 && ((size_t)-1)/n < s)
		return 0;
	n *= s;
	if(v = malloc(n))
		memset(v, 0, n);
	return v;
}
