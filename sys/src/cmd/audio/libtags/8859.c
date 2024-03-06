/* http://en.wikipedia.org/wiki/ISO/IEC_8859-1 */
#include "tagspriv.h"

int
iso88591toutf8(uchar *o0, int osz, const uchar *s, int sz)
{
	uchar *o;
	int i;

	o = o0;
	for(i = 0; i < sz && osz > 1 && s[i] != 0; i++){
		if(s[i] >= 0x7f && s[i] <= 0x9f) /* not expecting control chars */
			goto asis;
		if(s[i] >= 0xa0 && osz < 3)
			break;

		if(s[i] >= 0xc0){
			*o++ = 0xc3;
			*o++ = s[i] - 0x40;
			osz--;
		}else if(s[i] >= 0xa0){
			*o++ = 0xc2;
			*o++ = s[i];
			osz--;
		}else{
			*o++ = s[i];
		}
		osz--;
	}

	*o = 0;
	return i;

asis:
	/* FIXME - copy within UTF-8 chars boundaries */
	if(sz >= osz)
		sz = osz-1;
	memmove(o0, s, sz);
	o0[sz] = 0;
	return sz;
}
