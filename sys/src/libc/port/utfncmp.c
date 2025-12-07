#include <u.h>
#include <libc.h>

int
utfncmp(char *s1, char *s2, long n)
{
	Rune r1, r2;

	for(; n > 0; n--){
		s1 += chartorune(&r1, s1);
		s2 += chartorune(&r2, s2);
		if(r1 != r2){
			if(r1 > r2)
				return 1;
			return -1;
		}
		if(r1 == 0)
			break;
	}
	return 0;
}
