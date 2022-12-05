#include <u.h>
#include <libc.h>

int freq = 44100;
int chan = 2;
int bps = 16;
int delay = 5;

uchar buf[2048];

void
main(void)
{
	int n, m;

	if((freq % 44100) == 0){
		buf[0] = 0x80 | (freq / 44100);
	} else {
		buf[0] = freq / 48000;
	}
	buf[1] = bps;
	buf[2] = chan;
	buf[3] = 0;
	buf[4] = 0;

	n = (bps/8)*chan*((delay*freq+999)/1000);
	while((m = read(0, buf+5, n)) > 0){
		if(write(1, buf, 5+m) < 0)
			sysfatal("write: %r");
	}
	exits(nil);
}
