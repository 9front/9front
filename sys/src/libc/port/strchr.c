#include <u.h>
#include <libc.h>

char*
strchr(char *s, int c)
{
	char r;

	if(c == 0)
		while(*s++)
			;
	else
		while((r = *s++) != c)
			if(r == 0)
				return 0;
	return s-1;
}
