#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <auth.h>

static void postproc(void*);

void
_postmountsrv(Srv *s, char *name, char *mtpt, int flag)
{
	int fd[2];

	if(pipe(fd) < 0)
		sysfatal("pipe: %r");
	s->infd = s->outfd = fd[1];
	s->srvfd = fd[0];

	if(name)
		if(postfd(name, s->srvfd) < 0)
			sysfatal("postfd %s: %r", name);

	if(_forker == nil)
		sysfatal("no forker");
	_forker(postproc, s, RFNAMEG|RFFDG|RFNOTEG);

	close(s->infd);
	if(s->infd != s->outfd)
		close(s->outfd);

	if(mtpt){
		if(amount(s->srvfd, mtpt, flag, "") == -1)
			sysfatal("mount %s: %r", mtpt);
	}else
		close(s->srvfd);
}

void
_postsharesrv(Srv *s, char *name, char *mtpt, char *desc)
{
	int fd[2];

	if(pipe(fd) < 0)
		sysfatal("pipe: %r");
	s->infd = s->outfd = fd[1];
	s->srvfd = fd[0];

	if(name)
		if(postfd(name, s->srvfd) < 0)
			sysfatal("postfd %s: %r", name);

	if(_forker == nil)
		sysfatal("no forker");
	_forker(postproc, s, RFNAMEG|RFFDG|RFNOTEG);

	close(s->infd);
	if(s->infd != s->outfd)
		close(s->outfd);

	if(mtpt){
		if(sharefd(mtpt, desc, s->srvfd) < 0)
			sysfatal("sharefd %s: %r", mtpt);
	}else
		close(s->srvfd);
}


static void
postproc(void *v)
{
	Srv *s;

	s = v;
	close(s->srvfd);
	srv(s);
}
