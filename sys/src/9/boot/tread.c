#include <u.h>
#include <libc.h>

int c;

int
alarmed(void *a, char *msg)
{
	USED(a);
	USED(msg);
	if(!c)
		exits("timedout");
	noted(NCONT);
	return 1;
}

void
readline(int fd, char *buf, int nbuf)
{
	int i, n;

	i = 0;
	while(i < nbuf-1){
		n = read(fd, &c, sizeof c);
		alarm(0);
		c &= 0xff;
		write(fd, &c, 1);
		if(n != 1 || c == '\04' || c == '\177'){
			i = 0;
			break;
		} else if(c == '\n')
			break;
		else if(c == '\b' && i > 0)
			--i;
		else if(c == ('u' & 037)){
			c = '\b';
			for(n=0; n <= i; n++)
				write(fd, &c, 1);
			i = 0;
		} else
			buf[i++] = c;
	}
	buf[i] = 0;
}

void
main(int argc, char *argv[])
{
	int fd, ctl, i;
	char buf[256];
	long n;

	if(argc < 2)
		sysfatal("usage: tread timeout");

	atnotify(alarmed, 1);

	fd = open("/dev/cons", ORDWR);
	if(fd < 0)
		sysfatal("open cons: %r");
	ctl = open("/dev/consctl", OWRITE);
	if(ctl < 0)
		sysfatal("open consctl: %r");

	write(ctl, "rawon", 5);
	alarm(atoi(argv[1])*1000);

	readline(fd, buf, sizeof(buf));
	close(ctl);
	close(fd);
	print("%s", buf);
	exits(nil);
}
