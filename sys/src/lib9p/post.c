#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <auth.h>

static void
postproc(void *v)
{
	Srv *s = v;
	rendezvous(0, 0);
	close(s->srvfd);
	srv(s);
}

void
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

	if(s->forker == nil)
		s->forker = srvforker;
	(*s->forker)(postproc, s, RFNAMEG|RFNOTEG);

	rfork(RFFDG);
	rendezvous(0, 0);

	close(s->infd);
	if(s->infd != s->outfd)
		close(s->outfd);

	if(cfd >= 0)
		close(cfd);
}
