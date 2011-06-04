#include <u.h>
#include <libc.h>
#include <../boot/boot.h>

void
fatal(char *s)
{
	char buf[ERRMAX];

	buf[0] = '\0';
	errstr(buf, sizeof buf);
	fprint(2, "boot: %s: %s\n", s, buf);
	exits(0);
}

int
readfile(char *name, char *buf, int len)
{
	int f, n;

	buf[0] = 0;
	f = open(name, OREAD);
	if(f < 0)
		return -1;
	n = read(f, buf, len-1);
	if(n >= 0)
		buf[n] = 0;
	close(f);
	return 0;
}

void
run(char *file, ...)
{
	char buf[64];
	Waitmsg *w;
	int pid;

	switch(pid = fork()){
	case -1:
		fatal("fork");
	case 0:
		exec(file, &file);
		snprint(buf, sizeof buf, "can't exec %s", file);
		fatal(buf);
	default:
		while((w = wait()) != nil)
			if(w->pid == pid)
				break;
		if(w == nil){
			snprint(buf, sizeof buf, "wait returned nil running %s", file);
			free(w);
			fatal(buf);
		}
		free(w);
	}
}

int
writefile(char *name, char *buf, int len)
{
	int f, n;

	f = open(name, OWRITE);
	if(f < 0)
		return -1;
	n = write(f, buf, len);
	close(f);
	return (n != len) ? -1 : 0;
}

void
setenv(char *name, char *val, int ec)
{
	int f;
	char ename[64];

	snprint(ename, sizeof ename, "#e%s/%s", ec ? "c" : "", name);
	f = create(ename, 1, 0666);
	if(f < 0){
		fprint(2, "create %s: %r\n", ename);
		return;
	}
	write(f, val, strlen(val));
	close(f);
}
