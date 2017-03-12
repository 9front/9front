#include "common.h"

/*
 *  check for shell characters in a String
 */
static char *illegalchars = "\r\n";

extern int
shellchars(char *cp)
{
	char *sp;

	for(sp=illegalchars; *sp; sp++)
		if(strchr(cp, *sp))
			return 1;
	return 0;
}

static char *specialchars = " ()<>{};=\\'\`^&|";
static char *escape = "%%";

int
hexchar(int x)
{
	x &= 0xf;
	if(x < 10)
		return '0' + x;
	else
		return 'A' + x - 10;
}

/*
 *  rewrite a string to escape shell characters
 */
extern String*
escapespecial(String *s)
{
	String *ns;
	char *sp;

	for(sp = specialchars; *sp; sp++)
		if(strchr(s_to_c(s), *sp))
			break;
	if(*sp == 0)
		return s;

	ns = s_new();
	for(sp = s_to_c(s); *sp; sp++){
		if(strchr(specialchars, *sp)){
			s_append(ns, escape);
			s_putc(ns, hexchar(*sp>>4));
			s_putc(ns, hexchar(*sp));
		} else
			s_putc(ns, *sp);
	}
	s_terminate(ns);
	s_free(s);
	return ns;
}

uint
hex2uint(char x)
{
	if(x >= '0' && x <= '9')
		return x - '0';
	if(x >= 'A' && x <= 'F')
		return (x - 'A') + 10;
	if(x >= 'a' && x <= 'f')
		return (x - 'a') + 10;
	return -512;
}

/*
 *  rewrite a string to remove shell characters escapes
 */
extern String*
unescapespecial(String *s)
{
	char *sp;
	uint c, n;
	String *ns;

	if(strstr(s_to_c(s), escape) == 0)
		return s;
	n = strlen(escape);

	ns = s_new();
	for(sp = s_to_c(s); *sp; sp++){
		if(strncmp(sp, escape, n) == 0){
			c = (hex2uint(sp[n])<<4) | hex2uint(sp[n+1]);
			if(c & 0x80)
				s_putc(ns, *sp);
			else {
				s_putc(ns, c);
				sp += n+2-1;
			}
		} else
			s_putc(ns, *sp);
	}
	s_terminate(ns);
	s_free(s);
	return ns;

}

int
returnable(char *path)
{
	return strcmp(path, "/dev/null") != 0;
}
