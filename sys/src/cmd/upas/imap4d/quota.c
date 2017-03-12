#include "imap4d.h"

static int
openpipe(int *pip, char *cmd, char *av[])
{
	int pid, fd[2];

	if(pipe(fd) != 0)
		sysfatal("pipe: %r");
	pid = fork();
	switch(pid){
	case -1:
		return -1;
	case 0:
		close(1);
		dup(fd[1], 1);
		if(fd[1] != 1)
			close(fd[1]);
		if(fd[0] != 0)
			close(fd[0]);
		exec(cmd, av);
		ilog("exec: %r");
		_exits("b0rked");
		return -1;
	default:
		*pip = fd[0];
		close(fd[1]);
		return pid;
	}
}

static int
closepipe(int pid, int fd)
{
	int nz, wpid;
	Waitmsg *w;

	close(fd);
	while(w = wait()){
		nz = !w->msg || !w->msg[0];
		wpid = w->pid;
		free(w);
		if(wpid == pid)
			return nz? 0: -1;
	}
	return -1;
}

static char dupath[Pathlen];
static char *duav[] = { "du", "-s", dupath, 0};

vlong
getquota(void)
{
	char buf[Pathlen + 128], *f[3];
	int fd, pid;

	werrstr("");
	memset(buf, 0, sizeof buf);
	snprint(dupath, sizeof dupath, "%s", mboxdir);
	pid = openpipe(&fd, "/bin/du", duav);
	if(pid == -1)
		return -1;
	if(read(fd, buf, sizeof buf) < 4){
		closepipe(pid, fd);
		return -1;
	}
	if(closepipe(pid, fd) == -1)
		return -1;
	if(getfields(buf, f, 2, 1, "\t") != 2)
		return -1;
	return strtoull(f[0], 0, 0);
}
