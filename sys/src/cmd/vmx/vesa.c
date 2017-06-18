#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static uchar vesabios[512] = {
0x55, 0xaa, 0x01, 0xcb, 0x60, 0x1e, 0x06, 0x8c, 0xd0, 0xba, 0xe0, 0xfe, 0xef, 0x89, 0xe0, 0xba, 0xe1, 0xfe, 0xef, 0xba, 0xe2, 0xfe, 0xed, 0x85, 0xc0, 0x7c, 0x28, 0x50, 0x25, 0x00, 
0xf0, 0x8e, 0xc0, 0xba, 0xe3, 0xfe, 0xed, 0x89, 0xc6, 0xc3, 0x26, 0x8b, 0x04, 0xba, 0xe4, 0xfe, 0xef, 0xeb, 0xe2, 0xba, 0xe4, 0xfe, 0xed, 0x26, 0x89, 0x04, 0xeb, 0xd9, 0xba, 0xe4, 
0xfe, 0xed, 0x26, 0x88, 0x04, 0xeb, 0xd0, 0x58, 0x58, 0x61, 0xcf,
};
enum {
	READCMD = 0x28,
	WRITEWCMD = 0x31,
	WRITEBCMD = 0x3a,
};
typedef struct VESAIO VESAIO;
struct VESAIO {
	u8int port;
	u16int val;
};
Channel *vesawchan, *vesarchan;
typedef struct Ureg16 Ureg16;
struct Ureg16 {
	u16int ax, bx, cx, dx;
	u16int si, di, bp;
	u16int ds, es;
};
#define ESDI(u) ((u).di + ((u).es<<4))
typedef struct Vesa Vesa;
struct Vesa {
	u32int romptr;
	u32int oemstring, oemvendor, oemproduct, oemproductrev;
	u32int modetab;
	u8int pal8;
} vesa;
#define FARPTR(x) (((x)&0xf0000)<<12|(u16int)(x))
extern VgaMode *modes, **modeslast, *curmode, *nextmode, textmode;
extern int curhbytes, nexthbytes;
extern uintptr fbaddr, fbsz;
extern int maxw, maxh;
enum { CMAP4 = CHAN1(CMap, 4) };


static VgaMode *
findmode(u16int m)
{
	VgaMode *p;
	
	for(p = modes; p != nil; p = p->next)
		if(p->no == m)
			return p;
	return nil;
}

static int
vesagetsp(void)
{
	VESAIO io;
	u32int rc;

loop:
	while(recv(vesarchan, &io), io.port != 0)
		sendul(vesawchan, -1);
	rc = io.val << 4;
	sendul(vesawchan, -1);	
	recv(vesarchan, &io);
	sendul(vesawchan, -1);
	if(io.port != 1) goto loop;
	return rc + io.val;
}

static int
vesawrite(int addr, u32int val, int sz)
{
	VESAIO io;

	assert(sz == 1 || sz == 2 || sz == 4);
	recv(vesarchan, &io);
	if(io.port != 0x12){
	no:	sendul(vesawchan, -1);
		return -1;
	}
	sendul(vesawchan, (sz > 1 ? WRITEWCMD : WRITEBCMD) | addr >> 4 & 0xf000);
	recv(vesarchan, &io);
	if(io.port != 0x13) goto no;
	sendul(vesawchan, addr);
	recv(vesarchan, &io);
	if(io.port != 0x14) goto no;
	sendul(vesawchan, val);
	if(sz == 4)
		return vesawrite(addr + 2, val >> 16, 2);
	return 0;	
}

static int
vesaread(int addr)
{
	VESAIO io;

	recv(vesarchan, &io);
	if(io.port != 0x12){
	no:	sendul(vesawchan, -1);
		return -1;
	}
	sendul(vesawchan, READCMD);
	recv(vesarchan, &io);
	if(io.port != 0x13) goto no;
	sendul(vesawchan, addr);
	recv(vesarchan, &io);
	if(io.port != 0x4) goto no;
	sendul(vesawchan, -1);
	return io.val;	
}

static int
vesaregs(u32int sp, Ureg16 *ur)
{
	int rc;
	#define R(n, a) rc = vesaread(sp + n); if(rc < 0) return -1; ur->a = rc
	
	memset(ur, 0, sizeof(*ur));
	R(0, es);
	R(2, ds);
	R(4, di);
	R(6, si);
	R(8, bp);
	R(12, bx);
	R(14, dx);
	R(16, cx);
	R(18, ax);
	return 0;
	#undef R
}

static int
vesasetregs(u32int sp, Ureg16 *ur)
{
	#define R(n, a) if(vesawrite(sp + n, ur->a, 2) < 0) return -1;
	
	R(0, es);
	R(2, ds);
	R(4, di);
	R(6, si);
	R(8, bp);
	R(12, bx);
	R(14, dx);
	R(16, cx);
	R(18, ax);
	return 0;
	#undef R
}

#define vesasetax(sp, val) vesawrite(sp+18, val, 2)
#define vesasetbx(sp, val) vesawrite(sp+12, val, 2)

static int
vesapack(int addr, char *fmt, ...)
{
	va_list va;
	static u8int checksum;
	int v;
	char *p;
	int numb;
	
	if(addr < 0) return -1;
	numb = 0;
	va_start(va, fmt);
	for(; *fmt != 0; fmt++)
		switch(*fmt){
		case ' ': break;
		case 'b': v = va_arg(va, int); if(vesawrite(addr, v, 1) < 0) return -1; addr += 1; checksum += v; break;
		case 'w': v = va_arg(va, int); if(vesawrite(addr, v, 2) < 0) return -1; addr += 2; checksum += v + (v >> 8); break;
		case 'd': v = va_arg(va, int); if(vesawrite(addr, v, 4) < 0) return -1; addr += 4; checksum += v + (v >> 8) + (v >> 16) + (v >> 24); break;
		case 's': 
			p = va_arg(va, char *);
			for(; *p != 0; p++, addr++){
				if(vesawrite(addr, *p, 1) < 0)
					return -1;
				checksum += *p;
			}
			break;
		case 'S':
			p = va_arg(va, char *);
			v = va_arg(va, int);
			while(v-- > 0){
				if(vesawrite(addr++, *p, 1) < 0)
					return -1;
				checksum += *p++;
			}
			break;
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			numb = strtol(fmt, &fmt, 10);
			fmt--;
			break;
		case 'f':
			for(; numb >= 2; numb -= 2, addr += 2)
				if(vesawrite(addr, 0, 2) < 0) return -1;
			if(numb == 1){
				if(vesawrite(addr++, 0, 1) < 0) return -1;
				numb = 0;
			}
			break;
		case 'c': if(vesawrite(addr, -checksum, 1) < 0) return -1; addr += 1; break;
		case 'C': checksum = 0; break;
		default: vmerror("vesapack: unknown char %c", *fmt); return -1;
		}
	va_end(va);
	return addr;
}

static int
vesarompack(char *fmt, ...)
{
	va_list va;
	uchar *v, *v0;
	int rc, x;
	void *s;
	
	v0 = gptr(vesa.romptr, 0x8000);
	v = v0;
	assert(v != nil);
	va_start(va, fmt);
	for(; *fmt != 0; fmt++)
		switch(*fmt){
		case 'b': *(u8int*)v = va_arg(va, int); v++; break;
		case 'w': *(u16int*)v = va_arg(va, int); v += 2; break;
		case 'd': *(u32int*)v = va_arg(va, int); v += 4; break;
		case 'a': v = (uchar*)strecpy((char*)v, (char*)v + 0x8000, va_arg(va, char*)); *v++ = 0; break;
		case 'S': s = va_arg(va, void *); x = va_arg(va, int); memcpy(v, s, x); v += x; break;
		default: sysfatal("vesarompack: unknown char %c", *fmt);
		}
	va_end(va);
	rc = vesa.romptr;
	vesa.romptr += v - v0;
	return rc;
}

static int
vesamodeget(int addr, u16int mode, u16int *retv)
{
	VgaMode *p;
	u8int model;
	u8int nred, pred, ngreen, pgreen, nblue, pblue, nx, px;
	int i, pos, s;
	
	p = findmode(mode);
	if(p == nil){
		vmerror("vesa: Return VBE Mode Information: unknown mode %#x", mode);
		*retv = 0x014F;
		return 0;
	}
	*retv = 0x004F;
	model = 6;
	nred = pred = ngreen = pgreen = 0;
	nblue = pblue = nx = px = 0;
	pos = 0;
	for(i = 0; i < 4; i++){
		s = p->chan >> 8 * i & 15;
		if(s == 0) continue;
		switch(p->chan >> 8 * i + 4 & 15){
		case CRed: nred = s; pred = pos; break;
		case CGreen: ngreen = s; pgreen = pos; break;
		case CBlue: nblue = s; pblue = pos; break;
		case CAlpha: case CIgnore: nx = s; px = pos; break;
		case CMap: model = 4; break;
		}
		pos += s;
	}
	return vesapack(addr, "wbbwwwwdw wwbbbbbbbbb bbbbbbbbb ddw wbbbbb bbbbbd 189f",
		1<<0|1<<1|1<<3|1<<4|1<<5|1<<6|1<<7, /* attributes: color graphics, vga incompatible, linear framebuffer */
		0, 0, 0, 0, 0, 0, 0, p->hbytes, /* windowing crap */
		p->w, p->h,
		0, 0, /* character size */
		1, chantodepth(p->chan), 1, /* 1 bank, 1 plane, N bpp */
		model, /* memory model */
		0, 0, 1, /* no banking/paging */
		nred, pred, ngreen, pgreen, nblue, pblue, nx, px, /* masks */
		0, /* no ramp, reserved bits are reserved */
		fbaddr,
		0, 0, /* reserved */
		p->hbytes, 0, 0,
		nred, pred, ngreen, pgreen, nblue, pblue, nx, px, /* masks */
		p->w * p->h * 60 * 2 /* max pixelclock */);
}

static int
vesamodeset(u16int mode, u16int *retv)
{
	VgaMode *p;
	
	mode &= 0x1ff;
	if(mode == 3){
		*retv = 0x04F;
		nextmode = &textmode;
		return 0;
	}
	p = findmode(mode);
	if(p == nil){
		vmerror("vesa: Set VBE Mode: unknown mode %#x", mode);
		*retv = 0x14F;
		return 0;
	}
	*retv = 0x04F;
	nextmode = p;
	nexthbytes = p->hbytes;
	vesa.pal8 = 0;
	return 0;
}

static int
vesalinelen(Ureg16 *ur)
{
	int d;
	int nhb, x;
	
	d = chantodepth(nextmode->chan);
	if(nextmode == &textmode || d == 0) goto fail;
	switch(ur->bx & 0xff){
	case 0:
		nhb = d * ur->cx / 8;
	set:
		if((d & 7) != 0){
			vmerror("vesa: set logical length unsupported on bitdepth < 8");
		fail:	ur->ax = 0x014F;
			return 0;
		}
		if(nhb * nextmode->h > fbsz){
			ur->ax = 0x024F;
			return 0;
		}
		nexthbytes = nhb;
		ur->ax = 0x4F;
		ur->bx = nhb;
		ur->cx = nhb * 8 / d;
		ur->dx = fbsz / nhb;
		break;
	case 1:
		ur->ax = 0x4F;
		ur->bx = nexthbytes;
		ur->cx = nexthbytes * 8 / d;
		ur->dx = fbsz / nexthbytes;
		break;
	case 2:
		nhb = ur->cx;
		goto set;
	case 3:
		x = fbsz / nextmode->h;
		ur->ax = 0x4F;
		ur->bx = x;
		ur->cx = x * 8 / d;
		ur->dx = nextmode->h;
		break;
	default:
		vmerror("vesa: unsupported subfunction %#x of SetGetLogicalLineLength", ur->bx & 0xff);
	}
	return 0;
}

static int
vesapalformat(Ureg16 *ur)
{
	switch(ur->bx & 0xff){
	case 0:
		vesa.pal8 = (ur->bx >> 8) >= 8;
	case 1:
		ur->ax = 0x4F;
		ur->bx = vesa.pal8 ? 8 : 6;
		break;
	default:
		vmerror("vesa: unsupported subfunction %#x of SetGetDACPaletteFormat", ur->bx & 0xff);
	}
	return 0;
}

static int
vesapal(Ureg16 *ur)
{
	int i;
	int addr;
	u32int c;
	int r, g, b;

	switch(ur->bx & 0xff){
	case 0:
	case 0x80:
		if(ur->dx >= 256 || ur->cx > 256){
			ur->ax = 0x014F;
			return 0;
		}
		ur->ax = 0x4F;
		addr = ESDI(*ur);
		for(i = ur->dx; i < ur->dx + ur->cx; i++){
			b = vesaread(addr);
			if(b < 0) return -1;
			g = b >> 8;
			b = b & 0xff;
			r = vesaread(addr + 2);
			if(r <  0) return -1;
			r &= 0xff;
			if(!vesa.pal8){
				r <<= 2;
				g <<= 2;
				b <<= 2;
			}
			vgasetpal(i, r << 24 | g << 16 | b << 8 | 0xff);
			addr += 4;
		}
		break;
	case 0x1:
		if(ur->dx >= 256 || ur->cx > 256){
			ur->ax = 0x014F;
			return 0;
		}
		ur->ax = 0x4F;
		addr = ESDI(*ur);
		for(i = ur->dx; i < ur->dx + ur->cx; i++){
			c = vgagetpal(i);
			r = c >> 24; g = c >> 16; b = c >> 8;
			if(!vesa.pal8){
				r >>= 2;
				g >>= 2;
				b >>= 2;
			}
			addr = vesapack(addr, "bbbb", b, g, r, 0);
		}
		return addr >= 0 ? 0 : -1;
	default:
		vmerror("vesa: unsupported subfunction %#x of SetGetPaletteData", ur->bx & 0xff);
	}
	return 0;
}

static void
stdtiming(u8int *arr)
{
	VgaMode *m;
	u8int s1, s2;
	int i;
	
	memset(arr, 1, 16);
	for(m = modes; m != nil; m = m->next){
		if(m->w < 256 || m->w > 2288 || m->w > maxw || m->h > maxh) continue;
		if((m->w & 7) != 0) continue;
		if(m->w == 640 && m->h == 480) continue;
		if(m->w == 800 && m->h == 600) continue;
		if(m->w == 1024 && m->h == 768) continue;
		if(m->w == 1280 && m->h == 1024) continue;
		if(m->w * 10 == m->w * 16)
			s2 = 0x00;
		else if(m->w * 3 == m->h * 4)
			s2 = 0x40;
		else if(m->w * 4 == m->h * 5)
			s2 = 0x80;
		else if(m->w * 9 == m->w * 16)
			s2 = 0xc0;
		else
			continue;
		s1 = m->w / 8 - 31;
		for(i = 0; i < 16; i += 2){
			if(arr[i] == s1 && arr[i+1] == s2)
				goto skip;
			if(arr[i] == 1 && arr[i+1] == 1)
				break;
		}
		if(i == 16) skip: continue;
		arr[i] = s1;
		arr[i+1] = s2;
	}
}

static int
detailtiming(int addr, VgaMode **m)
{
	u32int hv, hfp, hsp, hbp, hmm;
	u32int vv, vfp, vsp, vbp, vmm;
	u32int freq;

again:
	if(*m == nil)
		return vesapack(addr, "d14f", 0x10000000);
	/* when in doubt, make shit up */
	hv = (*m)->w;
	hfp = hv / 40;
	hsp = hv * 3 / 20;
	hbp = hfp * 4 + hsp;
	vv = (*m)->h;
	vfp = (vv + 24) / 48;
	vsp = (vv + 120) / 240;
	vbp = vfp * 3 + vsp;
	freq = (hv + hfp + hsp + hbp) * (vv + vfp + vsp + vbp) * 60;
	hmm = hv * 254 / 960; /* assume 96 dpi */
	vmm = vv * 254 / 960;

	*m = (*m)->next;
	if(hv > maxw || vv > maxh || hv > 0xfff || hfp > 0x3ff || hsp > 0x3ff || vfp > 63 || vsp > 63)
		goto again;
	return vesapack(addr, "w bbbbbbbbbb bbbbb b",
		freq / 10000,
		hv, hbp, hv >> 4 & 0xf0 | hbp >> 8 & 0xf,
		vv, vbp, vv >> 4 & 0xf0 | vbp >> 8 & 0xf,
		hfp, hsp,
		vfp << 4 & 0xf0 | vsp & 0xf,
		hfp >> 2 & 0xc0 | hsp >> 4 & 0x30 | vfp >> 2 & 0x0c | vsp >> 4 & 0x03,
		hmm, vmm,
		hmm >> 4 & 0xf0 | vmm >> 8 & 0xf,
		0, 0, 0x1f);
		
}

static int
vesaddc(Ureg16 *ur)
{
	int addr;
	u32int tim;
	u8int std[16];
	VgaMode *m;

	switch(ur->bx & 0xff){
	case 0:
		ur->ax = 0x004F;
		ur->bx = 3;
		break;
	case 1:
		addr = ESDI(*ur);
		stdtiming(std);
		switch(ur->dx){
		case 0:
			ur->ax = 0x004F;
			tim = 0;
			if(maxw >= 640 && maxh >= 480) tim |= 0x3c;
			if(maxw >= 800 && maxh >= 600) tim |= 0xc003;
			if(maxw >= 1024 && maxh >= 768) tim |= 0xe00;
			if(maxw >= 1280 && maxh >= 1024) tim |= 0x100;
			addr = vesapack(addr, "Cdd bbbbdbb bb bbbbb bbbbb bbbbb wb S",
				0xFFFFFF00,
				0x00FFFFFF,
				0x0c, 0x34, 0xa7, 0xac, /* manufacturer & product id */
				1701, /* serial no */
				0xff, 27, /* model year 2017 */
				1, 4, /* edid version */
				0xa1, /* DVI input */
				maxw >= maxh ? maxw * 100 / maxh - 99 : 0, /* landscape aspect ratio */
				maxw < maxh ? maxh * 100 / maxw - 99 : 0, /* portrait aspect ratio */
				0x78, /* gamma=2.2 */
				7, /* srgb & have preferred timing mode */
				0, 0, 0, 0, 0, /* chromaticity coordinates */
				0, 0, 0, 0, 0,
				tim, tim >> 16,
				std, 16);
			m = modes;
			addr = detailtiming(addr, &m);
			addr = detailtiming(addr, &m);
			addr = detailtiming(addr, &m);
			addr = vesapack(addr, "3fbb bbbb bbbb bbbbb",
				0xfd, 0xa, 1, 0xff, 1, 0xff, 0xff, 0x04, 0x11,
				maxw >> 11 & 3, maxw >> 3, 0xf8, 0x18, 0, 60); 
			addr = vesapack(addr, "bc", 0);
			if(addr < 0) return -1;
			break;
		default:
			ur->ax = 0x014F;
		}
		break;
	default:
		vmerror("vesa: unsupported subfunction %#x of VBE/DDC", ur->bx & 0xff);
	}
	return 0;
}

static void
vesathread(void *)
{
	u32int sp;
	Ureg16 ur;

	for(;;){
		sp = vesagetsp();
//		vmdebug("SP = %.8ux", sp);
		if(vesaregs(sp, &ur) < 0) continue;
//		vmdebug("AX = %.4x BX=%.4x CX=%.4x DX=%.4x", ur.ax, ur.bx, ur.cx, ur.dx);
//		vmdebug("SI = %.4x DI=%.4x BP=%.4x", ur.si, ur.di, ur.bp);
//		vmdebug("DS = %.4x ES=%.4x", ur.ds, ur.es);
		switch(ur.ax){
		case 0x4f00:
			if(vesapack(ESDI(ur), "swdddwwddd",
				"VESA", /* VbeSignature */
				0x0300, /* VbeVersion */
				FARPTR(vesa.oemstring), /* OemStringPtr */
				1, /* Capabilities (support 8 bit colors) */
				FARPTR(vesa.modetab), /* VideoModePtr */
				fbsz >> 16, /* TotalMemory */
				0x100 /* OemSoftwareRev */,
				FARPTR(vesa.oemvendor), /* OemVendorNamePtr */
				FARPTR(vesa.oemproduct), /* OemProductNamePtr */
				FARPTR(vesa.oemproductrev) /* OemProductRevPtr */) < 0)
				continue;
			if(vesasetax(sp, 0x004F) < 0) continue;
			break;
		case 0x4f01:
			if(vesamodeget(ESDI(ur), ur.cx, &ur.ax) < 0) continue;
			if(vesasetax(sp, ur.ax) < 0) continue;
			break;
		case 0x4f02:
			if(vesamodeset(ur.bx, &ur.ax) < 0 || vesasetax(sp, ur.ax) < 0) continue;
			break;
		case 0x4f03:
			if(vesasetax(sp, 0x004F) < 0) continue;
			if(vesasetbx(sp, nextmode->no | 0x4000) < 0) continue;
			break;
		case 0x4f06:
			if(vesalinelen(&ur) < 0 || vesasetregs(sp, &ur) < 0) continue;
			break;
		case 0x4f08:
			if(vesapalformat(&ur) < 0 || vesasetregs(sp, &ur) < 0) continue;
			break;
		case 0x4f09:
			if(vesapal(&ur) < 0 || vesasetregs(sp, &ur) < 0) continue;
			break;
		case 0x4f15:
			if(vesaddc(&ur) < 0 || vesasetregs(sp, &ur) < 0) continue;
			break;
		default:
			vmerror("vesa: unsupported function %#x", ur.ax);
		}
	}
}

static void
addmode(int no, int x, int y, u32int chan)
{
	VgaMode *vm;
	
	vm = emalloc(sizeof(VgaMode));
	vm->no = no;
	vm->w = x;
	vm->h = y;
	vm->chan = chan;
	vm->hbytes = chantodepth(chan) * vm->w + 7 >> 3;
	vm->sz = vm->hbytes * y;
	*modeslast = vm;
	modeslast = &vm->next;
}

void
vesainit(void)
{
	void *v;
	int i, c;
	int mno;
	VgaMode *p;
	
	v = gptr(0xc0000, sizeof(vesabios));
	if(v == nil) sysfatal("vesainit: gptr failed");
	for(i = 0, c = 0; i < sizeof(vesabios) - 1; i++)
		c += vesabios[i];
	vesabios[sizeof(vesabios) - 1] = -c;
	vesa.romptr = 0xc0000;
	vesarompack("S", vesabios, sizeof(vesabios));
	
	v = gptr(0, 1024);
	if(v == nil) sysfatal("vesainit: gptr failed");
	PUT32(v, 0x40, 0xc0000004);	
	
	vesa.oemstring = vesarompack("a", "9front vmx(1)");
	vesa.oemvendor = vesarompack("a", "9front");
	vesa.oemproduct = vesarompack("a", "cat graphics adapter");
	vesa.oemproductrev = vesarompack("a", "∞");
	
	vesa.modetab = vesa.romptr;
	mno = 0x120;
	for(p = modes; p != nil; p = p->next){
		p->no = mno++;
		vesarompack("w", p->no);
	}
	vesarompack("w", 0xffff);

	addmode(0x100, 640, 400, CMAP8);
	addmode(0x101, 640, 480, CMAP8);
	addmode(0x102, 800, 600, CMAP4);
	addmode(0x6A, 800, 600, CMAP4);
	addmode(0x103, 800, 600, CMAP8);
	addmode(0x104, 1024, 768, CMAP4);
	addmode(0x105, 1024, 768, CMAP8);
	addmode(0x106, 1280, 1024, CMAP4);
	addmode(0x107, 1280, 1024, CMAP8);
	addmode(0x10D, 320, 200, RGB15);
	addmode(0x10E, 320, 200, RGB16);
	addmode(0x10F, 320, 200, RGB24);
	addmode(0x110, 640, 480, RGB15);
	addmode(0x111, 640, 480, RGB16);
	addmode(0x112, 640, 480, RGB24);
	addmode(0x113, 800, 600, RGB15);
	addmode(0x114, 800, 600, RGB16);
	addmode(0x115, 800, 600, RGB24);
	addmode(0x116, 1024, 768, RGB15);
	addmode(0x117, 1024, 768, RGB16);
	addmode(0x118, 1024, 768, RGB24);
	addmode(0x119, 1280, 1024, RGB15);
	addmode(0x11A, 1280, 1024, RGB16);
	addmode(0x11B, 1280, 1024, RGB24);
	
	vesarchan = chancreate(sizeof(VESAIO), 0);
	vesawchan = chancreate(sizeof(ulong), 0);
	threadcreate(vesathread, nil, 8192);
}

u32int
vesaio(int isin, u16int port, u32int val, int sz, void *)
{
	VESAIO io;
	
	if(sz != 2) return iowhine(isin, port, val, sz, nil);
	io.port = isin << 4 | port & 0xf;
	io.val = val;
	send(vesarchan, &io);
	return recvul(vesawchan);
}
