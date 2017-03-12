#include "imap4d.h"

/* not compatable with characters outside the basic plane */

/*
 * modified utf-7, as per imap4 spec
 * like utf-7, but substitues , for / in base 64,
 * does not allow escaped ascii characters.
 *
 * /lib/rfc/rfc2152 is utf-7
 * /lib/rfc/rfc1642 is obsolete utf-7
 *
 * test sequences from rfc1642
 *	'A≢Α.'		'A&ImIDkQ-.'
 *	'Hi Mom ☺!"	'Hi Mom &Jjo-!'
 *	'日本語'		'&ZeVnLIqe-'
 */

static uchar mt64d[256];
static char mt64e[64];

static void
initm64(void)
{
	int c, i;

	memset(mt64d, 255, 256);
	memset(mt64e, '=', 64);
	i = 0;
	for(c = 'A'; c <= 'Z'; c++){
		mt64e[i] = c;
		mt64d[c] = i++;
	}
	for(c = 'a'; c <= 'z'; c++){
		mt64e[i] = c;
		mt64d[c] = i++;
	}
	for(c = '0'; c <= '9'; c++){
		mt64e[i] = c;
		mt64d[c] = i++;
	}
	mt64e[i] = '+';
	mt64d['+'] = i++;
	mt64e[i] = ',';
	mt64d[','] = i;
}

char*
encmutf7(char *out, int lim, char *in)
{
	char *start, *e;
	int nb;
	ulong r, b;
	Rune rr;

	start = out;
	e = out + lim;
	if(mt64e[0] == 0)
		initm64();
	if(in)
	for(;;){
		r = *(uchar*)in;

		if(r < ' ' || r >= Runeself){
			if(r == 0)
				break;
			if(out + 1 >= e)
				return 0;
			*out++ = '&';
			b = 0;
			nb = 0;
			for(;;){
				in += chartorune(&rr, in);
				r = rr;
				if(r == 0 || r >= ' ' && r < Runeself)
					break;
				b = (b << 16) | r;
				for(nb += 16; nb >= 6; nb -= 6){
					if(out + 1 >= e)
						return 0;
					*out++ = mt64e[(b >> nb - 6) & 0x3f];
				}
			}
			for(; nb >= 6; nb -= 6){
				if(out + 1 >= e)
					return 0;
				*out++ = mt64e[(b >> nb - 6) & 0x3f];
			}
			if(nb){
				if(out + 1 >= e)
					return 0;
				*out++ = mt64e[(b << 6 - nb) & 0x3f];
			}

			if(out + 1 >= e)
				return 0;
			*out++ = '-';
			if(r == 0)
				break;
		}else
			in++;
		if(out + 1 >= e)
			return 0;
		*out = r;
		out++;
		if(r == '&')
			*out++ = '-';
	}
	*out = 0;
	if(!in || out >= e)
		return 0;
	return start;
}

char*
decmutf7(char *out, int lim, char *in)
{
	char *start, *e;
	int c, b, nb;
	Rune rr;

	start = out;
	e = out + lim;
	if(mt64e[0] == 0)
		initm64();
	if(in)
	for(;;){
		c = *in;

		if(c < ' ' || c >= Runeself){
			if(c == 0)
				break;
			return 0;
		}
		if(c != '&'){
			if(out + 1 >= e)
				return 0;
			*out++ = c;
			in++;
			continue;
		}
		in++;
		if(*in == '-'){
			if(out + 1 >= e)
				return 0;
			*out++ = '&';
			in++;
			continue;
		}

		b = 0;
		nb = 0;
		while((c = *in++) != '-'){
			c = mt64d[c];
			if(c >= 64)
				return 0;
			b = (b << 6) | c;
			nb += 6;
			if(nb >= 16){
				rr = b >> (nb - 16);
				nb -= 16;
				if(out + UTFmax + 1 >= e && out + runelen(rr) + 1 >= e)
					return 0;
				out += runetochar(out, &rr);
			}
		}
		if(b & ((1 << nb) - 1))
			return 0;
	}
	*out = 0;
	if(!in || out >= e)
		return 0;
	return start;
}
