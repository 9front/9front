#include <string.h>
#include <ctype.h>
#include <stdlib.h>

char*
strndup(char *p, size_t max)
{
	int n;
	char *np;

	n = strnlen(p, max);
	np = malloc(n+1);
	if(!np)
		return NULL;
	memcpy(np, p, n);
	np[n] = 0;
	return np;
}
