#include <u.h>
#include <libc.h>
#include <thread.h>
#include "threadimpl.h"

extern void (*_sysfatal)(char*, va_list);
extern void (*__assert)(char*);

int	mainstacksize;

static jmp_buf mainjmp;
static int mainargc;

static void
mainlauncher(void *arg)
{
	threadmain(mainargc, arg);
	threadexits("threadmain");
}

void
main(int argc, char **argv)
{
	rfork(RFREND);

//_threaddebuglevel = (DBGSCHED|DBGCHAN|DBGREND)^~0;
	_threadprocp = privalloc();
	_qlockinit(_threadrendezvous);
	_sysfatal = _threadsysfatal;
	__assert = _threadassert;
	notify(_threadnote);
	if(mainstacksize == 0)
		mainstacksize = 8*1024;
	mainargc = argc;
	_threadsetproc(_newproc(mainlauncher, argv, mainstacksize, "threadmain", 0, 0));
	setjmp(mainjmp);
	_schedinit();
	abort();	/* not reached */
}

static void
efork(Execargs *e)
{
	char buf[ERRMAX];

	_threaddebug(DBGEXEC, "_schedexec %s", e->prog);
	close(e->fd[0]);
	exec(e->prog, e->args);
	_threaddebug(DBGEXEC, "_schedexec failed: %r");
	rerrstr(buf, sizeof buf);
	if(buf[0]=='\0')
		strcpy(buf, "exec failed");
	write(e->fd[1], buf, strlen(buf));
	_exits(buf);
}

int
_schedexec(Execargs *e)
{
	int pid, flag;

	flag = (_threadwaitchan == nil) ? RFNOWAIT : 0;
	switch(pid = rfork(RFREND|RFNOTEG|RFFDG|RFMEM|RFPROC|flag)){
	case 0:
		efork(e);
	default:
		return pid;
	}
}

int
_schedfork(Proc *p)
{
	int pid;

	switch(pid = rfork(RFPROC|RFMEM|RFNOWAIT|p->rforkflag)){
	case 0:
		_threadsetproc(p);
		longjmp(mainjmp, 1);
	default:
		return pid;
	}
}

void
_schedexit(Proc *p)
{
	char ex[ERRMAX];

	utfecpy(ex, ex+sizeof ex, p->exitstr);
	free(p);
	_exits(ex);
}

void
_schedexecwait(void)
{
	int pid;
	Channel *c;
	Proc *p;
	Thread *t;
	Waitmsg *w;

	p = _threadgetproc();
	t = p->thread;
	pid = t->ret;
	_threaddebug(DBGEXEC, "_schedexecwait %d", t->ret);

	rfork(RFCFDG);
	for(;;){
		w = wait();
		if(w == nil)
			break;
		if(w->pid == pid)
			break;
		free(w);
	}
	if(w != nil){
		if((c = _threadwaitchan) != nil)
			sendp(c, w);
		else
			free(w);
	}
	threadexits("procexec");
}
