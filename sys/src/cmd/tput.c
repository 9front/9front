#include <u.h>
#include <libc.h>

int dopipe;
int buflen;
uvlong bc, sec;

void
usage(void)
{
	fprint(2, "usage: %s [-b buflen] [-p]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	double speed;
	int rc, cpid;
	char *buf;

	ARGBEGIN {
	case 'b':
		buflen = atoi(EARGF(usage()));
		break;
	case 'p':
		dopipe = 1;
		break;
	default:
		usage();
	} ARGEND

	if(argc != 0)
		usage();

	bc = 0;
	sec = 0;
	if(buflen <= 0){
		buflen = iounit(0);
		if(buflen <= 0)
			buflen = IOUNIT;
	}
	buf = sbrk(buflen);
	if(buf == (void*)-1)
		sysfatal("out of memory");
	cpid = rfork(RFPROC | RFMEM);
	if(cpid == 0) {
		while(1) {
			sleep(1000);
			speed = bc / ++sec;
			if(speed >= 1073741824) fprint(2, "%.2f GB/s\n", speed / 1073741824);
			else if(speed >= 1048576) fprint(2, "%.2f MB/s\n", speed / 1048576);
			else if(speed >= 1024) fprint(2, "%.2f KB/s\n", speed / 1024);
			else fprint(2, "%.2f B/s\n", speed);
		}
	}
	while(1) {
		rc = read(0, buf, buflen);
		if(rc <= 0) break;
		if(dopipe) write(1, buf, rc);
		bc += rc;
	}
	postnote(PNPROC, cpid, "kill");
	if(rc < 0) sysfatal("%r");
	exits(0);
}
