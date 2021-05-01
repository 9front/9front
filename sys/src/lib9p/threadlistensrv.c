#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

void
threadlistensrv(Srv *s, char *addr)
{
	if(s->forker == nil)
		s->forker = threadsrvforker;
	listensrv(s, addr);
}
