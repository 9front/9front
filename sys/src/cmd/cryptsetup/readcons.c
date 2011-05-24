/* From /sys/src/libauthsrv/readnvram.c, LPL licensed */
#include <u.h>
#include <libc.h>


char*
readcons(char *prompt, char *def, int raw, char *buf, int nbuf)
{
	int fdin, fdout, ctl, n, m;
	char line[10];

	fdin = open("/dev/cons", OREAD);
	if(fdin < 0)
		fdin = 0;
	fdout = open("/dev/cons", OWRITE);
	if(fdout < 0)
		fdout = 1;
	if(def != nil)
		fprint(fdout, "%s[%s]: ", prompt, def);
	else
		fprint(fdout, "%s: ", prompt);
	if(raw){
		ctl = open("/dev/consctl", OWRITE);
		if(ctl >= 0)
			write(ctl, "rawon", 5);
	} else
		ctl = -1;

	m = 0;
	for(;;){
		n = read(fdin, line, 1);
		if(n == 0){
			close(ctl);
			werrstr("readcons: EOF");
			return nil;
		}
		if(n < 0){
			close(ctl);
			werrstr("can't read cons");
			return nil;
		}
		if(line[0] == 0x7f)
			exits(0);
		if(n == 0 || line[0] == '\n' || line[0] == '\r'){
			if(raw){
				write(ctl, "rawoff", 6);
				write(fdout, "\n", 1);
				close(ctl);
			}
			buf[m] = '\0';
			if(buf[0]=='\0' && def)
				strcpy(buf, def);
			return buf;
		}
		if(line[0] == '\b'){
			if(m > 0)
				m--;
		}else if(line[0] == 0x15){	/* ^U: line kill */
			m = 0;
			if(def != nil)
				fprint(fdout, "%s[%s]: ", prompt, def);
			else
				fprint(fdout, "%s: ", prompt);
		}else{
			if(m >= nbuf-1){
				fprint(fdout, "line too long\n");
				m = 0;
				if(def != nil)
					fprint(fdout, "%s[%s]: ", prompt, def);
				else
					fprint(fdout, "%s: ", prompt);
			}else
				buf[m++] = line[0];
		}
	}
}

