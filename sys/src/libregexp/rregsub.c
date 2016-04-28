#include <u.h>
#include <libc.h>
#include <regexp.h>

void
rregsub(Rune *src, Rune *dst, int dlen, Resub *match, int msize)
{
	int i;
	Rune *ep, r;

	ep = dst + dlen-1;
	for(;*src != L'\0'; src++) switch(*src) {
	case L'\\':
		switch(*++src) {
		case L'0':
		case L'1':
		case L'2':
		case L'3':
		case L'4':
		case L'5':
		case L'6':
		case L'7':
		case L'8':
		case L'9':
			i = *src - L'0';
			if(match != nil && i < msize && match[i].rsp != nil) {
				r = *match[i].rep;
				*match[i].rep = L'\0';
				dst = runestrecpy(dst, ep+1, match[i].rsp);
				*match[i].rep = r;
			}
			break;
		case L'\\':
			if(dst < ep)
				*dst++ = L'\\';
			else
				goto End;
			break;
		case L'\0':
			goto End;
		default:
			if(dst < ep)
				*dst++ = *src;
			else
				goto End;
			break;
		}
		break;
	case L'&':
		if(match != nil && msize > 0 && match[0].rsp != nil) {
			r = *match[0].rep;
			*match[0].rep = L'\0';
			dst = runestrecpy(dst, ep+1, match[0].rsp);
			*match[0].rep = r;
		}
		break;
	default:
		if(dst < ep)
			*dst++ = *src;
		else
			goto End;
		break;
	}
End:
	*dst = L'\0';
}
