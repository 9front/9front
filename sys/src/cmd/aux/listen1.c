#include <u.h>
#include <libc.h>
#include <auth.h>

int maxprocs;
int verbose;
int trusted;
char *nsfile;

void
usage(void)
{
	fprint(2, "usage: listen1 [-tv] [-p maxprocs] [-n namespace] address cmd args...\n");
	exits("usage");
}

void
becomenone(void)
{
	int fd;

	fd = open("#c/user", OWRITE);
	if(fd < 0 || write(fd, "none", strlen("none")) < 0)
		sysfatal("can't become none: %r");
	close(fd);
	if(newns("none", nsfile) < 0)
		sysfatal("can't build namespace: %r");
}

char*
remoteaddr(char *dir)
{
	static char buf[128];
	char *p;
	int n, fd;

	snprint(buf, sizeof buf, "%s/remote", dir);
	fd = open(buf, OREAD);
	if(fd < 0)
		return "";
	n = read(fd, buf, sizeof(buf));
	close(fd);
	if(n > 0){
		buf[n] = 0;
		p = strchr(buf, '!');
		if(p)
			*p = 0;
		return buf;
	}
	return "";
}

void
main(int argc, char **argv)
{
	char data[60], dir[40], ndir[40], wbuf[64];
	int ctl, nctl, fd;
	int wfd, nowait, procs;
	Dir *d;

	ARGBEGIN{
	default:
		usage();
	case 't':
		trusted = 1;
		break;
	case 'v':
		verbose = 1;
		break;
	case 'p':
		maxprocs = atoi(EARGF(usage()));
		break;
	case 'n':
		nsfile = EARGF(usage());
		break;
	}ARGEND

	if(argc < 2)
		usage();

	if(!verbose){
		close(1);
		fd = open("/dev/null", OWRITE);
		if(fd != 1){
			dup(fd, 1);
			close(fd);
		}
	}

	if(!trusted)
		becomenone();

	print("listen started\n");
	ctl = announce(argv[0], dir);
	if(ctl < 0)
		sysfatal("announce %s: %r", argv[0]);

	wfd = -1;
	nowait = RFNOWAIT;
	if(maxprocs > 0){
		snprint(wbuf, sizeof(wbuf), "/proc/%d/wait", getpid());
		if((wfd = open(wbuf, OREAD)) >= 0)
			nowait = 0;
	}
	procs = 0;
	for(;;){
		if(nowait == 0 && (procs >= maxprocs || (procs % 8) == 0))
			while(procs > 0){
				if(procs < maxprocs){
					d = dirfstat(wfd);
					if(d == nil || d->length == 0){
						free(d);
						break;
					}
					free(d);
				}
				if(read(wfd, wbuf, sizeof(wbuf)) > 0)
					procs--;
			}

		nctl = listen(dir, ndir);
		if(nctl < 0)
			sysfatal("listen %s: %r", argv[0]);

		switch(rfork(RFFDG|RFPROC|RFMEM|RFENVG|RFNAMEG|RFNOTEG|RFREND|nowait)){
		case -1:
			reject(nctl, ndir, "host overloaded");
			close(nctl);
			continue;
		case 0:
			fd = accept(nctl, ndir);
			if(fd < 0){
				fprint(2, "accept %s: can't open  %s/data: %r\n",
					argv[0], ndir);
				_exits(0);
			}
			print("incoming call for %s from %s in %s\n", argv[0],
				remoteaddr(ndir), ndir);
			fprint(nctl, "keepalive");
			close(ctl);
			close(nctl);
			if(wfd >= 0)
				close(wfd);
			putenv("net", ndir);
			snprint(data, sizeof data, "%s/data", ndir);
			bind(data, "/dev/cons", MREPL);
			dup(fd, 0);
			dup(fd, 1);
			/* dup(fd, 2); keep stderr */
			close(fd);
			exec(argv[1], argv+1);
			if(argv[1][0] != '/')
				exec(smprint("/bin/%s", argv[1]), argv+1);
			fprint(2, "%s: exec: %r\n", argv0);
			exits(nil);
		default:
			close(nctl);
			procs++;
			break;
		}
	}
}
