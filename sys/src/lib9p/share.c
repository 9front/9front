#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

void
postsharesrv(Srv *s, char *name, char *mtpt, char *desc)
{
	char buf[80];
	int cfd, sfd;

	if(mtpt != nil && desc != nil){
		snprint(buf, sizeof buf, "#σc/%s", mtpt);
		if((cfd = create(buf, OREAD, DMDIR|0700)) >= 0)
			close(cfd);

		snprint(buf, sizeof buf, "#σc/%s/%s", mtpt, desc);
		if((cfd = create(buf, OWRITE|ORCLOSE|OCEXEC, 0600)) < 0)
			sysfatal("create %s: %r", buf);
	} else
		cfd = -1;

	sfd = postsrv(s, name);
	if(sfd < 0)
		sysfatal("postsrv: %r");
	if(cfd >= 0){
		if(fprint(cfd, "%d\n", sfd) < 0)
			sysfatal("write %s: %r", buf);
		close(cfd);
	}
	close(sfd);
}
