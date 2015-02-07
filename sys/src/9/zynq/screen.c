#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

Cursor	arrow = {
	{ -1, -1 },
	{ 0xFF, 0xFF, 0x80, 0x01, 0x80, 0x02, 0x80, 0x0C,
	  0x80, 0x10, 0x80, 0x10, 0x80, 0x08, 0x80, 0x04,
	  0x80, 0x02, 0x80, 0x01, 0x80, 0x02, 0x8C, 0x04,
	  0x92, 0x08, 0x91, 0x10, 0xA0, 0xA0, 0xC0, 0x40,
	},
	{ 0x00, 0x00, 0x7F, 0xFE, 0x7F, 0xFC, 0x7F, 0xF0,
	  0x7F, 0xE0, 0x7F, 0xE0, 0x7F, 0xF0, 0x7F, 0xF8,
	  0x7F, 0xFC, 0x7F, 0xFE, 0x7F, 0xFC, 0x73, 0xF8,
	  0x61, 0xF0, 0x60, 0xE0, 0x40, 0x40, 0x00, 0x00,
	},
};

Memimage *gscreen;
static Memdata xgdata;

static Memimage xgscreen =
{
	{ 0, 0, 800, 600 },	/* r */
	{ 0, 0, 800, 600 },	/* clipr */
	24,			/* depth */
	3,			/* nchan */
	BGR24,			/* chan */
	nil,			/* cmap */
	&xgdata,		/* data */
	0,			/* zero */
	0, 			/* width in words of a single scan line */
	0,			/* layer */
	0,			/* flags */
};

void
cursoron(void)
{
}

void
cursoroff(void)
{
}

void
setcursor(Cursor*)
{
}

void
flushmemscreen(Rectangle)
{
}

void
drawflushreal(void)
{
	uchar *fb, *fbe;
	
	fb = xgdata.bdata;
	fbe = fb + Dx(xgscreen.r) * Dy(xgscreen.r) * 3;
	cleandse(fb, fbe);
	clean2pa(PADDR(fb), PADDR(fbe));
}

void
screeninit(void)
{
	uchar *fb;

	fb = xspanalloc(Dx(xgscreen.r) * Dy(xgscreen.r) * 3, 64, 0);
	print("%p\n", PADDR(fb));
	memsetchan(&xgscreen, BGR24);
	conf.monitor = 1;
	xgdata.bdata = fb;
	xgdata.ref = 1;
	gscreen = &xgscreen;
	gscreen->width = wordsperline(gscreen->r, gscreen->depth);

	memimageinit();
}

uchar*
attachscreen(Rectangle *r, ulong *chan, int* d, int *width, int *softscreen)
{
	*r = gscreen->r;
	*d = gscreen->depth;
	*chan = gscreen->chan;
	*width = gscreen->width;
	*softscreen = 0;

	return gscreen->data->bdata;
}

void
getcolor(ulong, ulong *, ulong *, ulong *)
{
}

int
setcolor(ulong, ulong, ulong, ulong)
{
	return 0;
}

void
blankscreen(int)
{
}

void
mousectl(Cmdbuf *)
{
}
