/* https://en.wikipedia.org/wiki/Code_page_437 */
#include "tagspriv.h"

static Rune rh[129] =
	L"ΔÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧"
	L"ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜"
	L"╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌"
	L"▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠"
	L"⌡÷≈°∙·√ⁿ²■ ";

int
cp437toutf8(char *o, int osz, const char *s, int sz)
{
	char c[UTFmax];
	int i, n;
	Rune r;

	for(i = 0; i < sz && osz > 1 && s[i] != 0; i++){
		if((uchar)s[i] < 127){
			*o++ = s[i];
			osz--;
			continue;
		}
		r = rh[(uchar)s[i] - 127];
		if((n = runetochar(c, &r)) >= osz)
			break;
		memmove(o, c, n);
		o += n;
		osz -= n;
	}

	*o = 0;
	return i;
}
