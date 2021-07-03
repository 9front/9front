#include <u.h>
#include <libc.h>

#define between(x,min,max)	(((min-1-x) & (x-max-1))>>8)

int
enc64chr(int o)
{
	int c;

	c  = between(o,  0, 25) & ('A'+o);
	c |= between(o, 26, 51) & ('a'+(o-26));
	c |= between(o, 52, 61) & ('0'+(o-52));
	c |= between(o, 62, 62) & ('+');
	c |= between(o, 63, 63) & ('/');
	return c;
}

int
dec64chr(int c)
{
	int o;

	o  = between(c, 'A', 'Z') & (1+(c-'A'));
	o |= between(c, 'a', 'z') & (1+26+(c-'a'));
	o |= between(c, '0', '9') & (1+52+(c-'0'));
	o |= between(c, '+', '+') & (1+62);
	o |= between(c, '/', '/') & (1+63);
	return o-1;
}

int
dec64x(uchar *out, int lim, char *in, int n, int (*chr)(int))
{
	ulong b24;
	uchar *start = out;
	uchar *e = out + lim;
	int i, c;

	b24 = 0;
	i = 0;
	while(n-- > 0){
		c = chr(*in++);
		if(c < 0)
			continue;
		switch(i){
		case 0:
			b24 = c<<18;
			break;
		case 1:
			b24 |= c<<12;
			break;
		case 2:
			b24 |= c<<6;
			break;
		case 3:
			if(out + 3 > e)
				goto exhausted;

			b24 |= c;
			*out++ = b24>>16;
			*out++ = b24>>8;
			*out++ = b24;
			i = 0;
			continue;
		}
		i++;
	}
	switch(i){
	case 2:
		if(out + 1 > e)
			goto exhausted;
		*out++ = b24>>16;
		break;
	case 3:
		if(out + 2 > e)
			goto exhausted;
		*out++ = b24>>16;
		*out++ = b24>>8;
		break;
	}
exhausted:
	return out - start;
}

int
enc64x(char *out, int lim, uchar *in, int n, int (*chr)(int))
{
	int i;
	ulong b24;
	char *start = out;
	char *e = out + lim;

	for(i = n/3; i > 0; i--){
		b24 = *in++<<16;
		b24 |= *in++<<8;
		b24 |= *in++;
		if(out + 4 >= e)
			goto exhausted;
		*out++ = chr(b24>>18);
		*out++ = chr((b24>>12)&0x3f);
		*out++ = chr((b24>>6)&0x3f);
		*out++ = chr(b24&0x3f);
	}

	switch(n%3){
	case 2:
		b24 = *in++<<16;
		b24 |= *in<<8;
		if(out + 4 >= e)
			goto exhausted;
		*out++ = chr(b24>>18);
		*out++ = chr((b24>>12)&0x3f);
		*out++ = chr((b24>>6)&0x3f);
		*out++ = '=';
		break;
	case 1:
		b24 = *in<<16;
		if(out + 4 >= e)
			goto exhausted;
		*out++ = chr(b24>>18);
		*out++ = chr((b24>>12)&0x3f);
		*out++ = '=';
		*out++ = '=';
		break;
	}
exhausted:
	*out = 0;
	return out - start;
}

int
enc64(char *out, int lim, uchar *in, int n)
{
	return enc64x(out, lim, in, n, enc64chr);
}

int
dec64(uchar *out, int lim, char *in, int n)
{
	return dec64x(out, lim, in, n, dec64chr);
}
