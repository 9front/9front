/*
 * more regrettable, goofy processing
 */
#include "imap4d.h"

char tab[0x7f] = {
['\t']	'0',
[' ']	'#',
['#']	'1',
};

char itab[0x7f] = {
['0']	'\t',
['#']	' ',
['1']	'#',
};

char*
encfs(char *buf, int n, char *s)
{
	char *p, c;

	if(!s){
		*buf = 0;
		return 0;
	}
	if(!cistrcmp(s, "inbox"))
		s = "mbox";
	for(p = buf; n > 0 && (c = *s++); n--){
		if(tab[c & 0x7f]){
			if(n < 1)
				break;
			if((c = tab[c]) == 0)
				break;
			*p++ = '#';
		}
		*p++ = c;
	}
	*p = 0;
	return buf;
}

char*
decfs(char *buf, int n, char *s)
{
	char *p, c;

	if(!s){
		*buf = 0;
		return 0;
	}
	if(!cistrcmp(s, "mbox"))
		s = "INBOX";
	for(p = buf; n > 0 && (c = *s++); n--){
		if(c == '#'){
			c = *s++;
			if((c = itab[c]) == 0)
				break;
		}
		*p++ = c;
	}
	*p = 0;
	return buf;
}

/*
void
usage(void)
{
	fprint(2, "usage: encfs [-d] ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char buf[1024];
	int dflag;
	char *(*f)(char*, int, char*);

	dflag = 0;
	ARGBEGIN{
	case 'd':
		dflag ^= 1;
		break;
	default:
		usage();
	}ARGEND
	f = encfs;
	if(dflag)
		f = decfs;
	while(*argv){
		f(buf, sizeof buf, *argv++);
		print("%s\n", buf);
	}
	exits("");
}
*/
