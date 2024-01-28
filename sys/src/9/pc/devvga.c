/*
 * VGA controller
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

enum {
	Qdir,
	Qvgactl,
};

static Dirtab vgadir[] = {
	".",		{ Qdir, 0, QTDIR },	0,	0550,
	"vgactl",	{ Qvgactl, 0 },		0,	0660,
};

enum {
	CMactualsize,
	CMdrawinit,
	CMhwaccel,
	CMhwblank,
	CMhwgc,
	CMlinear,
	CMpalettedepth,
	CMsize,
	CMtextmode,
	CMtype,
	CMsoftscreen,
	CMpcidev,
	CMtilt,
};

static Cmdtab vgactlmsg[] = {
	CMactualsize,	"actualsize",	2,
	CMdrawinit,	"drawinit",	1,
	CMhwaccel,	"hwaccel",	2,
	CMhwblank,	"hwblank",	2,
	CMhwgc,		"hwgc",		2,
	CMlinear,	"linear",	0,
	CMpalettedepth,	"palettedepth",	2,
	CMsize,		"size",		3,
	CMtextmode,	"textmode",	1,
	CMtype,		"type",		2,
	CMsoftscreen,	"softscreen",	2,
	CMpcidev,	"pcidev",	2,
	CMtilt,		"tilt",		2,
};

static void
vgareset(void)
{
	Pcidev *pci;
	VGAscr *scr;

	/* reserve the 'standard' vga registers */
	if(ioalloc(0x2b0, 0x2df-0x2b0+1, 0, "vga") < 0)
		panic("vga ports already allocated");
	if(ioalloc(0x3c0, 0x3da-0x3c0+1, 0, "vga") < 0)
		panic("vga ports already allocated");

	/* find graphics card pci device */
	scr = &vgascreen[0];
	scr->pci = pci = nil;
	while((pci = pcimatch(pci, 0, 0)) != nil){
		if(pci->ccrb == Pcibcdisp){
			scr->pci = pci;
			break;
		}
	}

	conf.monitor = 1;
}

static void
vgashutdown(void)
{
	VGAscr *scr;

	scr = &vgascreen[0];
	if(scr->cur && scr->cur->disable)
		scr->cur->disable(scr);
}

static Chan*
vgaattach(char* spec)
{
	if(*spec && strcmp(spec, "0"))
		error(Enodev);
	return devattach('v', spec);
}

Walkqid*
vgawalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, vgadir, nelem(vgadir), devgen);
}

static int
vgastat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, vgadir, nelem(vgadir), devgen);
}

static Chan*
vgaopen(Chan* c, int omode)
{
	return devopen(c, omode, vgadir, nelem(vgadir), devgen);
}

static void
vgaclose(Chan*)
{
}

static long
vgaread(Chan* c, void* a, long n, vlong off)
{
	char *p, *s, *e;
	VGAscr *scr;
	ulong offset = off;
	char chbuf[30];

	switch((ulong)c->qid.path){

	case Qdir:
		return devdirread(c, a, n, vgadir, nelem(vgadir), devgen);

	case Qvgactl:
		scr = &vgascreen[0];

		s = smalloc(READSTR);
		if(waserror()){
			free(s);
			nexterror();
		}
		p = s, e = s+READSTR;
		p = seprint(p, e, "type %s\n",
			scr->dev != nil ? scr->dev->name : "cga");
		if(scr->gscreen != nil) {
			Rectangle r;

			p = seprint(p, e, "size %dx%dx%d %s\n",
				scr->width, scr->height, scr->gscreen->depth,
				chantostr(chbuf, scr->gscreen->chan));
			r = actualscreensize(scr);
			if(scr->width != r.max.x || scr->height != r.max.y)
				p = seprint(p, e, "actualsize %dx%d\n", r.max.x, r.max.y);
			p = seprint(p, e, "tilt %s\n", tiltstr[scr->tilt]);
		}
		p = seprint(p, e, "hwgc %s\n",
			scr->cur != nil ? scr->cur->name : "off");
		p = seprint(p, e, "hwaccel %s\n", hwaccel ? "on" : "off");
		p = seprint(p, e, "hwblank %s\n", hwblank ? "on" : "off");
		p = seprint(p, e, "addr p 0x%.8llux v %#p size %#ux\n",
			scr->paddr, scr->vaddr, scr->apsize);
		p = seprint(p, e, "softscreen %s\n", scr->softscreen ? "on" : "off");
		USED(p);
		n = readstr(offset, a, n, s);
		poperror();
		free(s);

		return n;

	default:
		error(Egreg);
		break;
	}
}

static char Ebusy[] = "vga already configured";
static char Enoscreen[] = "set the screen size first";

static void
vgactl(VGAscr *scr, Cmdbuf *cb)
{
	int align, size, tilt, z, i;
	Rectangle r;
	char *chanstr, *p;
	ulong chan;
	Cmdtab *ct;
	extern VGAdev *vgadev[];
	extern VGAcur *vgacur[];

	ct = lookupcmd(cb, vgactlmsg, nelem(vgactlmsg));
	switch(ct->index){
	case CMhwgc:
		if(scr->gscreen == nil)
			error(Enoscreen);
		if(strcmp(cb->f[1], "off") == 0){
			qlock(&drawlock);
			cursoroff();
			if(scr->cur){
				if(scr->cur->disable)
					scr->cur->disable(scr);
				scr->cur = nil;
			}
			qunlock(&drawlock);
			return;
		}
		for(i = 0; vgacur[i]; i++){
			if(strcmp(cb->f[1], vgacur[i]->name))
				continue;
			qlock(&drawlock);
			cursoroff();
			if(scr->cur && scr->cur->disable)
				scr->cur->disable(scr);
			scr->cur = vgacur[i];
			if(scr->cur->enable)
				scr->cur->enable(scr);
			cursoron();
			qunlock(&drawlock);
			return;
		}
		break;

	case CMpcidev:
		if(cb->nf == 2){
			Pcidev *p;

			if((p = pcimatchtbdf(strtoul(cb->f[1], 0, 16))) != nil)
				scr->pci = p;
		} else
			error(Ebadarg);
		return;

	case CMtype:
		for(i = 0; vgadev[i]; i++){
			if(strcmp(cb->f[1], vgadev[i]->name))
				continue;
			if(scr->dev){
				qlock(&drawlock);
				scr->fill = nil;
				scr->scroll = nil;
				scr->blank = nil;
				hwblank = 0;
				hwaccel = 0;
				qunlock(&drawlock);
				if(scr->dev->disable)
					scr->dev->disable(scr);
			}
			scr->dev = vgadev[i];
			if(scr->dev->enable)
				scr->dev->enable(scr);
			return;
		}
		break;

	case CMtextmode:
		screeninit();
		bootscreenconf(nil);
		return;

	case CMsize:
		r.min = ZP;
		r.max.x = strtoul(cb->f[1], &p, 0);
		if(*p)
			p++;
		r.max.y = strtoul(p, &p, 0);
		if(*p)
			p++;
		z = strtoul(p, &p, 0);
		if(badrect(r))
			error(Ebadarg);
		chanstr = cb->f[2];
		if((chan = strtochan(chanstr)) == 0)
			error("bad channel");
		if(chantodepth(chan) != z)
			error("depth, channel do not match");
		qlock(&drawlock);
		deletescreenimage();
		setscreensize(scr, r.max.x, r.max.y, z, chan, scr->tilt);
		qunlock(&drawlock);
		return;

	case CMactualsize:
		if(scr->gscreen == nil)
			error(Enoscreen);
		r.min = ZP;
		r.max.x = strtoul(cb->f[1], &p, 0);
		if(*p)
			p++;
		r.max.y = strtoul(p, nil, 0);
		if(badrect(r))
			error(Ebadarg);
		if(r.max.x > scr->width || r.max.y > scr->height)
			error("physical screen bigger than virtual");
		qlock(&drawlock);
		deletescreenimage();
		setactualsize(scr, r);
		goto Resized;
	
	case CMpalettedepth:
		z = strtoul(cb->f[1], &p, 0);
		if(z != 8 && z != 6)
			error(Ebadarg);
		scr->palettedepth = z;
		return;

	case CMsoftscreen:
		if(strcmp(cb->f[1], "on") == 0)
			scr->softscreen = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			scr->softscreen = 0;
		else
			break;
		if(0){
	case CMtilt:
			for(tilt = 0; tilt < nelem(tiltstr); tilt++)
				if(strcmp(cb->f[1], tiltstr[tilt]) == 0)
					break;
		} else {
			tilt = scr->tilt;
		}
		if(scr->gscreen == nil)
			return;
		qlock(&drawlock);
		r = actualscreensize(scr);
		chan = scr->gscreen->chan;
		z = scr->gscreen->depth;
		deletescreenimage();
		setscreensize(scr, scr->width, scr->height, z, chan, tilt);
		setactualsize(scr, r);
		/* no break */
	Init:
		if(scr->gscreen == nil){
			qunlock(&drawlock);
			error(Enoscreen);
		}
		if(scr->dev && scr->dev->drawinit)
			scr->dev->drawinit(scr);
		hwblank = scr->blank != nil;
		hwaccel = scr->fill != nil || scr->scroll != nil;
	Resized:
		vgascreenwin(scr);
		resetscreenimage();
		qunlock(&drawlock);
		return;

	case CMdrawinit:
		qlock(&drawlock);
		goto Init;

	case CMlinear:
		if(cb->nf!=2 && cb->nf!=3)
			error(Ebadarg);
		size = strtoul(cb->f[1], 0, 0);
		if(cb->nf == 2)
			align = 0;
		else
			align = strtoul(cb->f[2], 0, 0);
		if(screenaperture(scr, size, align) < 0)
			error("not enough free address space");
		return;

	case CMhwaccel:
		if(strcmp(cb->f[1], "on") == 0)
			hwaccel = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			hwaccel = 0;
		else
			break;
		return;
	
	case CMhwblank:
		if(strcmp(cb->f[1], "on") == 0)
			hwblank = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			hwblank = 0;
		else
			break;
		return;
	}

	cmderror(cb, "bad VGA control message");
}

static long
vgawrite(Chan* c, void* a, long n, vlong off)
{
	ulong offset = off;
	Cmdbuf *cb;

	switch((ulong)c->qid.path){

	case Qdir:
		error(Eperm);

	case Qvgactl:
		if(offset || n >= READSTR)
			error(Ebadarg);
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		vgactl(&vgascreen[0], cb);
		poperror();
		free(cb);
		return n;

	default:
		error(Egreg);
		break;
	}
}

Dev vgadevtab = {
	'v',
	"vga",

	vgareset,
	devinit,
	vgashutdown,
	vgaattach,
	vgawalk,
	vgastat,
	vgaopen,
	devcreate,
	vgaclose,
	vgaread,
	devbread,
	vgawrite,
	devbwrite,
	devremove,
	devwstat,
};
