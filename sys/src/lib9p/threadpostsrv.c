#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

int
threadpostsrv(Srv *s, char *name)
{
	if(s->forker == nil)
		s->forker = threadsrvforker;
	return postsrv(s, name);
}
