#include <u.h>
#include <libc.h>

static void
usage(void)
{
	fprint(2, "usage: %s [-dR] [-p perm] [-P patternfile] [-e exportfs] srvname path\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *ename, *arglist[16], **argp;
	int fd, pipefd[2];
	char buf[64];
	int perm = 0600;

	argp = arglist;
	ename = "/bin/exportfs";
	*argp++ = "exportfs";
	ARGBEGIN{
	default:
		usage();
	case 'd':
		*argp++ = "-d";
		break;
	case 'e':
		ename = EARGF(usage());
		break;
	case 'p':
		perm = strtol(EARGF(usage()), 0, 8);
		break;
	case 'P':
		*argp++ = "-P";
		*argp++ = EARGF(usage());
		break;
	case 'R':
		*argp++ = "-R";
		 break;
	}ARGEND
	if(argc != 2)
		usage();
	*argp++ = "-r";
	*argp++ = argv[1];
	*argp = 0;

	if(pipe(pipefd) < 0){
		fprint(2, "can't pipe: %r\n");
		exits("pipe");
	}
	if(argv[0][0] == '/')
		strecpy(buf, buf+sizeof buf, argv[0]);
	else
		snprint(buf, sizeof buf, "/srv/%s", argv[0]);
	fd = create(buf, OWRITE|ORCLOSE, perm);
	if(fd < 0){
		fprint(2, "can't create %s: %r\n", buf);
		exits("create");
	}
	fprint(fd, "%d", pipefd[1]);
	close(pipefd[1]);

	switch(rfork(RFPROC|RFNOWAIT|RFNOTEG|RFFDG)){
	case -1:
		fprint(2, "can't rfork: %r\n");
		exits("rfork");
	case 0:
		dup(pipefd[0], 0);
		dup(pipefd[0], 1);
		close(pipefd[0]);
		exec(ename, arglist);
		fprint(2, "can't exec exportfs: %r\n");
		exits("exec");
	default:
		break;
	}
	exits(0);
}
