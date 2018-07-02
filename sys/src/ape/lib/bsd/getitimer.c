#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

typedef struct Timer Timer;
struct Timer {
	int pid, signal;
	struct itimerval itimer;
};

Timer timers[3] = {
	{0, SIGALRM},
	{0, SIGVTALRM},
	{0, SIGPROF},
};

void
timerloop(int signal, const struct timeval tval)
{
	pid_t ppid;
	struct timespec t, s;

	ppid = getppid();
	t.tv_sec = tval.tv_sec;
	t.tv_nsec = tval.tv_usec*1000;
	for(;;){
		nanosleep(&t, &s);
		kill(ppid, signal);
	}
}

int
setitimer(int which, const struct itimerval *new, struct itimerval *curr)
{
	pid_t pid;
	int status;
	Timer *timer;

	if(which < 0 || which >= 3){
		errno = EINVAL;
		return -1;
	}

	timer = timers+which;
	if(timer->pid != 0){
		kill(timer->pid, SIGKILL);
		waitpid(timer->pid, &status, 0);
	}

	switch(pid = fork()){
	default:
		timer->pid = pid;
		if(curr != NULL)
			*curr = timer->itimer;
		timer->itimer = *new;
		break;
	case -1:
		errno = EFAULT;
		return -1;
	case 0:
		timerloop(timer->signal, new->it_interval);
		exit(0);
	}
	return 0;
}

int
getitimer(int which, struct itimerval *curr)
{
	Timer *timer;

	if(which < 0 || which >= 3){
		errno = EINVAL;
		return -1;
	}

	timer = timers+which;
	*curr = timer->itimer;
	return 0;
}
