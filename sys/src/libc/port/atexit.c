#include <u.h>
#include <libc.h>

extern void (*_onexit)(void);

typedef struct Onex Onex;
struct Onex{
	void	(*f)(void);
	int	pid;
};

static Lock onexlock;
static Onex onex[33];

static void
onexit(void)
{
	int i, pid;
	void (*f)(void);

	pid = getpid();
	for(i = nelem(onex)-1; i >= 0; i--)
		if((f = onex[i].f) != nil && onex[i].pid == pid) {
			onex[i].f = nil;
			(*f)();
		}
}

int
atexit(void (*f)(void))
{
	int i;

	_onexit = onexit;
	lock(&onexlock);
	for(i=0; i<nelem(onex); i++)
		if(onex[i].f == nil) {
			onex[i].pid = getpid();
			onex[i].f = f;
			unlock(&onexlock);
			return 1;
		}
	unlock(&onexlock);
	return 0;
}

void
atexitdont(void (*f)(void))
{
	int i, pid;

	pid = getpid();
	for(i=0; i<nelem(onex); i++)
		if(onex[i].f == f && onex[i].pid == pid)
			onex[i].f = nil;
}
