#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <auth.h>

static void
postproc(void *v)
{
	Srv *s;

	s = v;
	rendezvous(0, 0);
	close(s->srvfd);
	srv(s);
}

static void
postsrv(Srv *s, char *name)
{
	char buf[80];
	int fd[2];
	int cfd;

	if(pipe(fd) < 0)
		sysfatal("pipe: %r");
	s->infd = s->outfd = fd[1];
	s->srvfd = fd[0];

	if(name != nil){
		snprint(buf, sizeof buf, "/srv/%s", name);
		if((cfd = create(buf, OWRITE|ORCLOSE|OCEXEC, 0600)) < 0)
			sysfatal("create %s: %r", buf);
		if(fprint(cfd, "%d", s->srvfd) < 0)
			sysfatal("write %s: %r", buf);
	} else
		cfd = -1;

	if(_forker == nil)
		sysfatal("no forker");
	_forker(postproc, s, RFNAMEG|RFNOTEG);

	rfork(RFFDG);
	rendezvous(0, 0);

	close(s->infd);
	if(s->infd != s->outfd)
		close(s->outfd);

	if(cfd >= 0)
		close(cfd);
}

void
_postmountsrv(Srv *s, char *name, char *mtpt, int flag)
{
	postsrv(s, name);

	if(mtpt != nil){
		if(amount(s->srvfd, mtpt, flag, "") == -1)
			sysfatal("mount %s: %r", mtpt);
		/* mount closed s->srvfd */
	} else
		close(s->srvfd);
}

void
_postsharesrv(Srv *s, char *name, char *mtpt, char *desc)
{
	char buf[80];
	int cfd;

	if(mtpt != nil && desc != nil){
		snprint(buf, sizeof buf, "#σc/%s", mtpt);
		if((cfd = create(buf, OREAD, DMDIR|0700)) >= 0)
			close(cfd);

		snprint(buf, sizeof buf, "#σc/%s/%s", mtpt, desc);
		if((cfd = create(buf, OWRITE|ORCLOSE|OCEXEC, 0600)) < 0)
			sysfatal("create %s: %r", buf);
	} else
		cfd = -1;

	postsrv(s, name);

	if(cfd >= 0){
		if(fprint(cfd, "%d\n", s->srvfd) < 0)
			sysfatal("write %s: %r", buf);
		close(cfd);
	}
	close(s->srvfd);
}
