#include <u.h>
#include <libc.h>

/*
 *  prompt for a string with a possible default response
 */
char*
readcons(char *prompt, char *def, int raw)
{
	int fdin, fdout, ctl, n;
	char *s, *p;

	s = p = nil;
	fdout = ctl = -1;

	if((fdin = open("/dev/cons", OREAD)) < 0)
		goto Out;
	if((fdout = open("/dev/cons", OWRITE)) < 0)
		goto Out;

	if(raw){
		if((ctl = open("/dev/consctl", OWRITE)) < 0)
			goto Out;
		write(ctl, "rawon", 5);
	}

	if(def != nil)
		fprint(fdout, "%s[%s]: ", prompt, def);
	else
		fprint(fdout, "%s: ", prompt);

	for(;;){
		n = p - s;
		if((n % 32) == 0){
			if((p = realloc(s, n+32)) == nil)
				break;
			s = p, p += n;
		}

		n = read(fdin, p, 1);
		if(n < 0)
			break;
		if(n == 0 || *p == 0x7f){
			werrstr("input aborted");
			break;
		}

		if(*p == '\n' || *p == '\r'){
			if(p == s && def != nil){
				free(s);
				s = strdup(def);
			} else
				*p = 0;
			if(raw)
				write(fdout, "\n", 1);
			goto Out;
		} else if(*p == '\b') {
			while(p > s && (p[-1] & 0xc0) == 0x80)
				*p-- = 0;
			if(p > s)
				*p-- = 0;
		} else if(*p == 0x15) {	/* ^U: line kill */
			if(def != nil)
				fprint(fdout, "\n%s[%s]: ", prompt, def);
			else
				fprint(fdout, "\n%s: ", prompt);
			while(p > s)
				*p-- = 0;
		} else if(*p >= ' ')
			p++;
	}
	free(s);
	s = nil;
	if(raw)
		write(fdout, "\n", 1);
Out:
	if(ctl >= 0){
		write(ctl, "rawoff", 6);
		close(ctl);
	}
	if(fdin >= 0)
		close(fdin);
	if(fdout >= 0)
		close(fdout);

	return s;
}
