#include <u.h>
#include <libc.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

/* don't allow other processes to debug us and steal keys */
void
private(void)
{
	int fd;
	char buf[32];
	static char pmsg[] = "Warning! %s can't protect itself from debugging: %r\n";
	static char smsg[] = "Warning! %s can't turn off swapping: %r\n";

	snprint(buf, sizeof(buf), "/proc/%d/ctl", getpid());
	fd = open(buf, OWRITE|OCEXEC);
	if(fd < 0){
		fprint(2, pmsg, argv0);
		return;
	}
	if(fprint(fd, "private") < 0)
		fprint(2, pmsg, argv0);
	if(fprint(fd, "noswap") < 0)
		fprint(2, smsg, argv0);
	close(fd);
}
