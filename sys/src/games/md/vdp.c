#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u16int vdpstat = 0x3400;
int vdpx, vdpy, vdpyy, frame, intla;
u16int hctr;
static int xmax, xdisp;
static int sx, snx, col, pri, lum;
enum { DARK, NORM, BRIGHT };
enum { ymax = 262, yvbl = 234 };

void
vdpmode(void)
{
	if((reg[MODE4] & WIDE) != 0){
		xmax = 406;
		xdisp = 320;
	}else{
		xdisp = 256;
		xmax = 342;
	}
	intla = (reg[MODE4] & 6) == 6;
}

static void
pixeldraw(int x, int y, int v)
{
	u32int *p;
	union { u32int l; u8int b[4]; } u;

	p = (u32int *)pic + (x + y * 320) * scale;
	u.b[0] = v >> 16;
	u.b[1] = v >> 8;
	u.b[2] = v;
	u.b[3] = 0;
	switch(scale){
	case 16: *p++ = u.l;
	case 15: *p++ = u.l;
	case 14: *p++ = u.l;
	case 13: *p++ = u.l;
	case 12: *p++ = u.l;
	case 11: *p++ = u.l;
	case 10: *p++ = u.l;
	case 9: *p++ = u.l;
	case 8: *p++ = u.l;
	case 7: *p++ = u.l;
	case 6: *p++ = u.l;
	case 5: *p++ = u.l;
	case 4: *p++ = u.l;
	case 3: *p++ = u.l;
	case 2: *p++ = u.l;	/* intla ignored */
	default: *p = u.l;
	}
}

static u32int
shade(u32int v, int l)
{
	if(l == 1)
		return v;
	if(l == 2)
		return v << 1 & 0xefefef;
	return v >> 1 & 0xf7f7f7;
}

static void
pixel(int v, int p)
{
	if(p >= pri){
		col = v;
		pri = p;
	}
}

struct pctxt {
	u8int ws, w, hs, h;
	u16int tx, ty;
	u8int tnx, tny;
	u16int t;
	u32int c;
} pctxt[3];
int lwin, rwin;

static void
tile(struct pctxt *p)
{
	u16int a;
	int y;
	
	switch(p - pctxt){
	default: a = (reg[PANT] & 0x38) << 9; break;
	case 1: a = (reg[PBNT] & 7) << 12; break;
	case 2: a = (reg[PWNT] & 0x3e) << 9; break;
	}
	a += p->ty << p->ws;
	a += p->tx;
	p->t = vram[a];
	y = p->tny;
	if(intla){
		if((p->t & 0x1000) != 0)
			y = 15 - y;
		a = (p->t & 0x7ff) << 5 | y << 1;
	}else{
		if((p->t & 0x1000) != 0)
			y = 7 - y;
		a = (p->t & 0x7ff) << 4 | y << 1;
	}
	p->c = vram[a] << 16 | vram[a+1];
}

static void
planeinit(void)
{
	static int szs[] = {5, 6, 6, 7};
	int v, a, i;
	struct pctxt *p;
	
	pctxt[0].hs = pctxt[1].hs = szs[reg[PLSIZ] >> 4 & 3];
	pctxt[0].ws = pctxt[1].ws = szs[reg[PLSIZ] & 3];
	pctxt[2].ws = (reg[MODE4] & WIDE) != 0 ? 6 : 5;
	pctxt[2].hs = 5;
	for(i = 0; i <= 2; i++){
		pctxt[i].h = 1<<pctxt[i].hs;
		pctxt[i].w = 1<<pctxt[i].ws;
	}
	a = reg[HORSCR] << 9 & 0x7fff;
	switch(reg[MODE3] & 3){
	case 1: a += vdpy << 1 & 0xe; break;
	case 2: a += vdpy << 1 & 0xff0; break;
	case 3: a += vdpy << 1 & 0xffe; break;
	}
	for(i = 0; i < 2; i++){
		p = pctxt + i;
		v = -(vram[a + i] & 0x3ff);
		p->tnx = v & 7;
		p->tx = v >> 3 & pctxt[i].w - 1;
		if(intla){
			v = vsram[i] + vdpyy;
			p->tny = v & 15;
			p->ty = v >> 4 & pctxt[i].h - 1;
		}else{
			v = vsram[i] + vdpy;
			p->tny = v & 7;
			p->ty = v >> 3 & pctxt[i].h - 1;
		}
		tile(p);
		if(p->tnx != 0)
			if((p->t & 0x800) != 0)
				p->c >>= p->tnx << 2;
			else
				p->c <<= p->tnx << 2;
	}
	sx = 0;
	snx = 0;
	v = reg[WINV] << 3 & 0xf8;
	if((reg[WINV] & 0x80) != 0 ? vdpy < v : vdpy >= v){
		lwin = 0;
		rwin = reg[WINH] << 4 & 0x1f0;
		if((reg[WINH] & 0x80) != 0){
			lwin = rwin;
			rwin = 320;
		}
	}else{
		lwin = 0;
		rwin = 320;
	}
	if(rwin > lwin){
		p = pctxt + 2;
		p->tx = lwin >> 3 & pctxt[2].w - 1;
		p->tnx = lwin & 7;
		p->tny = vdpy & 7;
		p->ty = vdpy >> 3 & pctxt[2].h - 1;
		tile(p);
	}
}

static void
plane(int n, int vis)
{
	struct pctxt *p;
	u8int v, pr;
	
	p = pctxt + n;
	if((p->t & 0x800) != 0){
		v = p->c & 15;
		p->c >>= 4;
	}else{
		v = p->c >> 28;
		p->c <<= 4;
	}
	if(vis != 0){
		if(v != 0){
			v |= p->t >> 9 & 48;
			pr = 2 - (n & 1) + (p->t >> 13 & 4);
			pixel(v, pr);
		}
		lum |= p->t >> 15;
	}
	if(++p->tnx == 8){
		p->tnx = 0;
		if(++p->tx == p->w)
			p->tx = 0;
		tile(pctxt + n);
	}
}

static void
planes(void)
{
	int i, w;
	u16int v;

	if((reg[MODE3] & 4) != 0 && ++snx == 16){
		snx = 0;
		sx++;
		for(i = 0; i < 2; i++){
			v = vsram[sx + i] + vdpy;
			pctxt[i].tny = v & 7;
			pctxt[i].ty = v >> 3 & pctxt[i].h - 1;
		}
	}
	w = vdpx < rwin && vdpx >= lwin;
	plane(0, !w);
	plane(1, 1);
	if(w)
		plane(2, 1);
}

static struct sprite {
	u16int y, x;
	u8int w, h;
	u16int t;
	u32int c[4];
} spr[21], *lsp;

static void
spritesinit(void)
{
	u16int t, *p, dy, c;
	u32int v;
	int i, ns, np, nt;
	struct sprite *q;
	
	t = (reg[SPRTAB] << 8 & 0x7f00);
	p = vram + t;
	q = spr;
	ns = (reg[MODE4] & WIDE) != 0 ? 20 : 16;
	np = 0;
	nt = 0;
	do{
		if(intla){
			q->y = (p[0] & 0x3ff) - 256;
			q->h = (p[1] >> 8 & 3) + 1 << 4;
			dy = vdpyy - q->y;
		}else{
			q->y = (p[0] & 0x3ff) - 128;
			q->h = (p[1] >> 8 & 3) + 1 << 3;
			dy = vdpy - q->y;
		}
		if(dy >= q->h)
			continue;
		q->t = p[2];
		if((q->t & 0x1000) != 0)
			dy = q->h + ~dy;
		q->x = (p[3] & 0x3ff) - 128;
		if(q->x == 0xff80)
			break;
		q->w = (p[1] >> 10 & 3) + 1 << 3;
		c = ((q->t & 0x7ff) << 4+intla) + (dy << 1);
		for(i = 0; i < q->w >> 3 && np < xdisp; i++){
			v = vram[c] << 16 | vram[(u16int)(c+1)];
			c += q->h << 1;
			if((q->t & 0x800) != 0)
				q->c[(q->w >> 3) - 1 - i] = v;
			else
				q->c[i] = v;
			np += 8;
		}
		if((u16int)-q->x < q->w){
			i = -(s16int)q->x;
			if((q->t & 0x800) != 0)
				q->c[i>>3] >>= (i & 7) << 2;
			else
				q->c[i>>3] <<= (i & 7) << 2;
		}
		if(++q == spr + ns || np >= xdisp){
			vdpstat |= STATOVR;
			break;
		}
	}while(p = vram + (u16int)(t + ((p[1] & 0x7f) << 2)), p - vram != t && ++nt < 80);
	lsp = q;
}

static void
sprites(void)
{
	struct sprite *p;
	u16int dx;
	int v, col, set;
	u32int *c;
	
	set = 0;
	col = 0;
	for(p = spr; p < lsp; p++){
		dx = vdpx - p->x;
		if(dx >= p->w)
			continue;
		c = p->c + (dx >> 3);
		if((p->t & 0x800) != 0){
			v = *c & 15;
			*c >>= 4;
		}else{
			v = *c >> 28;
			*c <<= 4;
		}
		if(v != 0)
			if(set != 0)
				vdpstat |= STATCOLL;
			else{
				set = 1 | p->t & 0x8000;
				col = p->t >> 9 & 48 | v;
			}
	}
	if(set)
		if((reg[MODE4] & SHI) != 0)
			if((col & 0xfe) == 0x3e)
				lum = col & 1;
			else{
				pixel(col, set >> 13 | 2);
				if((col & 0xf) == 0xe)
					lum = 1;
				else
					lum |= set >> 15;
			}
		else
			pixel(col, set >> 13 | 2);
}

void
vdpstep(void)
{
	u32int v;

	if(vdpx == 0){
		planeinit();
		spritesinit();
	}
	if(vdpx < 320 && vdpy < 224)
		if(vdpx < xdisp){
			col = reg[BGCOL] & 0x3f;
			pri = 0;
			lum = 0;
			planes();
			sprites();
			if((reg[MODE2] & 0x40) != 0 && (vdpx >= 8 || (reg[MODE1] & 0x20) == 0)){
				v = cramc[col];
				if((reg[MODE4] & SHI) != 0)
					v = shade(v, lum);
				pixeldraw(vdpx, vdpy, v);
			}else
				pixeldraw(vdpx, vdpy, 0);
		}else
			pixeldraw(vdpx, vdpy, 0xcccccc);
	if(++vdpx >= xmax){
		z80irq = 0;
		vdpx = 0;
		if(++vdpy >= ymax){
			vdpy = 0;
			irq &= ~INTVBL;
			vdpstat ^= STATFR;
			vdpstat &= ~(STATINT | STATVBL | STATOVR | STATCOLL);
			flush();
		}
		if(intla)
			vdpyy = vdpy << 1 | frame;
		if(vdpy == 0 || vdpy >= 225)
			hctr = reg[HORCTR];
		else
			if(hctr-- == 0){
				if((reg[MODE1] & IE1) != 0)
					irq |= INTHOR;
				hctr = reg[HORCTR];
			}
		if(vdpy == yvbl){
			vdpstat |= STATVBL | STATINT;
			frame ^= 1;
			z80irq = 1;
			if((reg[MODE2] & IE0) != 0)
				irq |= INTVBL;
		}
	}
}
