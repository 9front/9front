#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

void
threadsrv(Srv *s)
{
	if(s->forker == nil)
		s->forker = threadsrvforker;
	srv(s);
}
