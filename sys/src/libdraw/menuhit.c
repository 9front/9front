#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <thread.h>

extern int genmenuhit(int but, Mouse *m, void (*_readmouse)(Mouse*), void (*_moveto)(Mouse*, Point), Menu *menu, Screen *scr);

static void
_readmouse(Mouse *m)
{
	readmouse((Mousectl*)m);
}

static void
_moveto(Mouse *m, Point pt)
{
	moveto((Mousectl*)m, pt);
}

int
menuhit(int but, Mousectl *mc, Menu *menu, Screen *scr)
{
	return genmenuhit(but, mc, _readmouse, _moveto, menu, scr);
}
