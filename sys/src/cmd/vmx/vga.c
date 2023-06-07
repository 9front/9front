#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <ctype.h>
#include "dat.h"
#include "fns.h"

static uchar *fb, *tfb;
uintptr fbsz;
uintptr fbaddr;

VgaMode *curmode, *nextmode, *modes, **modeslast = &modes;
int curhbytes, nexthbytes;
int vesamode, maxw, maxh;
int novga;

VgaMode textmode = {
	.w 640, .h 400, .no 3
};

static Image *img, *bg;
static Mousectl *mc;
static Rectangle picr;
Channel *kbdch, *mousech;
u8int mousegrab;
extern u8int mouseactive;
static uchar *sfb;

typedef struct VGA VGA;
struct VGA {
	u8int miscout;
	u8int cidx; /* crtc */
	u8int aidx; /* attribute (bit 7: access flipflop) */
	u8int gidx; /* graphics */
	u8int sidx; /* sequencer */
	u16int rdidx, wdidx; /* bit 0-1: color */
	u8int attr[32];
	u8int seq[5];
	u8int graph[9];
	u32int pal[256];
	Image *col[256];
	Image *acol[16];
	u8int crtc[0x18];
	QLock;
} vga = {
	.miscout 1,
	.attr { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	.crtc { [10] 13, [11] 14 },
	.pal {
		0x000000ff, 0x0000a8ff, 0x00a800ff, 0x00a8a8ff, 0xa80000ff, 0xa800a8ff, 0xa85400ff, 0xa8a8a8ff,
		0x545454ff, 0x5454fcff, 0x54fc54ff, 0x54fcfcff, 0xfc5454ff, 0xfc54fcff, 0xfcfc54ff, 0xfcfcfcff,
		0x000000ff, 0x141414ff, 0x202020ff, 0x2c2c2cff, 0x383838ff, 0x444444ff, 0x505050ff, 0x606060ff,
		0x707070ff, 0x808080ff, 0x909090ff, 0xa0a0a0ff, 0xb4b4b4ff, 0xc8c8c8ff, 0xe0e0e0ff, 0xfcfcfcff,
		0x0000fcff, 0x4000fcff, 0x7c00fcff, 0xbc00fcff, 0xfc00fcff, 0xfc00bcff, 0xfc007cff, 0xfc0040ff,
		0xfc0000ff, 0xfc4000ff, 0xfc7c00ff, 0xfcbc00ff, 0xfcfc00ff, 0xbcfc00ff, 0x7cfc00ff, 0x40fc00ff,
		0x00fc00ff, 0x00fc40ff, 0x00fc7cff, 0x00fcbcff, 0x00fcfcff, 0x00bcfcff, 0x007cfcff, 0x0040fcff,
		0x7c7cfcff, 0x9c7cfcff, 0xbc7cfcff, 0xdc7cfcff, 0xfc7cfcff, 0xfc7cdcff, 0xfc7cbcff, 0xfc7c9cff,
		0xfc7c7cff, 0xfc9c7cff, 0xfcbc7cff, 0xfcdc7cff, 0xfcfc7cff, 0xdcfc7cff, 0xbcfc7cff, 0x9cfc7cff,
		0x7cfc7cff, 0x7cfc9cff, 0x7cfcbcff, 0x7cfcdcff, 0x7cfcfcff, 0x7cdcfcff, 0x7cbcfcff, 0x7c9cfcff,
		0xb4b4fcff, 0xc4b4fcff, 0xd8b4fcff, 0xe8b4fcff, 0xfcb4fcff, 0xfcb4e8ff, 0xfcb4d8ff, 0xfcb4c4ff,
		0xfcb4b4ff, 0xfcc4b4ff, 0xfcd8b4ff, 0xfce8b4ff, 0xfcfcb4ff, 0xe8fcb4ff, 0xd8fcb4ff, 0xc4fcb4ff,
		0xb4fcb4ff, 0xb4fcc4ff, 0xb4fcd8ff, 0xb4fce8ff, 0xb4fcfcff, 0xb4e8fcff, 0xb4d8fcff, 0xb4c4fcff,
		0x000070ff, 0x1c0070ff, 0x380070ff, 0x540070ff, 0x700070ff, 0x700054ff, 0x700038ff, 0x70001cff,
		0x700000ff, 0x701c00ff, 0x703800ff, 0x705400ff, 0x707000ff, 0x547000ff, 0x387000ff, 0x1c7000ff,
		0x007000ff, 0x00701cff, 0x007038ff, 0x007054ff, 0x007070ff, 0x005470ff, 0x003870ff, 0x001c70ff,
		0x383870ff, 0x443870ff, 0x543870ff, 0x603870ff, 0x703870ff, 0x703860ff, 0x703854ff, 0x703844ff,
		0x703838ff, 0x704438ff, 0x705438ff, 0x706038ff, 0x707038ff, 0x607038ff, 0x547038ff, 0x447038ff,
		0x387038ff, 0x387044ff, 0x387054ff, 0x387060ff, 0x387070ff, 0x386070ff, 0x385470ff, 0x384470ff,
		0x505070ff, 0x585070ff, 0x605070ff, 0x685070ff, 0x705070ff, 0x705068ff, 0x705060ff, 0x705058ff,
		0x705050ff, 0x705850ff, 0x706050ff, 0x706850ff, 0x707050ff, 0x687050ff, 0x607050ff, 0x587050ff,
		0x507050ff, 0x507058ff, 0x507060ff, 0x507068ff, 0x507070ff, 0x506870ff, 0x506070ff, 0x505870ff,
		0x000040ff, 0x100040ff, 0x200040ff, 0x300040ff, 0x400040ff, 0x400030ff, 0x400020ff, 0x400010ff,
		0x400000ff, 0x401000ff, 0x402000ff, 0x403000ff, 0x404000ff, 0x304000ff, 0x204000ff, 0x104000ff,
		0x004000ff, 0x004010ff, 0x004020ff, 0x004030ff, 0x004040ff, 0x003040ff, 0x002040ff, 0x001040ff,
		0x202040ff, 0x282040ff, 0x302040ff, 0x382040ff, 0x402040ff, 0x402038ff, 0x402030ff, 0x402028ff,
		0x402020ff, 0x402820ff, 0x403020ff, 0x403820ff, 0x404020ff, 0x384020ff, 0x304020ff, 0x284020ff,
		0x204020ff, 0x204028ff, 0x204030ff, 0x204038ff, 0x204040ff, 0x203840ff, 0x203040ff, 0x202840ff,
		0x2c2c40ff, 0x302c40ff, 0x342c40ff, 0x3c2c40ff, 0x402c40ff, 0x402c3cff, 0x402c34ff, 0x402c30ff,
		0x402c2cff, 0x40302cff, 0x40342cff, 0x403c2cff, 0x40402cff, 0x3c402cff, 0x34402cff, 0x30402cff,
		0x2c402cff, 0x2c4030ff, 0x2c4034ff, 0x2c403cff, 0x2c4040ff, 0x2c3c40ff, 0x2c3440ff, 0x2c3040ff,
		0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff,
	},
};

static void
newpal(int l, int n)
{
	int x;

	assert(l >= 0 && n + l <= 256);
	for(; n-- > 0; l++){
		freeimage(vga.col[l]);
		vga.col[l] = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, vga.pal[l]);
		if(vga.col[l] == nil) sysfatal("allocimage: %r");
	}
	for(l = 0; l < 16; l++){
		x = vga.attr[0x14] << 4 & 0xc0 | vga.attr[l] & 0x3f;
		if((vga.attr[0x10] & 0x80) != 0)
			x = x & 0xcf | vga.attr[0x14] << 4 & 0x30;
		vga.acol[l] = vga.col[x];
	}
}

u32int
vgagetpal(u8int n)
{
	return vga.pal[n];
}

void
vgasetpal(u8int n, u32int v)
{
	qlock(&vga);
	vga.pal[n] = v;
	newpal(n, 1);
	qunlock(&vga);
}

static void
screeninit(int resize)
{
	Point p;
	int ch;

	if(resize){
		freeimage(bg);
		bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
		newpal(0, 256);
	}
	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = (Rectangle){subpt(p, Pt(curmode->w/2, curmode->h/2)), addpt(p, Pt((curmode->w+1)/2, (curmode->h+1)/2))};
	switch(curmode->chan){
	case 0: ch = screen->chan; break;
	case CHAN1(CMap, 4): case CMAP8:
		if(vesamode){
			ch = XRGB32;
			break;
		}
		/* wet floor */
	default: ch = curmode->chan; break;
	}
	freeimage(img);
	img = allocimage(display, Rect(0, 0, curmode->w, curmode->h), ch, 0, 0);
	draw(screen, screen->r, bg, nil, ZP);
}

u32int
vgaio(int isin, u16int port, u32int val, int sz, void *)
{
	u32int m;

	if(novga)
		return 0;
	if(port != 0x3df && sz == 2 && !isin){
		vgaio(0, port, (u8int)val, 1, nil);
		return vgaio(0, port+1, (u8int)(val >> 8), 1, nil);
	}
	if(sz != 1) vmdebug("vga: non-byte access to port %#ux, sz=%d", port, sz);
	val = (u8int) val;
	switch(isin << 16 | port){
	case 0x3c0:
		if((vga.aidx & 0x80) != 0){
			vmdebug("vga: attribute write %#.2x = %#.2x", vga.aidx & 0x1f, val);
			vga.attr[vga.aidx & 0x1f] = val;
			qlock(&vga); newpal(0, 0); qunlock(&vga);
		}else
			vga.aidx = val & 0x3f;
		vga.aidx ^= 0x80;
		return 0;
	case 0x3c2: vga.miscout = val; return 0;
	case 0x3c4: vga.sidx = val; return 0;
	case 0x3c5:
		switch(vga.sidx){
		case 0: vga.seq[vga.sidx] = val & 3; return 0;
		case 4: vga.seq[vga.sidx] = val & 0xe; return 0;
		default: vmdebug("vga: write to unknown sequencer register %#ux (val=%#ux)", vga.sidx, val); return 0;
		}
	case 0x3c6: return 0;
	case 0x3c7: vga.rdidx = val << 2; return 0;
	case 0x3c8: vga.wdidx = val << 2; return 0;
	case 0x3c9:
		vga.pal[vga.wdidx >> 2] = vga.pal[vga.wdidx >> 2] & ~(0xff << (~vga.wdidx << 3 & 24)) | val << 2 + (~vga.wdidx << 3 & 24);
		qlock(&vga); newpal(vga.wdidx >> 2, 1); qunlock(&vga);
		vga.wdidx = vga.wdidx + 1 + (vga.wdidx >> 1 & 1) & 0x3ff;
		return 0;
	case 0x3ce: vga.gidx = val; return 0;
	case 0x3cf:
		switch(vga.gidx){
		case 4: vga.graph[vga.gidx] = val & 3; break;
		case 8: vga.graph[vga.gidx] = val; break;
		default:
			vmdebug("vga: write to unknown graphics register %#ux (val=%#ux)", vga.gidx, val);
		}
		return 0;
	case 0x3d4: vga.cidx = val; return 0;
	case 0x3d5:
		switch(vga.cidx){
		case 10: case 11: case 12: case 13: case 14: case 15:
			vga.crtc[vga.cidx] = val;
			return 0;
		default:
			vmdebug("vga: write to unknown CRTC register %#ux (val=%#ux)", vga.cidx, val);
		}
		return 0;
	case 0x103c0: return vga.aidx & 0x3f;
	case 0x103c1: return vga.attr[vga.aidx & 0x1f];
	case 0x103c4: return vga.sidx;
	case 0x103c5:	
		switch(vga.sidx){
		case 0:
		case 4:
			return vga.seq[vga.sidx];
		default: vmdebug("vga: read from unknown sequencer register %#ux (val=%#ux)", vga.sidx, val); return 0;
		}
	case 0x103c6: return 0xff;
	case 0x103c7: return vga.rdidx >> 2;
	case 0x103c8: return vga.wdidx >> 2;
	case 0x103c9:
		m = vga.pal[vga.rdidx >> 2] >> (~vga.rdidx << 3 & 24) + 2;
		vga.rdidx = vga.rdidx + 1 + (vga.rdidx >> 1 & 1) & 0x3ff;
		return m;
	case 0x103cc: return vga.miscout;
	case 0x103ce: return vga.gidx;
	case 0x103cf:
		switch(vga.gidx){
		case 4:
		case 8:
			return vga.graph[vga.gidx];
		default:
			vmdebug("vga: read from unknown graphics register %#ux", vga.gidx);
			return 0;
		}
	case 0x103d4: return vga.cidx;
	case 0x103d5:
		switch(vga.cidx){
		case 10: case 11: case 12: case 13: case 14: case 15:
			return vga.crtc[vga.cidx];
		default:
			vmdebug("vga: read from unknown CRTC register %#ux", vga.cidx);
			return 0;
		}
	case 0x103ca:
	case 0x103da:
		vga.aidx &= 0x7f;
		return 0;
	}
	return iowhine(isin, port, val, sz, "vga");
}

typedef struct Key Key;
struct Key {
	Rune r;
	int code;
	Key *next;
};
Key *kbdmap[128];

static void
defkey(Rune r, int code)
{
	Key *k, **kp;

	for(kp = &kbdmap[r % nelem(kbdmap)]; *kp != nil; kp = &(*kp)->next)
		if((*kp)->r == r)
			return;
	k = emalloc(sizeof(Key));
	k->r = r;
	k->code = code;
	*kp = k;
}

void
kbdlayout(char *fn)
{
	Biobuf *bp;
	char *s, *p, *f[10];
	int nf, x, y;
	Rune z;
	
	defkey(Kshift, 0x2a);
	defkey(Kctl, 0x1d);
	defkey(Kalt, 0x38);
	defkey(Kctl, 0x11d);
	defkey(Kprint, 0x137);
	defkey(Kaltgr, 0x138);
	defkey(Kbreak, 0x146);
	defkey(Khome, 0x147);
	defkey(Kup, 0x148);
	defkey(Kpgup, 0x149);
	defkey(Kleft, 0x14b);
	defkey(Kright, 0x14d);
	defkey(Kend, 0x14f);
	defkey(Kdown, 0x150);
	defkey(Kpgdown, 0x151);
	defkey(Kins, 0x152);
	defkey(Kdel, 0x153);
	defkey(Kmod4, 0x15b);
	defkey(Kmod4, 0x15c);
	defkey(Kup, 0x179);

	bp = Bopen(fn, OREAD);
	if(bp == nil){
		vmerror("kbdlayout: %r");
		return;
	}
	for(;; free(s)){
		static char *tab[] = {
			"none", "shift", "esc", "altgr",
			"ctl", "ctlesc", "shiftesc", "shiftaltgr",
			"mod4", "altgrmod4",
		};
		s = Brdstr(bp, '\n', 1);
		if(s == nil) break;
		nf = getfields(s, f, nelem(f), 1, " \t");
		if(nf < 3) continue;
		for(x = 0; x < nelem(tab); x++)
			if(strcmp(f[0], tab[x]) == 0)
				break;
		if(x >= nelem(tab)){
			x = strtol(f[0], &p, 0);
			if(*p != 0)
				continue;
		}
		y = strtol(f[1], &p, 0);
		if(*p != 0) continue;
		if(*f[2] == '\'' || *f[2] == '^'){
			chartorune(&z, f[2]+1);
			if(*f[2] == '^') z -= '@';
		}else{
			z = strtol(f[2], &p, 0);
			if(*p != 0) continue;
		}
		
		if(x != 0 || z == 0) continue;
		defkey(z, y);
	}
	Bterm(bp);
}

static vlong kbwatchdog; /* used to release mouse grabbing if keyproc is stuck */

void
keyproc(void *)
{
	int fd, n;
	static char buf[256];
	static uvlong kdown[8], nkdown[8];
	uvlong set, rls;
	int i, j;
	char *s;
	Rune r;
	Key *k;

	threadsetname("keyproc");
	fd = open("/dev/kbd", OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	for(;;){
		if(buf[0] != 0){
			n = strlen(buf)+1;
			memmove(buf, buf+n, sizeof(buf)-n);
		}
		if(buf[0] == 0){
			kbwatchdog = 0;
			n = read(fd, buf, sizeof(buf)-1);
			if(n <= 0)
				sysfatal("read /dev/kbd: %r");
			kbwatchdog = nanosec();
			buf[n-1] = 0;
			buf[n] = 0;
		}
		if(buf[0] != 'k' && buf[0] != 'K')
			continue;
		s = buf + 1;
		memset(nkdown, 0, sizeof(nkdown));
		while(*s != 0){
			s += chartorune(&r, s);
			for(k = kbdmap[r % nelem(kbdmap)]; k != nil; k = k->next)
				if(k->r == r){
					nkdown[k->code >> 6] |= 1ULL<<(k->code&63);
					break;
				}
			if(k == nil) vmdebug("unknown key %d", r);
		}
		if(mousegrab && (nkdown[0]>>29 & 1) != 0 && (nkdown[0]>>56 & 1) != 0){
			mousegrab = 0;
			setcursor(mc, nil);
		}
		for(i = 0; i < 8; i++){
			if(nkdown[i] == kdown[i]) continue;
			set = nkdown[i] & ~kdown[i];
			rls = ~nkdown[i] & kdown[i];
			for(j = 0; j < 64; j++, set>>=1, rls >>= 1)
				if(((set|rls) & 1) != 0){
					if(i >= 4)
						sendul(kbdch, 0xe0);
					sendul(kbdch, j | i<<6&0xff | ((rls&1) != 0 ? 0x80 : 0));
					sendnotif(i8042kick, nil);
				}
			kdown[i] = nkdown[i];
		}
	}
}

void
mousethread(void *)
{
	Mouse m;
	static Mouse mm, om;
	int gotm;
	Point mid;
	Rectangle grabout;
	int clicked;
	static Cursor blank;
	
	gotm = 0;
	clicked = 0;
	for(;;){
		Alt a[] = {
			{mc->c, &m, CHANRCV},
			{mousech, &mm, gotm ? CHANSND : CHANNOP},
			{nil, nil, CHANEND},
		};
		
		switch(alt(a)){
		case 0:
			mid = divpt(addpt(picr.max, picr.min), 2);
			grabout = insetrect(Rpt(mid, mid), -50);
			if(!ptinrect(m.xy, picr)){
				clicked = 0;
				break;
			}
			if(!mousegrab){
				if(clicked && (m.buttons & 1) == 0 && mouseactive){
					mousegrab = 1;
					setcursor(mc, &blank);
				}
				clicked = m.buttons & 1;
				break;
			}
			if(kbwatchdog != 0 && nanosec() - kbwatchdog > 1000ULL*1000*1000)
				mousegrab = 0;
			gotm = 1;
			if(!ptinrect(m.xy, grabout)){
				moveto(mc, mid);
				m.xy = mid;
				om.xy = mid;
			}
			mm.xy = addpt(mm.xy, subpt(m.xy, om.xy));
			om = m;
			mm.buttons = m.buttons;
			break;
		case 1:
			sendnotif(i8042kick, nil);
			mm.xy = Pt(0,0);
			gotm = 0;
			break;
		}
	}
}

static Rune cp437[256] = {
	0x0020, 0x263a, 0x263b, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25d8, 0x25cb, 0x25d9, 0x2642, 0x2640, 0x266a, 0x266b, 0x263c,
	0x25ba, 0x25c4, 0x2195, 0x203c, 0x00b6, 0x00a7, 0x25ac, 0x21a8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221f, 0x2194, 0x25b2, 0x25bc, 
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f, 
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f, 
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f, 
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f, 
	0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f, 
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x2302, 
	0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5, 
	0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192, 
	0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba, 0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb, 
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510, 
	0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, 0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567, 
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, 0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580, 
	0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4, 0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229, 
	0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248, 0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0, 
};

static void
drawtext(void)
{
	Rune buf[80];
	uchar *p, attr;
	int y, x, x1;
	Rectangle r;
	u16int cp;
	static uchar rbuf[80*25*2];
	u16int sa;
	
	sa = vga.crtc[12] << 8 | vga.crtc[13];
	if(sa + 80*25 >= 0x10000){
		memset(rbuf, 0, sizeof(rbuf));
		memmove(rbuf, tfb + sa * 2, 0x10000 - 80*25 - sa);
		p = rbuf;
	}else
		p = tfb + sa * 2;
	for(y = 0; y < 25; y++){
		for(x = 0; x < 80; x++)
			buf[x] = cp437[p[2*x]];
		for(x = 0; x < 80; x = x1){
			attr = p[2*x+1];
			for(x1 = x; x1 < 80 && p[2*x1+1] == attr; x1++)
				;
			r = Rect(x * 8, y * 16, x1 * 8, (y + 1) * 16);
			draw(img, r, vga.acol[attr >> 4], nil, ZP);
			runestringn(img, r.min, vga.acol[attr & 0xf], ZP, display->defaultfont, buf + x, x1 - x);
		}
		p += 160;
	}
	cp = (vga.crtc[14] << 8 | vga.crtc[15]);
	if(cp >= sa && cp < sa + 80*25 && (vga.crtc[10] & 0x20) == 0 && nanosec() / 500000000 % 2 == 0){
		buf[0] = cp437[tfb[cp*2]];
		attr = tfb[cp*2+1];
		r.min = Pt((cp - sa) % 80 * 8, (cp - sa) / 80 * 16);
		r.max = Pt(r.min.x + 8, r.min.y + (vga.crtc[11] & 0x1f) + 1);
		r.min.y += vga.crtc[10] & 0x1f;
		draw(img, r, vga.acol[attr & 0xf], nil, ZP);
	}
	draw(screen, picr, img, nil, ZP);
	flushimage(display, 1);	
}

static void
drawfb(int redraw)
{
	u32int *p, *q;
	Rectangle upd;
	int xb, y, hb;
	u32int v;
	uchar *cp;
	u32int *buf, *bp;

	p = (u32int *) fb;
	q = (u32int *) sfb;
	upd.min.y = upd.max.y = -1;
	xb = 0;
	y = 0;
	hb = curhbytes;
	while(p < (u32int*)(fb + curmode->sz)){
		if(*p != *q || redraw){
			if(upd.min.y < 0) upd.min.y = y;
			upd.max.y = y + 1 + (xb + 4 > hb);
			*q = *p;
		}
		p++;
		q++;
		xb += 4;
		if(xb >= hb){
			xb -= hb;
			y++;
		}
	}
	if(upd.min.y == upd.max.y) return;
	upd.min.x = 0;
	upd.max.x = curmode->w;
	if(vesamode && (curmode->chan >> 4 == CMap)){
		buf = emalloc(curmode->w * 4 * (upd.max.y - upd.min.y));
		bp = buf;
		for(y = upd.min.y; y < upd.max.y; y++){
			cp = sfb + y * hb;
			for(xb = 0; xb < curmode->w; xb++){
				if(curmode->chan == CMAP8)
					v = *cp++;
				else if((xb & 1) == 0)
					v = *cp & 0xf;
				else
					v = *cp++ >> 4;
				*bp++ = vga.pal[v];
			}
		}
		loadimage(img, upd, (void *) buf, curmode->w * 4 * (upd.max.y - upd.min.y));
		free(buf);
		draw(screen, rectaddpt(upd, picr.min), img, nil, upd.min);
	}else if(curmode->chan != screen->chan || !rectinrect(picr, screen->r)){
		if(curmode->hbytes != hb){
			for(y = upd.min.y; y < upd.max.y; y++)
				loadimage(img, Rect(0, y, curmode->w, y+1), sfb + y * hb, curmode->hbytes);
		}else
			loadimage(img, upd, sfb + upd.min.y * hb, (upd.max.y - upd.min.y) * hb);
		draw(screen, rectaddpt(upd, picr.min), img, nil, upd.min);
	}else{
		if(curmode->hbytes != hb){
			for(y = upd.min.y; y < upd.max.y; y++)
				loadimage(screen, Rect(picr.min.x, picr.min.y + y, picr.max.x, picr.min.y + y + 1), sfb + y * hb, curmode->hbytes);
		}else
			loadimage(screen, rectaddpt(upd, picr.min), sfb + upd.min.y * hb, (upd.max.y - upd.min.y) * hb);
	}
	flushimage(display, 1);
}

void
drawproc(void *)
{
	ulong ul;
	int event;
	VgaMode *m;

	threadsetname("draw");
	sfb = emalloc(fbsz);
	event = 4;
	for(;; sleep(20)){
		qlock(&vga);
		m = nextmode;
		if(m != curmode){
			event |= 1;
			curmode = m;
			curhbytes = m->hbytes;
		}
		if(nexthbytes != curhbytes){
			event |= 1;
			curhbytes = nexthbytes;
		}
		while(nbrecv(mc->resizec, &ul) > 0)
			event |= 2;
		if((event & 3) != 0){
			if((event & 2) != 0 && getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			screeninit((event & 2) != 0);
		}
		if(curmode == &textmode)
			drawtext();
		else
			drawfb(event != 0);
		event = 0;
		qunlock(&vga);
	}
}

static int
chancheck(u32int ch)
{
	u8int got;
	int i, t;
	
	got = 0;
	for(i = 0; i < 4; i++){
		t = ch >> 8 * i + 4 & 15;
		if((ch >> 8 * i & 15) == 0) continue;
		if(t >= NChan) return 0;
		if((got & 1<<t) != 0) return 0;
		got |= 1<<t;
	}
	if(!vesamode) return 1;
	switch(got){
	case 1<<CRed|1<<CGreen|1<<CBlue:
	case 1<<CRed|1<<CGreen|1<<CBlue|1<<CAlpha:
	case 1<<CRed|1<<CGreen|1<<CBlue|1<<CIgnore:
		return 1;
	case 1<<CMap:
		return chantodepth(ch) == 4 || chantodepth(ch) == 8;
	default:
		return 0;
	}
}

static char *
vgamodeparse(char *p, VgaMode **mp)
{
	char *r;
	VgaMode *m;
	char c;
	
	m = emalloc(sizeof(VgaMode));
	*mp = m;
	*modeslast = m;
	modeslast = &m->next;
	m->w = strtoul(p, &r, 10);
	if(*r != 'x')
	nope:
		sysfatal("invalid mode specifier");
	p = r + 1;
	m->h = strtoul(p, &r, 10);
	if(*r != 'x'){
		m->chan = XRGB32;
		goto out;
	}
	p = r + 1;
	while(isalnum(*r))
		r++;
	c = *r;
	*r = 0;
	m->chan = strtochan(p);
	*r = c;
	if(m->chan == 0 || !chancheck(m->chan))
		goto nope;
out:
	if(m->w > maxw) maxw = m->w;
	if(m->h > maxh) maxh = m->h;
	return r;
}

void
vgafbparse(char *fbstring)
{
	char *p, *q;
	VgaMode *m;

	if(strcmp(fbstring, "text") == 0){
		curmode = &textmode;
		maxw = 640;
		maxh = 400;
		return;
	}else if(strncmp(fbstring, "vesa:", 5) == 0){
		vesamode = 1;
		p = fbstring + 5;
	}else
		p = fbstring;
	do{
		q = vgamodeparse(p, &m);
		if(p == q || m->w <= 0 || m->h <= 0)
			no: sysfatal("invalid mode specifier");
		m->w &= ~7;
		m->hbytes = chantodepth(m->chan) * m->w + 7 >> 3;
		m->sz = m->hbytes * m->h;
		if(m->sz > fbsz) fbsz = m->sz;
		p = q;
	}while(*p++ == ',');
	if(*--p == '@'){
		p++;
		fbaddr = strtoul(p, &q, 0);
		if(p == q) goto no;
		p = q;
	}else
		fbaddr = 0xf0000000;
	if(*p != 0) goto no;
	if(modes == nil || vesamode == 0 && modes->next != nil)
		goto no;
	if(vesamode == 0){
		curmode = modes;
		curhbytes = curmode->hbytes;
		fbsz = -(-fbsz & -4096);
		novga = 1;
	}else{
		curmode = &textmode;
		if(fbsz < (1<<22))
			fbsz = 1<<22;
		else
			fbsz = roundpow2(fbsz);
	}
}


void
vgainit(int new)
{
	char buf[512];
	int i;
	PCIDev *d;
	extern void vesainit(void);

	memset(vga.col, 0, sizeof(vga.col));
	memset(vga.acol, 0, sizeof(vga.acol));
	if(curmode == nil) return;
	nextmode = curmode;
	nexthbytes = curhbytes;
	tfb = gptr(0xb8000, 0x8000);
	if(tfb == nil)
		sysfatal("got nil ptr for text framebuffer");
	for(i = 0; i < 0x8000; i += 2)
		PUT16(tfb, i, 0x0720);
	if(fbsz != 0){
		fb = gptr(fbaddr, fbsz);
		if(fb == nil)
			sysfatal("got nil ptr for framebuffer");
	}
	snprint(buf, sizeof(buf), "-dx %d -dy %d", maxw+50, maxh+50);
	if((new && newwindow(buf) < 0) || initdraw(nil, nil, "vmx") < 0)
		sysfatal("failed to initialize graphics: %r");
	screeninit(1);
	flushimage(display, 1);
	kbdlayout("/dev/kbmap");
	mc = initmouse(nil, screen);
	kbdch = chancreate(sizeof(ulong), 128);
	mousech = chancreate(sizeof(Mouse), 32);
	proccreate(mousethread, nil, 4096);
	proccreate(keyproc, nil, 4096);
	proccreate(drawproc, nil, 4096);
	if(vesamode){
		d = mkpcidev(allocbdf(), 0x06660666, 0x03000000, 0);
		mkpcibar(d, BARMEM32 | BARPREF, fbaddr, fbsz, nil, nil);
		vesainit();
	}
}
