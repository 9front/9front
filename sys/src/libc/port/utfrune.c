#include <u.h>
#include <libc.h>

char*
utfrune(char *s, long c)
{
	Rune r;
	char buf[UTFmax + 1] = {0};

	if(c < Runesync)		/* not part of utf sequence */
		return strchr(s, c);

	r = c;
	runetochar(buf, &r);
	return strstr(s, buf);
}
