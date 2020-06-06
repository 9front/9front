/*
 * vga driver using just vesa bios to set up.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#define Ureg Ureg386
#include "/386/include/ureg.h"
typedef struct Ureg386 Ureg386;

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

enum {
	RealModeBuf = 0x9000,
};

static uchar modebuf[0x1000];
static Chan *creg, *cmem;

#define WORD(p) ((p)[0] | ((p)[1]<<8))
#define LONG(p) ((p)[0] | ((p)[1]<<8) | ((p)[2]<<16) | ((p)[3]<<24))
#define PWORD(p, v) (p)[0] = (v); (p)[1] = (v)>>8
#define PLONG(p, v) (p)[0] = (v); (p)[1] = (v)>>8; (p)[2] = (v)>>16; (p)[3] = (v)>>24

typedef struct Vmode Vmode;
struct Vmode
{
	char	chan[32];
	int	attr;	/* flags */
	int	bpl;
	int	dx, dy;
	int	depth;
	ulong	paddr;
};

static uchar*
vbesetup(Ureg386 *u, int ax)
{
	memset(modebuf, 0, sizeof modebuf);
	memset(u, 0, sizeof *u);
	u->ax = ax;
	u->es = (RealModeBuf>>4)&0xF000;
	u->di = RealModeBuf&0xFFFF;
	return modebuf;
}

static void
vbecall(Ureg386 *u)
{
	if(devtab[cmem->type]->write(cmem, modebuf, sizeof(modebuf), RealModeBuf) != sizeof(modebuf))
		error("write modebuf");
	u->trap = 0x10;
	if(devtab[creg->type]->write(creg, u, sizeof(*u), 0) != sizeof(*u))
		error("write ureg");
	if(devtab[creg->type]->read(creg, u, sizeof(*u), 0) != sizeof(*u))
		error("read ureg");
	if((u->ax&0xFFFF) != 0x004F)
		error("vesa bios error");
	if(devtab[cmem->type]->read(cmem, modebuf, sizeof(modebuf), RealModeBuf) != sizeof(modebuf))
		error("read modebuf");
}

static void
vbecheck(void)
{
	Ureg386 u;
	uchar *p;

	p = vbesetup(&u, 0x4F00);
	strcpy((char*)p, "VBE2");
	vbecall(&u);
	if(memcmp((char*)p, "VESA", 4) != 0)
		error("bad vesa signature");
	if(p[5] < 2)
		error("bad vesa version");
}

static int
vbegetmode(void)
{
	Ureg386 u;

	vbesetup(&u, 0x4F03);
	vbecall(&u);
	return u.bx;
}

static uchar*
vbemodeinfo(int mode)
{
	uchar *p;
	Ureg386 u;

	p = vbesetup(&u, 0x4F01);
	u.cx = mode;
	vbecall(&u);
	return p;
}

static char*
vmode(Vmode *m, uchar *p)
{
	m->attr = WORD(p);
	if(!(m->attr & (1<<4)))
		return "not in VESA graphics mode";
	if(!(m->attr & (1<<7)))
		return "not in linear graphics mode";
	m->bpl = WORD(p+16);
	m->dx = WORD(p+18);
	m->dy = WORD(p+20);
	m->depth = p[25];
	m->paddr = LONG(p+40);
	if(m->depth <= 8)
		snprint(m->chan, sizeof m->chan, "%c%d", 
			(m->attr & (1<<3)) ? 'm' : 'k', m->depth);
	else
		rgbmask2chan(m->chan, m->depth,
			(1UL<<p[31])-1 << p[32],
			(1UL<<p[33])-1 << p[34],
			(1UL<<p[35])-1 << p[36]);
	return nil;
}

static void
vesalinear(VGAscr *scr, int, int)
{
	int i, mode, size, havesize;
	Pcidev *pci;
	char *err;
	Vmode m;

	vbecheck();
	mode = vbegetmode();
	/*
	 * bochs loses the top bits - cannot use this
	if((mode&(1<<14)) == 0)
		error("not in linear graphics mode");
	 */
	mode &= 0x3FFF;
	if((err = vmode(&m, vbemodeinfo(mode))) != nil)
		error(err);

	size = m.dy * m.bpl;

	/*
	 * figure out max size of memory so that we have
	 * enough if the screen is resized.
	 */
	pci = nil;
	havesize = 0;
	while(!havesize && (pci = pcimatch(pci, 0, 0)) != nil){
		if(pci->ccrb != Pcibcdisp)
			continue;
		for(i=0; i<nelem(pci->mem); i++){
			uvlong a, e;

			if(pci->mem[i].size == 0 || (pci->mem[i].bar & 1) != 0)
				continue;
			a = pci->mem[i].bar & ~0xF;
			e = a + pci->mem[i].size;
			if(m.paddr >= a && (m.paddr+size) <= e){
				size = e - m.paddr;
				havesize = 1;
				break;
			}
		}
	}

	/* no pci - heuristic guess */
	if(!havesize)
		if(size < 4*1024*1024)
			size = 4*1024*1024;
		else
			size = ROUND(size, 1024*1024);

	vgalinearaddr(scr, m.paddr, size);
	if(scr->apsize)
		addvgaseg("vesascreen", scr->paddr, scr->apsize);

	scr->softscreen = 1;
}

static void
vesaenable(VGAscr *)
{
	cmem = namec("/dev/realmodemem", Aopen, ORDWR, 0);
	if(waserror()){
		cclose(cmem);
		cmem = nil;
		nexterror();
	}
	creg = namec("/dev/realmode", Aopen, ORDWR, 0);
	poperror();
}

static void
vesadisable(VGAscr *)
{
	if(cmem != nil)
		cclose(cmem);
	if(creg != nil)
		cclose(creg);
	cmem = creg = nil;
}

static void
vesablank(VGAscr *, int blank)
{
	Ureg386 u;

	vbesetup(&u, 0x4f10);
	u.bx = blank ? 0x0101 : 0x0001;

	/*
	 * dont wait forever when called from mouse kproc.
	 * some BIOS get stuck in i/o poll loop after
	 * blank/unblank for some reason. (Thinkpad A22p)
	 */
	if(up->kp)
		procalarm(10000);

	if(!waserror()){
		vbecall(&u);
		poperror();
	}

	if(up->kp){
		procalarm(0);
		up->notepending = 0;
	}
}

static void
vesadrawinit(VGAscr *scr)
{
	scr->blank = vesablank;
}

VGAdev vgavesadev = {
	"vesa",
	vesaenable,
	vesadisable,
	0,
	vesalinear,
	vesadrawinit,
};

/*
 * called from multibootargs() to convert
 * vbe mode info (passed from bootloader)
 * to *bootscreen= parameter
 */
char*
vesabootscreenconf(char *s, char *e, uchar *p)
{
	Vmode m;

	if(vmode(&m, p) != nil)
		return s;
	return seprint(s, e, "*bootscreen=%dx%dx%d %s %#lux\n",
		m.bpl * 8 / m.depth, m.dy, m.depth, m.chan, m.paddr);
}
