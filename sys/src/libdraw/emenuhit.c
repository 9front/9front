#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

extern int genmenuhit(int but, Mouse *m, void (*readmouse)(Mouse*), void (*moveto)(Mouse*, Point), Menu *menu, Screen *scr);

static void
_readmouse(Mouse *m)
{
	*m = emouse();
}

static void
_moveto(Mouse*, Point pt)
{
	emoveto(pt);
}

int
emenuhit(int but, Mouse *m, Menu *menu)
{
	return genmenuhit(but, m, _readmouse, _moveto, menu, nil);
}
