#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include "dat.h"
#include "fns.h"

static uchar *fb;
uintptr fbsz;
uintptr fbaddr;
int textmode;
static ulong screenchan;

static int picw, pich, hbytes;
static Image *img, *bg;
static Mousectl *mc;
static Rectangle picr;
Channel *kbdch, *mousech;
static u16int cursorpos;
u8int mousegrab;
static uchar *sfb;

static void
screeninit(void)
{
	Point p;

	p = divpt(addpt(screen->r.min, screen->r.max), 2);
	picr = (Rectangle){subpt(p, Pt(picw/2, pich/2)), addpt(p, Pt((picw+1)/2, (pich+1)/2))};
	bg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, 0xCCCCCCFF);
	img = allocimage(display, Rect(0, 0, picw, pich), screenchan == 0 ? screen->chan : screenchan, 0, 0);
	draw(screen, screen->r, bg, nil, ZP);
}

u32int
vgaio(int isin, u16int port, u32int val, int sz, void *)
{
	static u8int cgaidx;

	val = (u8int) val;
	switch(isin << 16 | port){
	case 0x3d4:
		cgaidx = val;
		return 0;
	case 0x103d4:
		return cgaidx;
	case 0x3d5:
		switch(cgaidx){
		case 14:
			cursorpos = cursorpos >> 8 | val << 8;
			break;
		case 15:
			cursorpos = cursorpos & 0xff00 | val;
			break;
		default:
			vmerror("write to unknown VGA register, 3d5/%#ux (val=%#ux)", cgaidx, val);
		}
		return 0;
	case 0x103d5:
		switch(cgaidx){
		case 14:
			return cursorpos >> 8;
		case 15:
			return (u8int)cursorpos;
		default:
			vmerror("read from unknown VGA register, 3d5/%#ux", cgaidx);
			return 0;
		}		
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
	defkey(Kup, 0x179);

	bp = Bopen(fn, OREAD);
	if(bp == nil){
		vmerror("kbdlayout: %r");
		return;
	}
	for(;; free(s)){
		s = Brdstr(bp, '\n', 1);
		if(s == nil) break;
		nf = getfields(s, f, nelem(f), 1, " \t");
		if(nf < 3) continue;
		x = strtol(f[0], &p, 0);
		if(*p != 0) continue;
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
			n = read(fd, buf, sizeof(buf)-1);
			if(n <= 0)
				sysfatal("read /dev/kbd: %r");
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
			if(k == nil) vmerror("unknown key %d", r);
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
				if(clicked && (m.buttons & 1) == 0 && !textmode){
					mousegrab = 1;
					setcursor(mc, &blank);
				}
				clicked = m.buttons & 1;
				break;
			}
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
	uchar *p;
	int y, x;
	Point pt;
	
	draw(img, img->r, display->black, nil, ZP);
	for(y = 0; y < 25; y++){
		p = &fb[y * 160];
		for(x = 0; x < 80; x++)
			buf[x] = cp437[p[2*x]];
		runestringn(img, Pt(0, 16 * y), display->white, ZP, display->defaultfont, buf, 80);
	}
	if(cursorpos < 80*25){
		buf[0] = cp437[fb[cursorpos*2]];
		pt = Pt(cursorpos % 80 * 8, cursorpos / 80 * 16);
		draw(img, Rect(pt.x, pt.y, pt.x + 8, pt.y + 16), display->white, nil, ZP);
		runestringn(img, pt, display->black, ZP, display->defaultfont, buf, 1);
	}
	draw(screen, picr, img, nil, ZP);
	flushimage(display, 1);	
}

static void
drawfb(void)
{
	u32int *p, *q;
	Rectangle upd;
	int xb, y;

	p = (u32int *) fb;
	q = (u32int *) sfb;
	upd.min.y = upd.max.y = -1;
	xb = 0;
	y = 0;
	while(p < (u32int*)(fb + fbsz)){
		if(*p != *q){
			if(upd.min.y < 0) upd.min.y = y;
			upd.max.y = y + 1 + (xb + 4 > hbytes);
			*q = *p;
		}
		p++;
		q++;
		xb += 4;
		if(xb >= hbytes){
			xb -= hbytes;
			y++;
		}
	}
	if(upd.min.y == upd.max.y) return;
	upd.min.x = 0;
	upd.max.x = picw;
	if(screenchan != screen->chan){
		loadimage(img, upd, sfb + upd.min.y * hbytes, (upd.max.y - upd.min.y) * hbytes);
		draw(screen, rectaddpt(upd, picr.min), img, nil, upd.min);
	}else
		loadimage(screen, rectaddpt(upd, picr.min), sfb + upd.min.y * hbytes, (upd.max.y - upd.min.y) * hbytes);
	flushimage(display, 1);
}

void
drawproc(void *)
{
	ulong ul;

	threadsetname("draw");
	sfb = emalloc(fbsz);
	for(;; sleep(20)){
		while(nbrecv(mc->resizec, &ul) > 0){
			if(getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			screeninit();
		}
		if(textmode)
			drawtext();
		else
			drawfb();
	}
}

void
vgafbparse(char *fbstring)
{
	char buf[512];
	char *p, *q;
	uvlong addr;

	if(picw != 0) sysfatal("vga specified twice");
	if(strcmp(fbstring, "text") == 0){
		picw = 640;
		pich = 400;
		fbsz = 80*25*2;
		fbaddr = 0xb8000;
		textmode++;
		screenchan = 0;
	}else{
		strecpy(buf, buf + nelem(buf), fbstring);
		picw = strtol(buf, &p, 10);
		if(*p != 'x')
		nope:
			sysfatal("vgafbparse: invalid framebuffer specifier: %#q (should be WxHxCHAN@ADDR or 'text')", fbstring);
		pich = strtol(p+1, &p, 10);
		if(*p != 'x') goto nope;
		q = strchr(p+1, '@');
		if(q == nil) goto nope;
		*q = 0;
		screenchan = strtochan(p+1);
		if(screenchan == 0) goto nope;
		p = q + 1;
		if(*p == 0) goto nope;
		addr = strtoull(p, &p, 0);
		fbaddr = addr;
		if(fbaddr != addr) goto nope;
		if(*p != 0) goto nope;
		hbytes = chantodepth(screenchan) * picw + 7 >> 3;
		fbsz = hbytes * pich;
	}
}

void
vgainit(void)
{
	char buf[512];

	if(picw == 0) return;
	fb = gptr(fbaddr, fbsz);
	if(fb == nil)
		sysfatal("got nil ptr for framebuffer");
	snprint(buf, sizeof(buf), "-dx %d -dy %d", picw+50, pich+50);
	newwindow(buf);
	initdraw(nil, nil, "vmx");
	screeninit();
	flushimage(display, 1);
	kbdlayout("/sys/lib/kbmap/us");
	mc = initmouse(nil, screen);
	kbdch = chancreate(sizeof(ulong), 128);
	mousech = chancreate(sizeof(Mouse), 32);
	proccreate(mousethread, nil, 4096);
	proccreate(keyproc, nil, 4096);
	proccreate(drawproc, nil, 4096);
}
