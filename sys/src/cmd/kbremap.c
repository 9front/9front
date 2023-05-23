#include <u.h>
#include <libc.h>

char *mod = "4";
char *key = "57";
enum{ Kswitch = 0xF000|0x0701 };

static void
writemap(char *file)
{
	int i, fd, ofd;
	char buf[8192];
	int n;
	char *p;

	if((fd = open(file, OREAD)) < 0)
		sysfatal("cannot open %s: %r", file);

	if((ofd = open("/dev/kbmap", OWRITE|OTRUNC)) < 0)
		sysfatal("cannot open /dev/kbmap: %r");

	/* do not write half lines */
	n = 0;
	while((i = read(fd, buf + n, sizeof buf - 1 - n)) > 0){
		n += i;
		buf[n] = '\0';
		p = strrchr(buf, '\n');
		if(p == nil){
			if(n == sizeof buf - 1)
				sysfatal("writing /dev/kbmap: line too long");
			continue;
		}
		p++;
		if(write(ofd, buf, p - buf) !=  p - buf)
			sysfatal("writing /dev/kbmap: %r");
		n -= p - buf;
		memmove(buf, p, n);
	}

	fprint(ofd, "%s\t%s\t0x%X\n", mod, key, Kswitch);
	close(fd);
	close(ofd);
}

void
usage(void)
{
	fprint(2, "usage: %s [ -m mod ] [ -k scancode ] map1 map2 ...\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *p, buf[8192];
	Rune r;
	long n;
	int index;

	index = 0;
	ARGBEGIN{
	case 'm':
		mod = EARGF(usage());
		break;
	case 'k':
		key = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;
	if(argc < 2)
		usage();

	chdir("/sys/lib/kbmap");
	writemap(argv[0]);
	for(;;){
		n = read(0, buf, sizeof buf - 1);
		if(n <= 0)
			break;
		buf[n] = '\0';
		for(p = buf; p < buf+n; p += strlen(p) + 1){
			chartorune(&r, p+1);
			if(*p != 'c' || r != Kswitch){
				write(1, p, strlen(p) + 1);
				continue;
			}
			index++;
			if(argv[index] == nil)
				index = 0;
			writemap(argv[index]);
		}
	}
	exits(nil);
}
