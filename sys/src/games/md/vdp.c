#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

u8int pic[320*224*4*3*3];
u16int vdpstat = 0x3400;
int vdpx, vdpy;
u16int hctr;
static int xmax, xdisp, ymax = 262, yvbl = 234;
static int sx, snx, col, pri;

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
}

static void
pixeldraw(int x, int y, int v)
{
	u8int *p;
	u32int *q;
	union { u32int w; u8int b[4]; } u;

	if(scale == 1){
		p = pic + (x + y * 320) * 4;
		p[0] = v >> 16;
		p[1] = v >> 8;
		p[2] = v;
		return;
	}
	u.b[0] = v >> 16;
	u.b[1] = v >> 8;
	u.b[2] = v;
	u.b[3] = 0;
	if(scale == 2){
		q = (u32int*)pic + (x + y * 320) * 2;
		q[0] = u.w;
		q[1] = u.w;
	}else{
		q = (u32int*)pic + (x + y * 320) * 3;
		q[0] = u.w;
		q[1] = u.w;
		q[2] = u.w;
	}
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

static void
tile(struct pctxt *p)
{
	u16int a;
	int y;
	
	switch(p - pctxt){
	default: a = (reg[PANT] & 0x38) << 9; break;
	case 1: a = (reg[PBNT] & 7) << 12; break;
	case 2: a = (reg[PWNT] & 0x38) << 9; break;
	}
	a += p->ty << p->ws;
	a += p->tx;
	p->t = vram[a];
	y = p->tny;
	if((p->t & 0x1000) != 0)
		y = 7 - y;
	a = (p->t & 0x7ff) << 4 | y << 1;
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
	for(i = 0; i < 2; i++){
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
		v = vsram[i] + vdpy;
		p->tny = v & 7;
		p->ty = v >> 3 & pctxt[i].h - 1;
		tile(pctxt + i);
		if(p->tnx != 0)
			if((p->t & 0x800) != 0)
				p->c >>= p->tnx << 2;
			else
				p->c <<= p->tnx << 2;
	}
	sx = 0;
	snx = 0;
}

static void
plane(int n)
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
	if(v != 0){
		v |= p->t >> 9 & 48;
		pr = 2 - (n & 1) + (p->t >> 13 & 4);
		pixel(v, pr);
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
	int i;
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
	plane(0);
	plane(1);
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
	u16int t, *p, dy, *c;
	u32int v;
	int i, ns, np;
	struct sprite *q;
	
	t = (reg[SPRTAB] << 8 & 0x7f00);
	p = vram + t;
	q = spr;
	ns = (reg[MODE4] & WIDE) != 0 ? 20 : 16;
	np = 0;
	do{
		q->y = (p[0] & 0x3ff) - 128;
		q->h = (p[1] >> 8 & 3) + 1 << 3;
		dy = vdpy - q->y;
		if(dy >= q->h)
			continue;
		q->t = p[2];
		if((q->t & 0x1000) != 0)
			dy = q->h + ~dy;
		q->x = (p[3] & 0x3ff) - 128;
		if(q->x == 0xff80)
			break;
		q->w = (p[1] >> 10 & 3) + 1 << 3;
		c = vram + ((q->t & 0x7ff) << 4) + (dy << 1);
		for(i = 0; i < q->w >> 3 && np < xdisp; i++){
			v = c[0] << 16 | c[1];
			c += q->h << 1;
			if((q->t & 0x800) != 0)
				q->c[(q->w >> 3) - 1 - i] = v;
			else
				q->c[i] = v;
			np += 8;
		}
		if(-q->x < q->w)
			if((q->t & 0x800) != 0)
				q->c[-q->x>>3] >>= (-q->x & 7) << 2;
			else
				q->c[-q->x>>3] <<= (-q->x & 7) << 2;
		if(++q == spr + ns || np >= xdisp){
			vdpstat |= STATOVR;
			break;
		}
	}while(p = vram + (u16int)(t + ((p[1] & 0x7f) << 2)), p - vram != t);
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
		pixel(col, set >> 13 | 2);
}

void
vdpstep(void)
{
	if(vdpx == 0){
		planeinit();
		spritesinit();
	}
	if(vdpx < 320 && vdpy < 224)
		if(vdpx < xdisp){
			col = reg[BGCOL] & 0x3f;
			pri = 0;
			planes();
			sprites();
			pixeldraw(vdpx, vdpy, cramc[col]);
		}else
			pixeldraw(vdpx, vdpy, 0xcccccc);
	if(++vdpx >= xmax){
		vdpx = 0;
		if(++vdpy >= ymax){
			vdpy = 0;
			irq &= ~INTVBL;
			vdpstat ^= STATFR;
			vdpstat &= ~(STATINT | STATFR | STATOVR | STATCOLL);
			flush();
		}
		if(vdpy == 0 || vdpy >= 225)
			hctr = reg[HORCTR];
		else
			if(--hctr == 0){
				if((reg[MODE1] & IE1) != 0)
					irq |= INTHOR;
				hctr = reg[HORCTR];
			}
		if(vdpy == yvbl){
			vdpstat |= STATVBL | STATINT;
			if((reg[MODE2] & IE0) != 0)
				irq |= INTVBL;
		}
	}
}
