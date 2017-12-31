#include <u.h>
#include <libc.h>

#define between(x,min,max)	(((min-1-x) & (x-max-1))>>8)

int
enc16chr(int o)
{
	int c;

	c  = between(o,  0,  9) & ('0'+o);
	c |= between(o, 10, 15) & ('A'+(o-10));
	return c;
}

int
dec16chr(int c)
{
	int o;

	o  = between(c, '0', '9') & (1+(c-'0'));
	o |= between(c, 'A', 'F') & (1+10+(c-'A'));
	o |= between(c, 'a', 'f') & (1+10+(c-'a'));
	return o-1;
}

int
dec16(uchar *out, int lim, char *in, int n)
{
	int c, w = 0, i = 0;
	uchar *start = out;
	uchar *eout = out + lim;

	while(n-- > 0){
		c = dec16chr(*in++);
		if(c < 0)
			continue;
		w = (w<<4) + c;
		i++;
		if(i == 2){
			if(out + 1 > eout)
				goto exhausted;
			*out++ = w;
			w = 0;
			i = 0;
		}
	}
exhausted:
	return out - start;
}

int
enc16(char *out, int lim, uchar *in, int n)
{
	uint c;
	char *eout = out + lim;
	char *start = out;

	while(n-- > 0){
		c = *in++;
		if(out + 2 >= eout)
			goto exhausted;
		*out++ = enc16chr(c>>4);
		*out++ = enc16chr(c&15);
	}
exhausted:
	*out = 0;
	return out - start;
}
