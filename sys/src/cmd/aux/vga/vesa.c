#include <u.h>
#include <libc.h>
#include <bio.h>
#include </386/include/ureg.h>
typedef struct Ureg Ureg;

#include "pci.h"
#include "vga.h"
#include "edid.h"

typedef struct Vbe Vbe;
typedef struct Vmode Vmode;

enum
{
	MemSize = 1024*1024,
	PageSize = 4096,
	RealModeBuf = 0x9000,
};

struct Vbe
{
	int	rmfd;	/* /dev/realmode */
	int	memfd;	/* /dev/realmem */
	uchar	*mem;	/* copy of memory; 1MB */
	uchar	*isvalid;	/* 1byte per 4kB in mem */
	uchar	*modebuf;
	int	dspcon;	/* connected displays bitmask */
	int	dspact;	/* active displays bitmask */
	void (*scale)(Vga*, Ctlr*);
};

struct Vmode
{
	char	size[Namelen+1];
	char	chan[Namelen+1];
	int	id;
	int	attr;	/* flags */
	int	bpl;
	int	dx, dy;
	int	depth;
	char	*model;
	int	r, g, b, x;
	int	ro, go, bo, xo;
	int	directcolor;	/* flags */
	ulong	paddr;
};

#define WORD(p) ((p)[0] | ((p)[1]<<8))
#define LONG(p) ((p)[0] | ((p)[1]<<8) | ((p)[2]<<16) | ((p)[3]<<24))
#define PWORD(p, v) (p)[0] = (v); (p)[1] = (v)>>8
#define PLONG(p, v) (p)[0] = (v); (p)[1] = (v)>>8; (p)[2] = (v)>>16; (p)[3] = (v)>>24

static Vbe *vbe;
static Edid *edid;

Vbe *mkvbe(void);
int vbecheck(Vbe*);
uchar *vbemodes(Vbe*);
int vbemodeinfo(Vbe*, int, Vmode*);
int vbegetmode(Vbe*);
int vbesetmode(Vbe*, int);
void vbeprintinfo(Vbe*);
void vbeprintmodeinfo(Vbe*, int, char*);
int vbesnarf(Vbe*, Vga*);
void vesaddc(void);
int vbeddcedid(Vbe *vbe, Edid *e);
uchar* vbesetup(Vbe*, Ureg*, int);
int vbecall(Vbe*, Ureg*);
int setdisplay(Vbe *vbe, int display);
int getdisplay(Vbe *vbe);
void fixbios(Vbe*);

int
dbvesa(Vga* vga)
{
	if(vbe == nil){
		vbe = mkvbe();
		if(vbe == nil){
			fprint(2, "mkvbe: %r\n");
			return 0;
		}
	}
	if(vbecheck(vbe) < 0){
		fprint(2, "dbvesa: %r\n");
		return 0;
	}

	vga->link = alloc(sizeof(Ctlr));
	*vga->link = vesa;
	vga->vesa = vga->link;
	vga->ctlr = vga->link;

	vga->link->link = alloc(sizeof(Ctlr));
	*vga->link->link = softhwgc;
	vga->hwgc = vga->link->link;

	return 1;
}

Mode*
dbvesamode(char *size)
{
	int i, width, nargs, display;
	int oldmode, olddisplay;
	uchar *p, *ep;
	Attr *a;
	Vmode vm;
	Mode *m;
	Modelist *l;
	char *args[4], *scale;

	if(vbe == nil)
		return nil;

	scale = nil;
	display = 0;
	oldmode = olddisplay = 0;
	nargs = getfields(size, args, 4, 0, ",");
	if(nargs > 1){
		if(args[1][0] == '#'){
			display = atoi(&args[1][1]);
			if(nargs > 2)
				scale = args[2];
		}else if(args[1][0] == 's'){
			scale = args[1];
			if(nargs > 2)
				display = atoi(&args[2][1]);
		}
	}

	if(display != 0){
		olddisplay = getdisplay(vbe);
		oldmode = vbegetmode(vbe);
		if(setdisplay(vbe, display) < 0)
			return nil;
	}

	if(strncmp(size, "0x", 2) == 0){
		if(vbemodeinfo(vbe, strtol(size+2, nil, 16), &vm) == 0)
			goto havemode;
	}else{
		if(p = vbemodes(vbe)){
			for(ep=p+1024; (p[0]!=0xFF || p[1]!=0xFF) && p<ep; p+=2){
				if(vbemodeinfo(vbe, WORD(p), &vm) < 0)
					continue;
				if(strcmp(vm.size, size) == 0)
					goto havemode;
			}
		}
		if(1){
			fprint(2, "warning: scanning for unoffered vesa modes\n");
			for(i=0x100; i<0x200; i++){
				if(vbemodeinfo(vbe, i, &vm) < 0)
					continue;
				if(strcmp(vm.size, size) == 0)
					goto havemode;
			}
		}
	}

	if(display != 0 && setdisplay(vbe, olddisplay) == 0)
		vbesetmode(vbe, oldmode);

	werrstr("no such vesa mode");
	return nil;

havemode:
	if(display != 0 && setdisplay(vbe, olddisplay) == 0)
		vbesetmode(vbe, oldmode);

	m = alloc(sizeof(Mode));
	m->x = vm.dx;
	m->y = vm.dy;
	m->ht = m->x;
	m->shb = m->x;
	m->ehb = m->x;
	m->shs = m->x;
	m->ehs = m->x;
	m->vt = m->y;
	m->vrs = m->y;
	m->vre = m->y;
	m->frequency = m->ht * m->vt * 60;

	/* get default monitor timing */
	for(i=0; vesamodes[i]; i++){
		if(vesamodes[i]->x != vm.dx || vesamodes[i]->y != vm.dy)
			continue;
		*m = *vesamodes[i];
		break;
	}
	if(edid != nil){
		for(l = edid->modelist; l; l = l->next){
			if(l->x != vm.dx || l->y != vm.dy)
				continue;
			*m = *((Mode*)l);
			break;
		}
	}

	strcpy(m->type, "vesa");
	strcpy(m->size, vm.size);
	strcpy(m->chan, vm.chan);
	m->z = vm.depth;

	a = alloc(sizeof(Attr));
	a->attr = "id";
	a->val = alloc(32);
	sprint(a->val, "0x%x", vm.id);

	a->next = nil;
	m->attr = a;

	/* scaling mode */
	if(scale != nil){
		a = alloc(sizeof(Attr));
		a->attr = "scale";
		a->val = alloc(32);
		strncpy(a->val, scale, 32);

		a->next = m->attr;
		m->attr = a;
	}

	/* display id */
	if(display != 0){
		a = alloc(sizeof(Attr));
		a->attr = "display";
		a->val = alloc(2);
		a->val[0] = '0' + display;

		a->next = m->attr;
		m->attr = a;
	}

	/* account for framebuffer stride */
	width = vm.bpl * 8 / m->z;
	if(width > m->x){
		a = alloc(sizeof(Attr));
		a->attr = "virtx";
		a->val = alloc(32);
		sprint(a->val, "%d", width);

		a->next = m->attr;
		m->attr = a;
	}

	return m;
}

static void
snarf(Vga* vga, Ctlr* ctlr)
{
	if(vbe == nil)
		vbe = mkvbe();
	if(vbe != nil)
		vga->vesa = ctlr;
	vbesnarf(vbe, vga);
	vga->linear = 1;
	ctlr->flag |= Hlinear|Ulinear|Fsnarf;
}

static void
options(Vga *vga, Ctlr *ctlr)
{
	char *v;

	if(v = dbattr(vga->mode->attr, "virtx")){
		vga->virtx = atoi(v);
		vga->virty = vga->mode->y;
		vga->panning = 0;
	}
	ctlr->flag |= Foptions;
}

static void
load(Vga* vga, Ctlr* ctlr)
{
	int mode, display;
	int oldmode, olddisplay;
	char *ds;

	if(vbe == nil)
		error("no vesa bios\n");
	mode = atoi(dbattr(vga->mode->attr, "id"));
	ds = dbattr(vga->mode->attr, "display");
	display = ds == nil ? 0 : atoi(ds);
	olddisplay = oldmode = 0;

	/* need to reset scaling before switching displays */
	if(vbe->scale != nil)
		vbe->scale(nil, nil);

	if(display != 0){
		olddisplay = getdisplay(vbe);
		oldmode = vbegetmode(vbe);
	}

	if(setdisplay(vbe, display) < 0){
		ctlr->flag |= Ferror;
		fprint(2, "vbesetmode: %r\n");
	}else if(vbesetmode(vbe, mode) < 0){
		if(display != 0){
			setdisplay(vbe, olddisplay);
			vbesetmode(vbe, oldmode);
		}
		ctlr->flag |= Ferror;
		fprint(2, "vbesetmode: %r\n");
	}else if(vbe->scale != nil)
		vbe->scale(vga, ctlr);
}

static void
dump(Vga*, Ctlr*)
{
	int i;
	char did[0x200];
	uchar *p, *ep;

	if(vbe == nil){
		Bprint(&stdout, "no vesa bios\n");
		return;
	}

	memset(did, 0, sizeof did);
	vbeprintinfo(vbe);
	p = vbemodes(vbe);
	if(p != nil){
		for(ep=p+1024; (p[0]!=0xFF || p[1]!=0xFF) && p<ep; p+=2){
			vbeprintmodeinfo(vbe, WORD(p), "");
			if(WORD(p) < nelem(did))
				did[WORD(p)] = 1;
		}
	}
	for(i=0x100; i<0x1FF; i++)
		if(!did[i])
			vbeprintmodeinfo(vbe, i, " (unoffered)");
	if(edid != nil)
		printedid(edid);
}

static void
intelscale(Vga* vga, Ctlr* ctlr)
{
	Ureg u;
	int cx;
	char *scale;

	if(vbe == nil)
		error("no vesa bios\n");

	if(vga == nil)
		cx = 4;
	else{
		/* NOTE: intel doesn't support "aspect" scaling mode :( */
		scale = dbattr(vga->mode->attr, "scale");
		if(scale == nil)
			cx = 0;
		else if(strcmp(scale, "scalefull") == 0)
			cx = 4;
		else{
			ctlr->flag |= Ferror;
			fprint(2, "vbescale: unsupported mode %s\n", scale);
			return;
		}
	}

	vbesetup(vbe, &u, 0x5F61);
	u.bx = 0;
	u.cx = cx; /* horizontal */
	u.dx = cx; /* vertical */
	vbecall(vbe, &u);
}

static void
nvidiascale(Vga* vga, Ctlr* ctlr)
{
	Ureg u;
	int cx;
	char *scale;

	if(vbe == nil)
		error("no vesa bios\n");

	if(vga == nil)
		cx = 0;
	else{
		scale = dbattr(vga->mode->attr, "scale");
		if(scale == nil)
			cx = 1;
		else if(strcmp(scale, "scaleaspect") == 0)
			cx = 3;
		else if(strcmp(scale, "scalefull") == 0)
			cx = 0;
		else{
			ctlr->flag |= Ferror;
			fprint(2, "vbescale: unsupported mode %s\n", scale);
			return;
		}
	}

	vbesetup(vbe, &u, 0x4F14);
	u.bx = 0x102;
	u.cx = cx;
}

Ctlr vesa = {
	"vesa",
	snarf,
	options,
	nil,
	load,
	dump,
};

Ctlr softhwgc = {
	"soft",
};

/*
 * VESA bios extension
 */

static Flag capabilityflag[] = {
	0x01, "8-bit-dac",
	0x02, "not-vga",
	0x04, "ramdac-needs-blank",
	0x08, "stereoscopic",
	0x10, "stereo-evc",
	0
};

enum {
	AttrSupported	= 1<<0,
	AttrTTY		= 1<<2,
	AttrColor	= 1<<3,
	AttrGraphics	= 1<<4,
	AttrNotVGA	= 1<<5,
	AttrNotWinVGA	= 1<<6,
	AttrLinear	= 1<<7,
	AttrDoublescan	= 1<<8,
	AttrInterlace	= 1<<9,
	AttrTriplebuf	= 1<<10,
	AttrStereo	= 1<<11,
	AttrDualAddr	= 1<<12,
};

static Flag modeattributesflags[] = {
	AttrSupported,	"supported",
	AttrTTY,	"tty",
	AttrColor,	"color",
	AttrGraphics,	"graphics",
	AttrNotVGA,	"not-vga",
	AttrNotWinVGA,	"no-windowed-vga",
	AttrLinear,	"linear",
	AttrDoublescan,	"double-scan",
	AttrInterlace,	"interlace",
	AttrTriplebuf,	"triple-buffer",
	AttrStereo,	"stereoscopic",
	AttrDualAddr,	"dual-start-addr",
	0
};

static Flag winattributesflags[] = {
	1<<0, "relocatable",
	1<<1, "readable",
	1<<2, "writeable",
	0
};

static Flag directcolorflags[] = {
	1<<0, "programmable-color-ramp",
	1<<1, "x-usable",
	0
};

enum {
	ModText = 0,
	ModCGA,
	ModHercules,
	ModPlanar,
	ModPacked,
	ModNonChain4,
	ModDirect,
	ModYUV,
};

static char *modelstr[] = {
	[ModText]	"text",
	[ModCGA]	"cga",
	[ModHercules]	"hercules",
	[ModPlanar]	"planar",
	[ModPacked]	"packed",
	[ModNonChain4]	"non-chain4",
	[ModDirect]	"direct",
	[ModYUV]	"YUV",
};

Vbe*
mkvbe(void)
{
	Vbe *vbe;

	vbe = alloc(sizeof(Vbe));
	if((vbe->rmfd = open("/dev/realmode", ORDWR)) < 0)
		return nil;
	if((vbe->memfd = open("/dev/realmodemem", ORDWR)) < 0)
		return nil;
	vbe->mem = alloc(MemSize);
	vbe->isvalid = alloc(MemSize/PageSize);
	vbe->modebuf = alloc(PageSize);
	fixbios(vbe);
	return vbe;
}

static void
loadpage(Vbe *vbe, int p, int wr)
{
	if(p >= MemSize/PageSize)
		return;
	if(vbe->isvalid[p] == 0)
		if(pread(vbe->memfd, vbe->mem+p*PageSize, PageSize, p*PageSize) != PageSize)
			error("read /dev/realmodemem: %r\n");
	vbe->isvalid[p] = 1+wr;
}

static void
flushpage(Vbe *vbe, int p)
{
	if(p >= MemSize/PageSize || vbe->isvalid[p]!=2)
		return;
	if(pwrite(vbe->memfd, vbe->mem+p*PageSize, PageSize, p*PageSize) != PageSize)
		error("write /dev/realmodemem: %r\n");
	vbe->isvalid[p] = 1;
}

static void*
getmem(Vbe *vbe, int off, int wr)
{
	if(off == 0 || off >= MemSize)
		return nil;
	loadpage(vbe, off/PageSize, wr);
	if(off % PageSize)
		loadpage(vbe, (off/PageSize)+1, wr);
	return vbe->mem+off;
}

static void*
unfarptr(Vbe *vbe, uchar *p)
{
	int seg, off;

	seg = WORD(p+2);
	off = WORD(p);
	off += seg<<4;
	return getmem(vbe, off, 0);
}

uchar*
vbesetup(Vbe *vbe, Ureg *u, int ax)
{
	uchar *p;

	memset(u, 0, sizeof *u);
	u->ax = ax;
	u->es = (RealModeBuf>>4)&0xF000;
	u->di = RealModeBuf&0xFFFF;
	p = getmem(vbe, RealModeBuf, 1);
	memset(p, 0, PageSize);
	return p;
}

void
vbeflush(Vbe *vbe)
{
	int p;

	for(p=0; p<MemSize/PageSize; p++)
		flushpage(vbe, p);

	memset(vbe->isvalid, 0, MemSize/PageSize);
}

int
vbecall(Vbe *vbe, Ureg *u)
{
	u->trap = 0x10;

	vbeflush(vbe);

	if(pwrite(vbe->rmfd, u, sizeof *u, 0) != sizeof *u)
		error("write /dev/realmode: %r\n");
	if(pread(vbe->rmfd, u, sizeof *u, 0) != sizeof *u)
		error("read /dev/realmode: %r\n");

	getmem(vbe, RealModeBuf, 0);

	if((u->ax&0xFFFF) != 0x004F){
		werrstr("VBE error %#.4lux", u->ax&0xFFFF);
		return -1;
	}
	return 0;
}

int
vbecheck(Vbe *vbe)
{
	char *oem;
	uchar *p;
	Ureg u;

	p = vbesetup(vbe, &u, 0x4F00);
	strcpy((char*)p, "VBE2");
	if(vbecall(vbe, &u) < 0)
		return -1;
	if(memcmp(p, "VESA", 4)){
		werrstr("invalid vesa signature %.4H\n", p);
		return -1;
	}
	if(p[5] < 2){
		werrstr("invalid vesa version: %.4H\n", p+4);
		return -1;
	}
	oem = unfarptr(vbe, p+6);
	if(strncmp(oem, "Intel", 5) == 0){
		vbe->scale = intelscale;

		/* detect connected display devices */
		vbesetup(vbe, &u, 0x5F64);
		u.bx = 0x200;
		vbecall(vbe, &u);
		vbe->dspcon = u.cx >> 8; /* CH = connected, CL = available? */

		/* detect active display devices */
		vbesetup(vbe, &u, 0x5F64);
		u.bx = 0x100;
		vbecall(vbe, &u);
		vbe->dspact = u.cx;
	}else if(memcmp(oem, "NVIDIA", 6) == 0)
		vbe->scale = nvidiascale;
	return 0;
}

int
vbesnarf(Vbe *vbe, Vga *vga)
{
	uchar *p;
	Ureg u;

	p = vbesetup(vbe, &u, 0x4F00);
	strcpy((char*)p, "VBE2");
	if(vbecall(vbe, &u) < 0)
		return -1;
	if(memcmp(p, "VESA", 4) != 0 || p[5] < 2)
		return -1;
	vga->apz = WORD(p+18)*0x10000UL;
	if(edid == nil){
		edid = alloc(sizeof(Edid));
		if(vbeddcedid(vbe, edid) < 0){
			free(edid);
			edid = nil;
		}
	}
	return 0;
}

void
vbeprintinfo(Vbe *vbe)
{
	uchar *p;
	Ureg u;
	int i;

	p = vbesetup(vbe, &u, 0x4F00);
	strcpy((char*)p, "VBE2");
	if(vbecall(vbe, &u) < 0)
		return;

	printitem("vesa", "sig");
	Bprint(&stdout, "%.4s %d.%d\n", (char*)p, p[5], p[4]);
	if(p[5] < 2)
		return;

	printitem("vesa", "oem");
	Bprint(&stdout, "%s %d.%d\n", unfarptr(vbe, p+6), p[21], p[20]);
	printitem("vesa", "vendor");
	Bprint(&stdout, "%s\n", unfarptr(vbe, p+22));
	printitem("vesa", "product");
	Bprint(&stdout, "%s\n", unfarptr(vbe, p+26));
	printitem("vesa", "rev");
	Bprint(&stdout, "%s\n", unfarptr(vbe, p+30));

	printitem("vesa", "cap");
	printflags(capabilityflag, p[10]);

	printitem("vesa", "mem");
	Bprint(&stdout, "%lud\n", WORD(p+18)*0x10000UL);

	printitem("vesa", "dsp con");
	for(i = 0; i < 8; i++)
		if(vbe->dspcon & (1<<i))
			Bprint(&stdout, "%d ", i+1);
	Bprint(&stdout, "\n");

	printitem("vesa", "dsp act");
	for(i = 0; i < 8; i++)
		if(vbe->dspact & (1<<i))
			Bprint(&stdout, "%d ", i+1);
	Bprint(&stdout, "\n");
}

uchar*
vbemodes(Vbe *vbe)
{
	uchar *p;
	Ureg u;

	p = vbesetup(vbe, &u, 0x4F00);
	strcpy((char*)p, "VBE2");
	if(vbecall(vbe, &u) < 0)
		return nil;
	memmove(vbe->modebuf, unfarptr(vbe, p+14), 1024);
	return vbe->modebuf;
}

int
vbemodeinfo(Vbe *vbe, int id, Vmode *m)
{
	uchar *p;
	Ureg u;
	int mod;

	p = vbesetup(vbe, &u, 0x4F01);
	u.cx = id;
	if(vbecall(vbe, &u) < 0)
		return -1;

	m->id = id;
	m->attr = WORD(p);
	if(!(m->attr & AttrSupported))
		goto Unsupported;
	if(!(m->attr & AttrGraphics))
		goto Unsupported;
	if(!(m->attr & AttrLinear))
		goto Unsupported;
	m->bpl = WORD(p+16);
	m->dx = WORD(p+18);
	m->dy = WORD(p+20);
	m->depth = p[25];
	if((m->dx * m->dy * m->depth) <= 0)
		goto Unsupported;
	mod = p[27];
	switch(mod){
	default:
	Unsupported:
		werrstr("mode unsupported");
		return -1;
	case ModCGA:
	case ModHercules:
	case ModPacked:
	case ModDirect:
		m->model = modelstr[mod];
		break;
	}
	m->r = p[31];
	m->g = p[33];
	m->b = p[35];
	m->x = p[37];
	m->ro = p[32];
	m->go = p[34];
	m->bo = p[36];
	m->xo = p[38];
	m->directcolor = p[39];
	m->paddr = LONG(p+40);

	snprint(m->size, sizeof m->size, "%dx%dx%d", m->dx, m->dy, m->depth);
	if(m->depth <= 8)
		snprint(m->chan, sizeof m->chan, "%c%d", 
			(m->attr & AttrColor) ? 'm' : 'k', m->depth);
	else {
		int o;
		ulong d, c, x;

		m->xo = m->x = 0;
		d = 1<<m->depth-1;
		d |= d-1;
		c = ((1<<m->r)-1) << m->ro;
		c |= ((1<<m->g)-1) << m->go;
		c |= ((1<<m->b)-1) << m->bo;
		if(x = d ^ c){
			for(; (x & 1) == 0; x >>= 1)
				m->xo++;
			for(; (x & 1) == 1; x >>= 1)
				m->x++;
		}

		o = 0;
		m->chan[0] = 0;
		while(o < m->depth){
			char tmp[sizeof m->chan];

			if(m->r && m->ro == o){
				snprint(tmp, sizeof tmp, "r%d%s", m->r, m->chan);
				o += m->r;
			}else if(m->g && m->go == o){
				snprint(tmp, sizeof tmp, "g%d%s", m->g, m->chan);
				o += m->g;
			}else if(m->b && m->bo == o){
				snprint(tmp, sizeof tmp, "b%d%s", m->b, m->chan);
				o += m->b;
			}else if(m->x && m->xo == o){
				snprint(tmp, sizeof tmp, "x%d%s", m->x, m->chan);
				o += m->x;
			}else
				break;
			strncpy(m->chan, tmp, sizeof m->chan);
		}
	}
	return 0;
}

void
vbeprintmodeinfo(Vbe *vbe, int id, char *suffix)
{
	Vmode m;

	if(vbemodeinfo(vbe, id, &m) < 0)
		return;
	printitem("vesa", "mode");
	Bprint(&stdout, "0x%ux %s %s %s%s\n",
		m.id, m.size, m.chan, m.model, suffix);
}

int
vbegetmode(Vbe *vbe)
{
	Ureg u;

	vbesetup(vbe, &u, 0x4F03);
	if(vbecall(vbe, &u) < 0)
		return 0;
	return u.bx;
}

int
vbesetmode(Vbe *vbe, int id)
{
	Ureg u;

	vbesetup(vbe, &u, 0x4F02);
	u.bx = id;
	if(id != 3)
		u.bx |= 3<<14;	/* graphics: use linear, do not clear */
	return vbecall(vbe, &u);
}

void
vesatextmode(void)
{
	if(vbe == nil){
		vbe = mkvbe();
		if(vbe == nil)
			error("mkvbe: %r\n");
	}
	if(vbecheck(vbe) < 0)
		error("vbecheck: %r\n");
	if(vbesetmode(vbe, 3) < 0)
		error("vbesetmode: %r\n");
}

int parseedid128(Edid *e, void *v);

int
vbeddcedid(Vbe *vbe, Edid *e)
{
	uchar *p;
	Ureg u;

	p = vbesetup(vbe, &u, 0x4F15);
	u.bx = 0x0001;
	if(vbecall(vbe, &u) < 0)
		return -1;
	if(parseedid128(e, p) < 0){
		werrstr("parseedid128: %r");
		return -1;
	}
	return 0;
}

int
getdisplay(Vbe *vbe)
{
	int i;

	for(i = 0; i < 8; i++)
		if(vbe->dspact & 1<<i)
			return i+1;

	/* fallback to a connected one */
	for(i = 0; i < 8; i++)
		if(vbe->dspcon & 1<<i)
			return i+1;

	return 0;
}

int
setdisplay(Vbe *vbe, int display)
{
	Ureg u;
	int cx;

	if(display == 0)
		return 0;

	cx = 1<<(display-1);
	if(vbe->dspcon & cx){
		/* switch to common mode before trying */
		vbesetmode(vbe, 3);

		vbesetup(vbe, &u, 0x5F64);
		u.bx = 0;
		u.cx = cx;
		vbecall(vbe, &u);
		if(u.ax == 0x5f)
			return 0;
		werrstr("setdisplay: VBE error %#.4lux", u.ax);
	}else
		werrstr("setdisplay: %d not connected", display);
	return -1;
}

void
fixbios(Vbe *vbe)
{
	uchar *p;
	int i;

	/*
	 * Intel(r) Cantiga Graphics Chip Accelerated VGA BIOS 1.0 has
	 * a wrong entry in mode alias table at c000:7921 for mode 0x16b 
	 * (1440x900x32) wrongly replacing it with mode 0x162 (768x480x32).
	 */
	p = getmem(vbe, 0xc7921, 0);
	if(p != nil && p[0] == 0x01 && p[1] == 0xff && p[2] == 0xff){
		for(i=1; i<64; i++){
			p = getmem(vbe, 0xc7921 + 3*i, 0);
			if(p == nil || p[0] == 0xff)
				break;
			if(p[0] == 0x6b && p[1] == 0x6b && p[2] == 0x62){
				p = getmem(vbe, 0xc7921 + 3*i, 1);
				p[2] = 0x6b;	/* fix */
				break;
			}
		}
	}
}

