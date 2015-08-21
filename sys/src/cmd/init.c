#include <u.h>
#include <libc.h>
#include <auth.h>

char*	readfile(char*);
char*	readenv(char*);
void	setenv(char*, char*);
void	cpenv(char*, char*);
void	closefds(void);
int	procopen(int, char*, int);
void	fexec(void(*)(void));
void	rcexec(void);
void	cpustart(void);

char	*service;
char	*cmd;
char	*cpu;
char	*systemname;
int	manual;
int	iscpu;

void
main(int argc, char *argv[])
{
	char *user;
	int fd;

	closefds();

	service = "cpu";
	manual = 0;
	ARGBEGIN{
	case 'c':
		service = "cpu";
		break;
	case 'm':
		manual = 1;
		break;
	case 't':
		service = "terminal";
		break;
	}ARGEND
	cmd = *argv;

	fd = procopen(getpid(), "ctl", OWRITE);
	if(fd >= 0){
		if(write(fd, "pri 10", 6) != 6)
			fprint(2, "init: warning: can't set priority: %r\n");
		close(fd);
	}

	cpu = readenv("#e/cputype");
	setenv("#e/objtype", cpu);
	setenv("#e/service", service);
	cpenv("/adm/timezone/local", "#e/timezone");

	user = readenv("#c/user");
	systemname = readenv("#c/sysname");

	newns(user, 0);
	iscpu = strcmp(service, "cpu")==0;

	if(iscpu && manual == 0)
		fexec(cpustart);

	for(;;){
		fprint(2, "\ninit: starting /bin/rc\n");
		fexec(rcexec);
		manual = 1;
		cmd = 0;
		sleep(1000);
	}
}

static int gotnote;
static int interrupted;

void
pinhead(void*, char *msg)
{
	gotnote = 1;
	if(strcmp(msg, "interrupt") == 0)
		interrupted = 1;
	else
		fprint(2, "init got note '%s'\n", msg);
	noted(NCONT);
}

void
fexec(void (*execfn)(void))
{
	Waitmsg *w;
	int fd, pid;

	switch(pid=fork()){
	case 0:
		rfork(RFNOTEG);
		(*execfn)();
		fprint(2, "init: exec error: %r\n");
		exits("exec");
	case -1:
		fprint(2, "init: fork error: %r\n");
		return;
	default:
		fd = procopen(pid, "notepg", OWRITE);
	casedefault:
		notify(pinhead);
		interrupted = 0;
		gotnote = 0;
		w = wait();
		if(w == nil){
			if(interrupted && fd >= 0)
				write(fd, "interrupt", 9);
			if(gotnote)
				goto casedefault;
			fprint(2, "init: wait error: %r\n");
			break;
		}
		if(w->pid != pid){
			free(w);
			goto casedefault;
		}
		if(strstr(w->msg, "exec error") != 0){
			fprint(2, "init: exit string %s\n", w->msg);
			fprint(2, "init: sleeping because exec failed\n");
			for(;;)
				sleep(1000);
		}
		if(w->msg[0])
			fprint(2, "init: rc exit status: %s\n", w->msg);
		free(w);
		break;
	}
	if(fd >= 0)
		close(fd);
}

void
rcexec(void)
{
	if(cmd)
		execl("/bin/rc", "rc", "-c", cmd, nil);
	else if(manual || iscpu)
		execl("/bin/rc", "rc", nil);
	else if(strcmp(service, "terminal") == 0)
		execl("/bin/rc", "rc", "-c", ". /rc/bin/termrc; home=/usr/$user; cd && . ./lib/profile", nil);
	else
		execl("/bin/rc", "rc", nil);
}

void
cpustart(void)
{
	execl("/bin/rc", "rc", "-c", "/rc/bin/cpurc", nil);
}

char*
readfile(char *name)
{
	int f, len;
	Dir *d;
	char *val;

	f = open(name, OREAD);
	if(f < 0){
		fprint(2, "init: can't open %s: %r\n", name);
		return nil;	
	}
	d = dirfstat(f);
	if(d == nil){
		fprint(2, "init: can't stat %s: %r\n", name);
		close(f);
		return nil;
	}
	len = d->length;
	free(d);
	if(len == 0)	/* device files can be zero length but have contents */
		len = 64;
	val = malloc(len+1);
	if(val == nil){
		fprint(2, "init: can't malloc %s: %r\n", name);
		close(f);
		return nil;
	}
	len = read(f, val, len);
	close(f);
	if(len < 0){
		fprint(2, "init: can't read %s: %r\n", name);
		return nil;
	}else
		val[len] = '\0';
	return val;
}

char*
readenv(char *name)
{
	char *val;

	val = readfile(name);
	if(val == nil)
		val = "*unknown*";
	return val;
}

void
setenv(char *name, char *val)
{
	int fd;

	fd = create(name, OWRITE, 0644);
	if(fd < 0)
		fprint(2, "init: can't create %s: %r\n", name);
	else{
		write(fd, val, strlen(val));
		close(fd);
	}
}

void
cpenv(char *from, char *to)
{
	char *val;

	val = readfile(from);
	if(val != nil){
		setenv(to, val);
		free(val);
	}
}

/*
 *  clean up after /boot
 */
void
closefds(void)
{
	int i;

	for(i = 3; i < 30; i++)
		close(i);
}

int
procopen(int pid, char *name, int mode)
{
	char buf[128];
	int fd;

	snprint(buf, sizeof(buf), "#p/%d/%s", pid, name);
	fd = open(buf, mode);
	if(fd < 0)
		fprint(2, "init: warning: can't open %s: %r\n", name);
	return fd;
}
