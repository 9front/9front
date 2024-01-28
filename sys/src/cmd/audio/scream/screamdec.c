#include <u.h>
#include <libc.h>

char deffmt[] = "s16c2r44100";
char fmt[64];
uchar hdr[5];
uchar buf[2048];
int pfd[2];

char*
getformat(uchar hdr[5])
{
	int freq, bits, chan;

	if(hdr[0] & 0x80)
		freq = 44100;
	else
		freq = 48000;
	freq *= hdr[0] & 0x7F;
	bits = hdr[1];
	chan = hdr[2];
	snprint(fmt, sizeof(fmt), "s%dc%dr%d", bits, chan, freq);
	return fmt;
}

void
main(void)
{
	int n;

	for(;;){
		alarm(500);

		n = read(0, buf, sizeof(buf));
		if(n < sizeof(hdr))
			break;

		if(pfd[1] == 0 || memcmp(buf, hdr, sizeof(hdr)) != 0){
			if(pfd[1] > 1){
				close(pfd[1]);
				waitpid();
			}
			if(strcmp(getformat(buf), deffmt) == 0){
				pfd[1] = 1;
			} else {
				if(pipe(pfd) < 0)
					sysfatal("pipe: %r");
				switch(fork()){
				case -1:
					sysfatal("fork: %r");
				case 0:
					close(pfd[1]);
					dup(pfd[0], 0);
					execl("/bin/audio/pcmconv", "pcmconv", "-i", fmt, nil);
					sysfatal("exec: %r");
				}
				close(pfd[0]);
			}
			memmove(hdr, buf, sizeof(hdr));
		}

		n -= sizeof(hdr);
		if(n <= 0)
			continue;

		if(write(pfd[1], buf+sizeof(hdr), n) != n)
			break;
	}
	exits(nil);
}
