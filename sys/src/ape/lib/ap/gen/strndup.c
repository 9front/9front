#include <string.h>
#include <ctype.h>
#include <stdlib.h>

char*
strndup(char *p, size_t max)
{
	int n;
	char *np;

	n = strlen(p)+1;
	if(n > max)
		n = max+1;
	np = malloc(n);
	if(!np)
		return nil;
	memmove(np, p, n);
	np[n-1] = 0;
	return np;
}
