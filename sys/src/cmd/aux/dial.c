#include <u.h>
#include <libc.h>

int eflag;
int nopts;
char *opts[16];

void
xfer(int from, int to)
{
	char buf[IOUNIT];
	int n;

	while((n = read(from, buf, sizeof buf)) > 0)
		if(write(to, buf, n) < 0)
			break;
}

void
usage(void)
{
	fprint(2, "usage: %s [-e] [-o msg]... addr [cmd [args]...]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int i, fd, cfd, pid;

	ARGBEGIN {
	case 'e':
		eflag = 1;
		break;
	case 'o':
		if(nopts >= nelem(opts)){
			fprint(2, "%s: too many -o options\n", argv0);
			exits("opts");
		}
		opts[nopts++] = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	if(--argc < 0)
		usage();
	fd = dial(*argv++, nil, nil, &cfd);
	if(fd < 0){
		fprint(2, "%s: dial: %r\n", argv0);
		exits("dial");
	}
	for(i = 0; i < nopts; i++)
		write(cfd, opts[i], strlen(opts[i]));
	close(cfd);

	if(argc > 0){
		dup(fd, 0);
		dup(fd, 1);
		/* dup(fd, 2); keep stderr */
		if(fd > 2) close(fd);

		exec(argv[0], argv);
		if(argv[0][0] != '/')
			exec(smprint("/bin/%s", argv[0]), argv);
		fprint(2, "%s: exec: %r\n", argv0);
		exits("exec");
	}

	pid = fork();
	switch(pid){
	case -1:
		fprint(2, "%s: fork: %r", argv0);
		exits("fork");
	case 0:
		xfer(0, fd);
		if(eflag) exits(nil);
		pid = getppid();
		break;
	default:
		xfer(fd, 1);
		break;
	}
	postnote(PNPROC, pid, "kill");
	waitpid();
	exits(nil);
}
