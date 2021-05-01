#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

void
threadpostsharesrv(Srv *s, char *name, char *mtpt, char *desc)
{
	if(s->forker == nil)
		s->forker = threadsrvforker;
	postsharesrv(s, name, mtpt, desc);
}
