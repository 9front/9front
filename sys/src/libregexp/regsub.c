#include <u.h>
#include <libc.h>
#include <regexp.h>

void
regsub(char *src, char *dst, int dlen, Resub *match, int msize)
{
	int i;
	char *ep, c;

	ep = dst + dlen-1;
	for(;*src != '\0'; src++) switch(*src) {
	case '\\':
		switch(*++src) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			i = *src - '0';
			if(match != nil && i < msize && match[i].ep != nil) {
				c = *match[i].ep;
				*match[i].ep = '\0';
				dst = strecpy(dst, ep+1, match[i].sp);
				*match[i].ep = c;
			}
			break;
		case '\\':
			if(dst < ep)
				*dst++ = '\\';
			else
				goto End;
			break;
		case '\0':
			goto End;
		default:
			if(dst < ep)
				*dst++ = *src;
			else
				goto End;
			break;
		}
		break;
	case '&':
		if(match != nil && msize > 0 && match[0].sp != nil) {
			c = *match[0].ep;
			*match[0].ep = '\0';
			dst = strecpy(dst, ep+1, match[0].sp);
			*match[0].ep = c;
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
	*dst = '\0';
}
