#include <u.h>
#include <libc.h>

enum {buflen = 4096};

void
main(int argc, char **argv)
{
	int rc, cpid, fd, dopipe;
	static char buf[buflen];
	static uvlong bc, sec;
	double speed;
	
	dopipe = 0;
	ARGBEGIN {
	case 'p': dopipe = 1;
	} ARGEND
	
	bc = 0;
	sec = 0;
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
	sprint(buf, "/proc/%d/note", cpid);
	fd = open(buf, OWRITE);
	write(fd, "kill", 4);
	if(rc < 0) sysfatal("%r");
}
