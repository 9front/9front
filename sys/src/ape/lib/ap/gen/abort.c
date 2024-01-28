#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

_Noreturn void
abort(void)
{
	kill(getpid(), SIGABRT);
}
