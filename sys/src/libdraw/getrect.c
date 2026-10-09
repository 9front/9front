#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>

extern Rectangle gengetrect(int, Mouse*, void(*)(Mouse*, Cursor*), void(*)(Mouse*), Screen*);

static void
_setcursor(Mouse *m, Cursor *c)
{
	setcursor((Mousectl*)m, c);
}

static void
_readmouse(Mouse *m)
{
	readmouse((Mousectl*)m);
}

Rectangle
getrect(int but, Mousectl *mc)
{
	return gengetrect(but, mc, _setcursor, _readmouse, nil);
}
