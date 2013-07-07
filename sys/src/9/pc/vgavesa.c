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
#include "ureg.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

enum {
	Cdisable = 0,
	Cenable,
	Cblank,

	RealModeBuf = 0x9000,
};

static uchar modebuf[0x1000];
static Chan *creg, *cmem;
static QLock vesaq;
static Rendez vesar;
static int vesactl;

#define WORD(p) ((p)[0] | ((p)[1]<<8))
#define LONG(p) ((p)[0] | ((p)[1]<<8) | ((p)[2]<<16) | ((p)[3]<<24))
#define PWORD(p, v) (p)[0] = (v); (p)[1] = (v)>>8
#define PLONG(p, v) (p)[0] = (v); (p)[1] = (v)>>8; (p)[2] = (v)>>16; (p)[3] = (v)>>24

static uchar*
vbesetup(Ureg *u, int ax)
{
	memset(modebuf, 0, sizeof modebuf);
	memset(u, 0, sizeof *u);
	u->ax = ax;
	u->es = (RealModeBuf>>4)&0xF000;
	u->di = RealModeBuf&0xFFFF;
	return modebuf;
}

static void
vbecall(Ureg *u)
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
	Ureg u;
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
	Ureg u;

	vbesetup(&u, 0x4F03);
	vbecall(&u);
	return u.bx;
}

static uchar*
vbemodeinfo(int mode)
{
	uchar *p;
	Ureg u;

	p = vbesetup(&u, 0x4F01);
	u.cx = mode;
	vbecall(&u);
	return p;
}

static void
vesalinear(VGAscr *scr, int, int)
{
	int i, mode, size, havesize;
	ulong paddr;
	Pcidev *pci;
	uchar *p;

	vbecheck();
	mode = vbegetmode();
	/*
	 * bochs loses the top bits - cannot use this
	if((mode&(1<<14)) == 0)
		error("not in linear graphics mode");
	 */
	mode &= 0x3FFF;
	p = vbemodeinfo(mode);
	if(!(WORD(p+0) & (1<<4)))
		error("not in VESA graphics mode");
	if(!(WORD(p+0) & (1<<7)))
		error("not in linear graphics mode");

	paddr = LONG(p+40);
	size = WORD(p+20)*WORD(p+16);

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
			ulong a, e;

			if(pci->mem[i].bar&1)	/* not memory */
				continue;
			a = pci->mem[i].bar & ~0xF;
			e = a + pci->mem[i].size;
			if(paddr >= a && (paddr+size) <= e){
				size = e - paddr;
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

	vgalinearaddr(scr, paddr, size);
	if(scr->apsize)
		addvgaseg("vesascreen", scr->paddr, scr->apsize);

	scr->softscreen = 1;
}

static int
gotctl(void *arg)
{
	return vesactl != *((int*)arg);
}

static void
vesaproc(void*)
{
	Ureg u;
	int ctl;

	ctl = Cenable;
	while(ctl != Cdisable){
		if(!waserror()){
			sleep(&vesar, gotctl, &ctl);
			ctl = vesactl;

			vbesetup(&u, 0x4f10);
			if(ctl == Cblank)
				u.bx = 0x0101;
			else	
				u.bx = 0x0001;

			/*
			 * dont wait forever here. some BIOS get stuck
			 * in i/o poll loop after blank/unblank for some
			 * reason. (Thinkpad A22p)
			 */
			procalarm(10000);
			vbecall(&u);

			poperror();
		}
		procalarm(0);
		up->notepending = 0;
	}
	cclose(cmem);
	cclose(creg);
	cmem = creg = nil;
	qunlock(&vesaq);

	pexit("", 1);
}

static void
vesaenable(VGAscr *)
{
	eqlock(&vesaq);
	if(waserror()){
		qunlock(&vesaq);
		nexterror();
	}
	cmem = namec("/dev/realmodemem", Aopen, ORDWR, 0);
	if(waserror()){
		cclose(cmem);
		cmem = nil;
		nexterror();
	}
	creg = namec("/dev/realmode", Aopen, ORDWR, 0);
	poperror();
	poperror();

	vesactl = Cenable;
	kproc("vesa", vesaproc, nil);
}

static void
vesadisable(VGAscr *)
{
	vesactl = Cdisable;
	wakeup(&vesar);

	/* wait for vesaproc to finish */
	qlock(&vesaq);
	qunlock(&vesaq);
}

static void
vesablank(VGAscr *, int blank)
{
	if(vesactl != Cdisable){
		vesactl = blank ? Cblank : Cenable;
		wakeup(&vesar);
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
