#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

static void
postproc(void *v)
{
	Srv *s = v;
	close((int)(uintptr)rendezvous(s, 0));
	srv(s);
}

int
postsrv(Srv *s, char *name)
{
	int fd[2], cfd;

	if(pipe(fd) < 0)
		return -1;
	if(name != nil){
		char buf[160];

		snprint(buf, sizeof buf, "/srv/%s", name);
		if((cfd = create(buf, OWRITE|ORCLOSE|OCEXEC, 0600)) < 0
		|| fprint(cfd, "%d", fd[0]) < 0){
			close(fd[0]);
			fd[0] = -1;
			goto Out;
		}
	} else
		cfd = -1;

	/* now we are commited */
	s->infd = s->outfd = fd[1];
	if(s->forker == nil)
		s->forker = srvforker;
	(*s->forker)(postproc, s, RFNAMEG|RFNOTEG);

	rfork(RFFDG);
	rendezvous(s, (void*)(uintptr)fd[0]);
Out:
	if(cfd >= 0)
		close(cfd);
	close(fd[1]);
	return fd[0];
}
