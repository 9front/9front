#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <auth.h>

void
postmountsrv(Srv *s, char *name, char *mtpt, int flag)
{
	int sfd;

	sfd = postsrv(s, name);
	if(sfd < 0)
		sysfatal("postsrv: %r");
	if(mtpt != nil){
		if(amount(sfd, mtpt, flag, "") == -1)
			sysfatal("mount %s: %r", mtpt);
		/* mount closed sfd */
	} else
		close(sfd);
}
