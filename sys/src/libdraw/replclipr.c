#include <u.h>
#include <libc.h>
#include <draw.h>

void
replclipr(Image *i, int repl, Rectangle clipr)
{
	_lockdisplay(i->display);
	if(drawcmd(i->display, "blbR", 'c', i->id, repl != 0, &clipr) < 0){
		_unlockdisplay(i->display);
		fprint(2, "replclipr: %r\n");
		return;
	}
	_unlockdisplay(i->display);
	i->repl = repl;
	i->clipr = clipr;
}
