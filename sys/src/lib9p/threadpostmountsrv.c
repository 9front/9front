#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

void
threadpostmountsrv(Srv *s, char *name, char *mtpt, int flag)
{
	if(s->forker == nil)
		s->forker = threadsrvforker;
	postmountsrv(s, name, mtpt, flag);
}
