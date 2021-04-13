/* http://en.wikipedia.org/wiki/ISO/IEC_8859-1 */
#include "tagspriv.h"

int
iso88591toutf8(uchar *o, int osz, const uchar *s, int sz)
{
	int i;

	for(i = 0; i < sz && osz > 1 && s[i] != 0; i++){
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
}
