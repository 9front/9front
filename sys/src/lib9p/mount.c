#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <auth.h>

void
postmountsrv(Srv *s, char *name, char *mtpt, int flag)
{
	postsrv(s, name);

	if(mtpt != nil){
		if(amount(s->srvfd, mtpt, flag, "") == -1)
			sysfatal("mount %s: %r", mtpt);
		/* mount closed s->srvfd */
	} else
		close(s->srvfd);
}
