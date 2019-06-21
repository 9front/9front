#include <stdlib.h>

extern	char **environ;

int
putenv(const char *str)
{
	char *s1, *s2, **e;
	int n;

	for(n = 0; s2 = environ[n]; n++)
		for(s1 = (char *)str; *s1 == *s2; s1++, s2++)
			if(*s1 == '\0' || *s1 == '='){
				environ[n] = (char*)str;
				return 0;
			}
	e = realloc(environ, (n+1) * sizeof(char*));
	if(e == 0)
		return -1;
	environ = e;
	e[n++] = (char*)str;
	e[n] = 0;
	return 0;
}
