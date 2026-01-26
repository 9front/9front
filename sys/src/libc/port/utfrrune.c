#include <u.h>
#include <libc.h>

char*
utfrrune(char *s, long c)
{
	Rune r;
	int n;
	char *p;
	char buf[UTFmax + 1] = {0};

	if(c < Runesync)		/* not part of utf sequence */
		return strrchr(s, c);

	r = c;
	n = runetochar(buf, &r);
	p = nil;
	while(s = strstr(s, buf)){
		p = s;
		s += n;
	}
	return p;
}
