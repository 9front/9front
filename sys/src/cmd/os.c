#include <u.h>
#include <libc.h>

enum {
	Fstdin,
	Fstdout,
	Fstderr,
	Fwait,
	Fctl,
	Nfd,
};

enum {
	Pcopyin,
	Pcopyout,
	Pcopyerr,
	Preadwait,
	Npid,
};

char *mnt = "/mnt/term/cmd";
char *dir = nil;
char buf[IOUNIT];
int fd[Nfd] = {-1};
int pid[Npid];
int nice, foreground = 1;

void
killstdin(void)
{
	if(pid[Pcopyin] != 0)
		postnote(PNPROC, pid[Pcopyin], "kill");
}

void
killproc(void)
{
	if(fd[Fctl] >= 0)
		write(fd[Fctl], "kill", 4);
}

int
catch(void*, char *msg)
{
	if(strcmp(msg, "interrupt") == 0
	|| strcmp(msg, "hangup") == 0
	|| strcmp(msg, "kill") == 0){
		killproc();
		return 1;
	}
	return 0;
}

void
fd01(int fd0, int fd1)
{
	int i;

	if(fd0 >= 0 && fd0 != 0){
		if(dup(fd0, 0) < 0)
			sysfatal("dup: %r");
	}
	if(fd1 >= 0 && fd1 != 1){
		if(dup(fd1, 1) < 0)
			sysfatal("dup: %r");
	}
	for(i = 0; i<Nfd; i++){
		if(fd[i] > 2)
			close(fd[i]);
		if(fd[i] == fd0)
			fd[i] = 0;
		else if(fd[i] == fd1)
			fd[i] = 1;
		else
			fd[i] = -1;
	}
}

void
copy(void)
{
	int n;

	while((n = read(0, buf, sizeof(buf))) > 0)
		if(write(1, buf, n) != n)
			break;
}

void
usage(void)
{
	fprint(2, "%s: [ -b ] [ -m mountpoint ] [ -d dir ] [ -n ] [ -N level ] cmd [ arg... ]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	Waitmsg *w;
	char *s;
	int n;

	quotefmtinstall();

	ARGBEGIN {
	case 'b':
		foreground = 0;
		break;
	case 'm':
		mnt = cleanname(EARGF(usage()));
		break;
	case 'd':
		dir = EARGF(usage());
		break;
	case 'n':
		nice = 1;
		break;
	case 'N':
		nice = atoi(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND;

	if(argc < 1)
		usage();

	seprint(buf, &buf[sizeof(buf)], "%s/clone", mnt);
	if((fd[Fctl] = open(buf, ORDWR)) < 0)
		sysfatal("open: %r");
	s = &buf[strlen(mnt)+1];
	if((n = read(fd[Fctl], s, &buf[sizeof(buf)-1]-s)) < 0)
		sysfatal("read clone: %r");
	while(n > 0 && buf[n-1] == '\n')
		n--;
	s += n;

	seprint(s, &buf[sizeof(buf)], "/wait");
	if((fd[Fwait] = open(buf, OREAD)) < 0)
		sysfatal("open: %r");

	if(foreground){
		seprint(s, &buf[sizeof(buf)], "/data");
		if((fd[Fstdin] = open(buf, OWRITE)) < 0)
			sysfatal("open: %r");

		seprint(s, &buf[sizeof(buf)], "/data");
		if((fd[Fstdout] = open(buf, OREAD)) < 0)
			sysfatal("open: %r");

		seprint(s, &buf[sizeof(buf)], "/stderr");
		if((fd[Fstderr] = open(buf, OREAD)) < 0)
			sysfatal("open: %r");
	}

	if(dir != nil){
		if(fprint(fd[Fctl], "dir %q", dir) < 0)
			sysfatal("cannot change directory: %r");
	} else {
		/*
		 * try to automatically change directory if we are in
		 * /mnt/term or /mnt/term/root, but unlike -d flag,
		 * do not error when the dir ctl command fails.
		 */
		if((s = strrchr(mnt, '/')) != nil){
			n = s - mnt;
			dir = getwd(buf, sizeof(buf));
			if(strncmp(dir, mnt, n) == 0 && (dir[n] == 0 || dir[n] == '/')){
				dir += n;
				if(strncmp(dir, "/root", 5) == 0 && (dir[5] == 0 || dir[5] == '/'))
					dir += 5;
				if(*dir == 0)
					dir = "/";
				/* hack for win32: /C:... -> C:/... */
				if(dir[1] >= 'A' && dir[1] <= 'Z' && dir[2] == ':')
					dir[0] = dir[1], dir[1] = ':', dir[2] = '/';
				fprint(fd[Fctl], "dir %q", dir);
			}
		}
	}

	if(nice != 0)
		fprint(fd[Fctl], "nice %d", nice);

	if(foreground)
		fprint(fd[Fctl], "killonclose");

	s = seprint(buf, &buf[sizeof(buf)], "exec");
	while(*argv != nil){
		s = seprint(s, &buf[sizeof(buf)], " %q", *argv++);
		if(s >= &buf[sizeof(buf)-1])
			sysfatal("too many arguments");
	}

	if(write(fd[Fctl], buf, s - buf) < 0)
		sysfatal("write: %r");

	if((pid[Preadwait] = fork()) == -1)
		sysfatal("fork: %r");
	if(pid[Preadwait] == 0){
		fd01(fd[Fwait], 2);
		if((n = read(0, buf, sizeof(buf)-1)) < 0)
			rerrstr(buf, sizeof(buf));
		else {
			char *f[5];

			while(n > 0 && buf[n-1] == '\n')
				n--;
			buf[n] = 0;
			if(tokenize(buf, f, 5) == 5)
				exits(f[4]);
		}
		exits(buf);
	}

	if(foreground){
		if((pid[Pcopyerr] = fork()) == -1)
			sysfatal("fork: %r");
		if(pid[Pcopyerr] == 0){
			fd01(fd[Fstderr], 2);
			copy();
			rerrstr(buf, sizeof(buf));
			exits(buf);
		}
		if((pid[Pcopyout] = fork()) == -1)
			sysfatal("fork: %r");
		if(pid[Pcopyout] == 0){
			fd01(fd[Fstdout], 1);
			copy();
			rerrstr(buf, sizeof(buf));
			exits(buf);
		}
		if((pid[Pcopyin] = fork()) == -1)
			sysfatal("fork: %r");
		if(pid[Pcopyin] == 0){
			fd01(0, fd[Fstdin]);
			copy();
			rerrstr(buf, sizeof(buf));
			exits(buf);
		}
	}
	fd01(fd[Fctl], 2);
	atexit(killstdin);
	atnotify(catch, 1);

	while((w = wait()) != nil){
		if((s = strstr(w->msg, ": ")) == nil)
			s = w->msg;
		else
			s += 2;
		for(n = 0; n < Npid; n++){
			if(pid[n] == w->pid){
				pid[n] = 0;
				break;
			}
		}
		if(n == Preadwait)
			exits(s);
		free(w);
	}
}
