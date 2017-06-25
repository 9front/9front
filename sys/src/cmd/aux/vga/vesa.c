#include <u.h>
#include <libc.h>
#include <bio.h>
#include </386/include/ureg.h>
typedef struct Ureg Ureg;

#include "pci.h"
#include "vga.h"

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
	int	directcolor;	/* flags */
	ulong	paddr;
};

#define WORD(p) ((p)[0] | ((p)[1]<<8))
#define LONG(p) ((p)[0] | ((p)[1]<<8) | ((p)[2]<<16) | ((p)[3]<<24))
#define PWORD(p, v) (p)[0] = (v); (p)[1] = (v)>>8
#define PLONG(p, v) (p)[0] = (v); (p)[1] = (v)>>8; (p)[2] = (v)>>16; (p)[3] = (v)>>24

static Vbe *vbe;

static int dspcon;	/* connected displays bitmask */
static int dspact;	/* active displays bitmask */
static int (*setscale)(Vbe*, char*);

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
Edid* vbeddcedid(Vbe *vbe);
uchar* vbesetup(Vbe*, Ureg*, int);
int vbecall(Vbe*, Ureg*);
int setdisplay(Vbe *vbe, int display);
int getdisplay(Vbe*);
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

static char*
cracksize(char *size, char **scale, int *display)
{
	static char buf[256];
	char *f[4];
	int i, n;

	*scale = nil;
	*display = 0;
	snprint(buf, sizeof(buf), "%s", size);
	n = getfields(buf, f, nelem(f), 0, ",");
	for(i=1; i<n; i++){
		if(f[i][0] == '#')
			*display = atoi(&f[i][1]);
		else if(strncmp(f[i], "scale", 5) == 0)
			*scale = f[i];
		else
			error("bad size option: %s for %s\n", f[i], f[0]);
	}
	return f[0];
}

static char*
rgbmask2chan(char *buf, int depth, u32int rm, u32int gm, u32int bm)
{
	u32int m[4], dm;	/* r,g,b,x */
	char tmp[32];
	int c, n;

	dm = 1<<depth-1;
	dm |= dm-1;

	m[0] = rm & dm;
	m[1] = gm & dm;
	m[2] = bm & dm;
	m[3] = (~(m[0] | m[1] | m[2])) & dm;

	buf[0] = 0;
Next:
	for(c=0; c<4; c++){
		for(n = 0; m[c] & (1<<n); n++)
			;
		if(n){
			m[0] >>= n, m[1] >>= n, m[2] >>= n, m[3] >>= n;
			snprint(tmp, sizeof tmp, "%c%d%s", "rgbx"[c], n, buf);
			strcpy(buf, tmp);
			goto Next;
		}
	}
	return buf;
}

Mode*
dbvesamode(Vga *vga, char *size)
{
	int i, width, display;
	int oldmode, olddisplay;
	uchar *p, *ep;
	Vmode vm;
	Mode *m;
	Modelist *l;
	char *scale;

	if(vbe == nil)
		return nil;

	size = cracksize(size, &scale, &display);

	oldmode = olddisplay = 0;
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
	for(i=0; i<nelem(vga->edid); i++){
		if(vga->edid[i] == nil)
			continue;
		for(l = vga->edid[i]->modelist; l; l = l->next){
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

	/* account for framebuffer stride */
	width = vm.bpl * 8 / m->z;
	if(width > m->x)
		m->attr = mkattr(m->attr, "virtx", "%d", width);

	if(scale != nil)
		m->attr = mkattr(m->attr, "scale", "%s", scale);
	if(display != 0)
		m->attr = mkattr(m->attr, "display", "%d", display);

	m->attr = mkattr(m->attr, "id", "0x%x", vm.id);

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
	char *ds, *scale;

	if(vbe == nil)
		error("no vesa bios\n");
	mode = strtol(dbattr(vga->mode->attr, "id"), nil, 0);
	scale = dbattr(vga->mode->attr, "scale");
	ds = dbattr(vga->mode->attr, "display");
	display = ds == nil ? 0 : atoi(ds);
	olddisplay = oldmode = 0;

	/* need to reset scaling before switching displays */
	if(setscale != nil)
		(*setscale)(vbe, "scalefull");
	if(display != 0){
		olddisplay = getdisplay(vbe);
		oldmode = vbegetmode(vbe);
		if(setdisplay(vbe, display) < 0){
			fprint(2, "setdisplay: %r\n");
			ctlr->flag |= Ferror;
			return;
		}
	}
	if(vbesetmode(vbe, mode) < 0){
		fprint(2, "vbesetmode: %r\n");
		ctlr->flag |= Ferror;
		if(display != 0){
			setdisplay(vbe, olddisplay);
			vbesetmode(vbe, oldmode);
		}
		return;
	}
	if(setscale != nil){
		if((*setscale)(vbe, scale) < 0){
			fprint(2, "setscale: %r\n");
		}
	}
}

static void
dump(Vga *, Ctlr*)
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
}

static int
intelscale(Vbe *vbe, char *scale)
{
	Ureg u;
	int cx;

	if(scale == nil)
		cx = 0;
	else if(strcmp(scale, "scalefull") == 0)
		cx = 4;
	else {
		werrstr("intelscale: not supported: %s", scale);
		return -1;
	}
	vbesetup(vbe, &u, 0x5F61);
	u.bx = 0;
	u.cx = cx; /* horizontal */
	u.dx = cx; /* vertical */
	return vbecall(vbe, &u);
}

static int
nvidiascale(Vbe *vbe, char *scale)
{
	Ureg u;
	int cx;

	if(scale == nil)
		cx = 1;
	else if(strcmp(scale, "scaleaspect") == 0)
		cx = 3;
	else if(strcmp(scale, "scalefull") == 0)
		cx = 0;
	else {
		werrstr("nvidiascale: not supported: %s", scale);
		return -1;
	}
	vbesetup(vbe, &u, 0x4F14);
	u.bx = 0x102;
	u.cx = cx;
	return vbecall(vbe, &u);
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
	int ax;

	ax = u->ax >> 8;
	u->trap = 0x10;

	vbeflush(vbe);

	if(pwrite(vbe->rmfd, u, sizeof *u, 0) != sizeof *u)
		error("write /dev/realmode: %r\n");
	if(pread(vbe->rmfd, u, sizeof *u, 0) != sizeof *u)
		error("read /dev/realmode: %r\n");

	getmem(vbe, RealModeBuf, 0);

	if((u->ax&0xFFFF) != ax){
		werrstr("VBE error %#.4lux", u->ax&0xFFFF);
		return -1;
	}
	return 0;
}

int
vbecheck(Vbe *vbe)
{
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
	return 0;
}

int
vbesnarf(Vbe *vbe, Vga *vga)
{
	char *oem;
	uchar *p;
	Ureg u;

	p = vbesetup(vbe, &u, 0x4F00);
	strcpy((char*)p, "VBE2");
	if(vbecall(vbe, &u) < 0)
		return -1;
	if(memcmp(p, "VESA", 4) != 0 || p[5] < 2)
		return -1;
	vga->apz = WORD(p+18)*0x10000UL;

	oem = unfarptr(vbe, p+6);
	if(strncmp(oem, "Intel", 5) == 0){
		setscale = intelscale;

		/* detect connected display devices */
		vbesetup(vbe, &u, 0x5F64);
		u.bx = 0x200;
		if(vbecall(vbe, &u) < 0)
			u.cx = 0;
		dspcon = u.cx >> 8; /* CH = connected, CL = available? */

		/* detect active display devices */
		vbesetup(vbe, &u, 0x5F64);
		u.bx = 0x100;
		if(vbecall(vbe, &u) < 0)
			u.cx = 0;
		dspact = u.cx;

	}
/* breaks modeset on 10de/0392 "G73 [GeForce 7600 GS]" -- cinap
	else if(memcmp(oem, "NVIDIA", 6) == 0)
		setscale = nvidiascale;
*/

	vga->edid[0] = vbeddcedid(vbe);

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

	if(dspcon != 0){
		printitem("vesa", "dsp con");
		for(i = 0; i < 8; i++)
			if(dspcon & (1<<i))
				Bprint(&stdout, "%d ", i+1);
		Bprint(&stdout, "\n");
	}
	if(dspact != 0){
		printitem("vesa", "dsp act");
		for(i = 0; i < 8; i++)
			if(dspact & (1<<i))
				Bprint(&stdout, "%d ", i+1);
		Bprint(&stdout, "\n");
	}
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
	m->directcolor = p[39];
	m->paddr = LONG(p+40);

	snprint(m->size, sizeof m->size, "%dx%dx%d", m->dx, m->dy, m->depth);
	if(m->depth <= 8)
		snprint(m->chan, sizeof m->chan, "%c%d", 
			(m->attr & AttrColor) ? 'm' : 'k', m->depth);
	else
		rgbmask2chan(m->chan, m->depth,
			(1UL<<p[31])-1 << p[32],
			(1UL<<p[33])-1 << p[34],
			(1UL<<p[35])-1 << p[36]);
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

Edid*
vbeddcedid(Vbe *vbe)
{
	uchar *p;
	Ureg u;

	p = vbesetup(vbe, &u, 0x4F15);
	u.bx = 0x0001;
	if(vbecall(vbe, &u) < 0)
		return nil;
	return parseedid128(p);
}

int
getdisplay(Vbe*)
{
	int i;

	for(i = 0; i < 8; i++)
		if(dspact & 1<<i)
			return i+1;

	/* fallback to a connected one */
	for(i = 0; i < 8; i++)
		if(dspcon & 1<<i)
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
	if(dspcon & cx){
		/* switch to common mode before trying */
		vbesetmode(vbe, 3);

		vbesetup(vbe, &u, 0x5F64);
		u.bx = 0;
		u.cx = cx;
		return vbecall(vbe, &u);
	} else {
		werrstr("display #%d not connected", display);
		return -1;
	}
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

