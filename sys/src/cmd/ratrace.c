#include <u.h>
#include <libc.h>
#include <thread.h>

enum {
	Stacksize	= 8*1024,
};

Channel *out;
Channel *quit;
Channel *forkc;
int readers = 1;

typedef struct Msg Msg;
struct Msg {
	int	pid;
	char	buf[8*1024];
};

typedef struct Reader Reader;
struct Reader {
	int	pid;

	int	tfd;
	int	cfd;

	Msg*	msg;
};

void
die(Reader *r)
{
	Msg *s;

	s = r->msg;
	snprint(s->buf, sizeof(s->buf), " = %r\n");
	s->pid = r->pid;
	sendp(quit, s);
	if(r->tfd >= 0)
		close(r->tfd);
	if(r->cfd >= 0)
		close(r->cfd);
	threadexits(nil);
}

void
cwrite(Reader *r, char *cmd)
{
	if (write(r->cfd, cmd, strlen(cmd)) < 0)
		die(r);
}

void
reader(void *v)
{
	int forking = 0, newpid;
	Reader r;
	Msg *s;

	r.pid = (int)(uintptr)v;
	r.tfd = r.cfd = -1;

	r.msg = s = mallocz(sizeof(Msg), 1);
	snprint(s->buf, sizeof(s->buf), "/proc/%d/ctl", r.pid);
	if ((r.cfd = open(s->buf, OWRITE)) < 0)
		die(&r);
	snprint(s->buf, sizeof(s->buf), "/proc/%d/syscall", r.pid);
	if ((r.tfd = open(s->buf, OREAD)) < 0)
		die(&r);

	cwrite(&r, "stop");
	cwrite(&r, "startsyscall");

	while(pread(r.tfd, s->buf, sizeof(s->buf)-1, 0) > 0){
		if (forking && s->buf[1] == '=' && s->buf[3] != '-') {
			forking = 0;
			newpid = strtol(&s->buf[3], 0, 0);
			sendp(forkc, (void*)newpid);
			procrfork(reader, (void*)newpid, Stacksize, 0);
		}

		/*
		 * There are three tests here and they (I hope) guarantee
		 * no false positives.
		 */
		if (strstr(s->buf, " Rfork") != nil) {
			char *a[8];
			char *rf;

			rf = strdup(s->buf);
         		if (tokenize(rf, a, 8) == 5) {
				ulong flags;

				flags = strtoul(a[4], 0, 16);
				if (flags & RFPROC)
					forking = 1;
			}
			free(rf);
		}
		s->pid = r.pid;
		sendp(out, s);

		r.msg = s = mallocz(sizeof(Msg), 1);
		cwrite(&r, "startsyscall");
	}
	die(&r);
}

void
writer(int lastpid)
{
	char lastc = -1;
	Alt a[4];
	Msg *s;
	int n;

	a[0].op = CHANRCV;
	a[0].c = quit;
	a[0].v = &s;
	a[1].op = CHANRCV;
	a[1].c = out;
	a[1].v = &s;
	a[2].op = CHANRCV;
	a[2].c = forkc;
	a[2].v = nil;
	a[3].op = CHANEND;

	while(readers > 0){
		switch(alt(a)){
		case 0:
			readers--;
		case 1:
			if(s->pid != lastpid){
				lastpid = s->pid;
				if(lastc != '\n'){
					lastc = '\n';
					write(2, &lastc, 1);
				}
				if(s->buf[1] == '=')
					fprint(2, "%d ...", lastpid);
			}
			n = strlen(s->buf);
			if(n > 0){
				write(2, s->buf, n);
				lastc = s->buf[n-1];
			}
			free(s);
			break;
		case 2:
			readers++;
			break;
		}
	}
}

void
usage(void)
{
	fprint(2, "Usage: ratrace [-c cmd [arg...]] | [pid]\n");
	threadexits("usage");
}

void
threadmain(int argc, char **argv)
{
	int pid;
	char *cmd = nil;
	char **args = nil;

	/*
	 * don't bother with fancy arg processing, because it picks up options
	 * for the command you are starting.  Just check for -c as argv[1]
	 * and then take it from there.
	 */
	if (argc < 2)
		usage();
	if (argv[1][0] == '-')
		switch(argv[1][1]) {
		case 'c':
			if (argc < 3)
				usage();
			cmd = strdup(argv[2]);
			args = &argv[2];
			break;
		default:
			usage();
		}

	/* run a command? */
	if(cmd) {
		pid = fork();
		if (pid < 0)
			sysfatal("fork failed: %r");
		if(pid == 0) {
			write(open(smprint("/proc/%d/ctl", getpid()), OWRITE|OCEXEC), "hang", 4);
			exec(cmd, args);
			if(cmd[0] != '/')
				exec(smprint("/bin/%s", cmd), args);
			sysfatal("exec %s failed: %r", cmd);
		}
	} else {
		if(argc != 2)
			usage();
		pid = atoi(argv[1]);
	}

	out   = chancreate(sizeof(Msg*), 0);
	quit  = chancreate(sizeof(Msg*), 0);
	forkc = chancreate(sizeof(void*), 0);
	procrfork(reader, (void*)pid, Stacksize, 0);

	writer(pid);
	threadexits(nil);
}
