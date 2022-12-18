#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include "iotrack.h"
#include "dat.h"
#include "dosfs.h"
#include "fns.h"

#include "errstr.h"

#define	Reqsize	(sizeof(Fcall)+Maxfdata)
Fcall	*req;
Fcall	*rep;

uchar	mdata[Maxiosize];
char	repdata[Maxfdata];
uchar	statbuf[STATMAX];
int	errno;
char	errbuf[ERRMAX];
char	srvfile[128];
char	*deffile;
int	doabort;
int	trspaces;

void	(*fcalls[])(void) = {
	[Tversion]	rversion,
	[Tflush]	rflush,
	[Tauth]	rauth,
	[Tattach]	rattach,
	[Twalk]		rwalk,
	[Topen]		ropen,
	[Tcreate]	rcreate,
	[Tread]		rread,
	[Twrite]	rwrite,
	[Tclunk]	rclunk,
	[Tremove]	rremove,
	[Tstat]		rstat,
	[Twstat]	rwstat,
};

void
usage(void)
{
	fprint(2, "usage: %s [-v] [-s] [-f devicefile] [srvname]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int stdio, srvfd, pipefd[2];

	rep = malloc(sizeof(Fcall));
	req = malloc(Reqsize);
	if(rep == nil || req == nil)
		panic("out of memory");
	stdio = 0;
	ARGBEGIN{
	case ':':
		trspaces = 1;
		break;
	case 'r':
		readonly = 1;
		break;
	case 'v':
		++chatty;
		break;
	case 'f':
		deffile = ARGF();
		break;
	case 's':
		stdio = 1;
		break;
	case 'p':
		doabort = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc == 0)
		snprint(srvfile, sizeof srvfile, "/srv/dos");
	else if(argc == 1)
		snprint(srvfile, sizeof srvfile, "/srv/%s", argv[0]);
	else
		usage();

	iotrack_init();

	if(!stdio){
		if(pipe(pipefd) < 0)
			panic("pipe");
		srvfd = create(srvfile, OWRITE|ORCLOSE, 0600);
		if(srvfd < 0)
			panic(srvfile);
		fprint(srvfd, "%d", pipefd[0]);
		close(pipefd[0]);
		fprint(2, "%s: serving %s\n", argv0, srvfile);

		dup(pipefd[1], 0);
		dup(pipefd[1], 1);
	}

	switch(rfork(RFNOWAIT|RFNOTEG|RFFDG|RFPROC|RFNAMEG)){
	case -1:
		panic("fork");
	default:
		_exits(nil);
	case 0:
		break;
	}

	io();
	exits(nil);
}

void
io(void)
{
	int n, pid;

	pid = getpid();

	fmtinstall('F', fcallfmt);
	while((n = read9pmsg(0, mdata, sizeof mdata)) != 0){
		if(n < 0)
			panic("mount read");
		if(convM2S(mdata, n, req) != n)
			panic("convM2S format error");

		if(chatty)
			fprint(2, "dossrv %d:<-%F\n", pid, req);

		errno = 0;
		if(!fcalls[req->type])
			errno = Ebadfcall;
		else
			(*fcalls[req->type])();
		if(errno){
			rep->type = Rerror;
			rep->ename = xerrstr(errno);
		}else{
			rep->type = req->type + 1;
			rep->fid = req->fid;
		}
		rep->tag = req->tag;
		if(chatty)
			fprint(2, "dossrv %d:->%F\n", pid, rep);
		n = convS2M(rep, mdata, sizeof mdata);
		if(n == 0)
			panic("convS2M error on write");
		if(write(1, mdata, n) != n)
			panic("mount write");
	}
	chat("server shut down\n");
}

char *
xerrstr(int e)
{
	if (e < 0 || e >= sizeof errmsg/sizeof errmsg[0])
		return "no such error";
	if(e == Eerrstr){
		errstr(errbuf, sizeof errbuf);
		return errbuf;
	}
	return errmsg[e];
}

int
eqqid(Qid q1, Qid q2)
{
	return q1.path == q2.path && q1.type == q2.type && q1.vers == q2.vers;
}
