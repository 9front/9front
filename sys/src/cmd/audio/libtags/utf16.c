/* Horror stories: http://en.wikipedia.org/wiki/UTF-16 */
#include "tagspriv.h"

#define rchr(s) (be ? ((s)[0]<<8 | (s)[1]) : ((s)[1]<<8 | (s)[0]))

static const uchar mark[] = {0x00, 0x00, 0xc0, 0xe0, 0xf0};

int
utf16to8(uchar *o, int osz, const uchar *s, int sz)
{
	int i, be, c, c2, wr, j;

	i = 0;
	be = 1;
	if(s[0] == 0xfe && s[1] == 0xff)
		i += 2;
	else if(s[0] == 0xff && s[1] == 0xfe){
		be = 0;
		i += 2;
	}

	for(; i < sz-1 && osz > 1;){
		c = rchr(&s[i]);
		i += 2;
		if(c >= 0xd800 && c <= 0xdbff && i < sz-1){
			c2 = rchr(&s[i]);
			if(c2 >= 0xdc00 && c2 <= 0xdfff){
				c = 0x10000 | (c - 0xd800)<<10 | (c2 - 0xdc00);
				i += 2;
			}else
				return -1;
		}else if(c >= 0xdc00 && c <= 0xdfff)
			return -1;

		if(c < 0x80)
			wr = 1;
		else if(c < 0x800)
			wr = 2;
		else if(c < 0x10000)
			wr = 3;
		else
			wr = 4;

		osz -= wr;
		if(osz < 1)
			break;

		o += wr;
		for(j = wr; j > 1; j--){
			*(--o) = (c & 0xbf) | 0x80;
			c >>= 6;
		}
		*(--o) = c | mark[wr];
		o += wr;
	}

	*o = 0;
	return i;
}
