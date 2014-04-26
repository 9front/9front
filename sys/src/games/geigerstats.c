#include <u.h>
#include <libc.h>

enum {
	SRATE = 44100,
	NSAMP = SRATE / 10,
};

void
usage(void)
{
	fprint(2, "%s: usage: %s [-d dev] [-v vol]\n", argv0, argv0);
	exits("usage");
}

int
load(void)
{
	static int fd = -1;
	static char buf[1024];
	int rc, sum;
	char *p, *q, *e;
	char *f[10];
	
	if(fd < 0)
		fd = open("/dev/sysstat", OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	seek(fd, 0, 0);
	if((rc = readn(fd, buf, sizeof(buf)-1)) < 0)
		sysfatal("read: %r");
	p = buf;
	e = buf + rc;
	sum = 0;
	while(p < e){
		q = memchr(p, '\n', e - p);
		if(q == nil)
			q = e;
		*q = 0;
		rc = tokenize(p, f, nelem(f));
		if(rc >= 7)
			sum += atoi(f[7]);
		p = q;
		if(p < e)
			p++;
	}
	return sum;
}

void
main(int argc, char **argv)
{
	char *dev;
	uchar buf[4 * NSAMP], *p;
	short s;
	int vol, fd, cps;
	ulong tresh;
	
	dev = "/dev/audio";
	vol = 32767;
	s = 0;
	ARGBEGIN{
	case 'd':
		dev = strdup(EARGF(usage()));
		break;
	case 'v':
		vol = atoi(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 0)
		usage();
	fd = open(dev, OWRITE);
	if(fd < 0)
		sysfatal("open: %r");
	for(;;){
		cps = 3 + load() / 3;
		tresh = umuldiv(0xFFFFFFFF, cps, SRATE);
		for(p = buf; p < buf + sizeof(buf);){
			s = lrand() < tresh ? (vol - s) : s;
			*p++ = s;
			*p++ = s >> 8;
			*p++ = s;
			*p++ = s >> 8;
		}
		if(write(fd, buf, sizeof(buf)) < 0)
			sysfatal("write: %r");
	}
}
