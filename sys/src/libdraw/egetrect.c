#include <u.h>
#include <libc.h>
#include <draw.h>
#include <cursor.h>
#include <event.h>

extern Rectangle gengetrect(int, Mouse*, void(*)(Mouse*, Cursor*), void(*)(Mouse*), Screen*);
extern void drawgetrect(Rectangle, int);

static void
_setcursor(Mouse *, Cursor *c)
{
	esetcursor(c);
}

static void
_readmouse(Mouse *m)
{
	*m = emouse();
}


Rectangle
egetrect(int but, Mouse *m)
{
	return gengetrect(but, m, _setcursor, _readmouse, nil);
}

void
edrawgetrect(Rectangle rc, int up)
{
	drawgetrect(rc, up);
}
