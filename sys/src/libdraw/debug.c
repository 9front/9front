#include <u.h>
#include <libc.h>
#include <draw.h>

void
drawsetdebug(int v)
{
	_lockdisplay(display);
	if(drawcmd(display, "bb", 'D', v) < 0){
		_unlockdisplay(display);
		fprint(2, "drawsetdebug: %r\n");
		return;
	}
	_unlockdisplay(display);
}
