#include "common.h"

static uchar flagtab[] = {
	'a',	Fanswered,
	'D',	Fdeleted,
	'd',	Fdraft,
	'f',	Fflagged,
	'r',	Frecent,
	's',	Fseen,
	'S',	Fstored,
};

char*
flagbuf(char *buf, int flags)
{
	char *p, c;
	int i;

	p = buf;
	for(i = 0; i < nelem(flagtab); i += 2){
		c = '-';
		if(flags & flagtab[i+1])
			c = flagtab[i];
		*p++ = c;
	}
	*p = 0;
	return buf;
}

int
buftoflags(char *p)
{
	uchar flags;
	int i;

	flags = 0;
	for(i = 0; i < nelem(flagtab); i += 2)
		if(p[i>>1] == flagtab[i])
			flags |= flagtab[i + 1];
	return flags;
}

char*
txflags(char *p, uchar *flags)
{
	uchar neg, f, c, i;

	for(;;){
		neg = 0;
	again:
		if((c = *p++) == '-'){
			neg = 1;
			goto again;
		}else if(c == '+'){
			neg = 0;
			goto again;	
		}
		if(c == 0)
			return nil;
		for(i = 0;; i += 2){
			if(i == nelem(flagtab))
				return "bad flag";
			if(c == flagtab[i]){
				f = flagtab[i+1];
				break;
			}
		}
		if(neg)
			*flags &= ~f;
		else
			*flags |= f;
	}
}
