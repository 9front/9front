#include <u.h>
#include <libc.h>
#include <bio.h>

Biobuf *tablet;
int mouseout;

int
main()
{
	mouseout = open("/dev/mousein", OWRITE);
	if(mouseout < 0) sysfatal("%r");
	tablet = Bopen("/dev/tablet", OREAD);
	if(tablet == nil) sysfatal("%r");
	while(1) {
		char *line, *p;
		int x, y, b;
		
		line = Brdline(tablet, 10);
		if(!line) sysfatal("%r");
		p = line;
		if(*p++ != 'm') continue;
		if(*p++ != ' ') continue;
		x = strtol(p, &p, 10);
		if(*p++ != ' ') continue;
		y = strtol(p, &p, 10);
		if(*p++ != ' ') continue;
		b = strtol(p, &p, 10);
		if(*p++ != ' ') continue;
		fprint(mouseout, "A %d %d %d\n", x, y, b);
	}
}