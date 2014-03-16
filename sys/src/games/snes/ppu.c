#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

int ppux, ppuy, rx;
static u8int mode, bright, pixelpri[2];
static u32int pixelcol[2];
u16int vtime = 0x1ff, htime = 0x1ff, subcolor, mosatop;
uchar pic[256*239*2*9];
u16int hofs[5], vofs[5];
s16int m7[6];

enum {
	M7A,
	M7B,
	M7C,
	M7D,
	M7X,
	M7Y
};

enum { OBJ = 4, COL = 5, OBJNC = 8 };

static u16int
darken(u16int v)
{
	u8int r, g, b;
	
	r = (v >> 10) & 0x1f;
	g = (v >> 5) & 0x1f;
	b = v & 0x1f;
	r = r * bright / 15;
	g = g * bright / 15;
	b = b * bright / 15;
	return r << 10 | g << 5 | b;
}

static void
pixeldraw(int x, int y, u16int v)
{
	uchar *p;
	u16int *q;
	union { u16int w; u8int b[2]; } u;
	int i;

	if(bright != 0xf)
		v = darken(v);
	if(scale == 1){
		p = pic + (x + y * 256) * 2;
		*p++ = v;
		*p = v >> 8;
		return;
	}
	u.b[0] = v;
	u.b[1] = v >> 8;
	if(scale == 2){
		q = (u16int*)pic + (x + y * 256 * 2) * 2;
		*q++ = u.w;
		*q = u.w;
		q += 256 * 2 - 1;
		*q++ = u.w;
		*q = u.w;
	}else{
		q = (u16int*)pic + (x + y * 256 * 3) * 3;
		for(i = 0; i < 3; i++){
			*q++ = u.w;
			*q++ = u.w;
			*q = u.w;
			q += 256 * 3 - 2;
		}
	}
}

static int
window(int n)
{
	int a, w1, w2;

	a = reg[0x2123 + (n >> 1)];
	if((n & 1) != 0)
		a >>= 4;
	if((a & (WIN1|WIN2)) == 0)
		return 0;
	w1 = rx >= reg[0x2126] && rx <= reg[0x2127];
	w2 = rx >= reg[0x2128] && rx <= reg[0x2129];
	if((a & INVW1) != 0)
		w1 = !w1;
	if((a & INVW2) != 0)
		w2 = !w2;
	if((a & (WIN1|WIN2)) != (WIN1|WIN2))
		return (a & WIN1) != 0 ? w1 : w2;
	a = reg[0x212a + (n >> 2)] >> ((n & 3) << 1);
	switch(a & 3){
	case 1: return w1 & w2;
	case 2: return w1 ^ w2;
	case 3: return w1 ^ w2 ^ 1;
	}
	return w1 | w2;
}

static void
pixel(int n, int v, int pri)
{
	int a;
	
	a = 1<<n;
	if((reg[TM] & a) != 0 && pri > pixelpri[0] && ((reg[TMW] & a) == 0 || !window(n))){
		pixelcol[0] = v;
		pixelpri[0] = pri;
	}
	if((reg[TS] & a) != 0 && pri > pixelpri[1] && ((reg[TSW] & a) == 0 || !window(n))){
		pixelcol[1] = v;
		pixelpri[1] = pri;
	}
}

static u16int
tile(int n, int tx, int ty)
{
	int a;
	u16int ta;
	u16int t;

	a = reg[0x2107 + n];
	ta = ((a & ~3) << 9) + ((tx & 0x1f) << 1) + ((ty & 0x1f) << 6);
	if((a & 1) != 0)
		ta += (tx & 0x20) << 6;
	if((a & 2) != 0)
		ta += (ty & 0x20) << (6 + (a & 1));
	t = vram[ta++];
	return t | vram[ta] << 8;
}

static void
chr(int n, int nb, int sz, u16int t, int x, int y, u32int c[])
{
	u16int a;

	if(sz == 16){
		if(y >= 8){
			t += ((x >> 3 ^ t >> 14) & 1) + ((~t >> 11) & 16);
			y -= 8;
		}else
			t += ((x >> 3 ^ t >> 14) & 1) + ((t >> 11) & 16);
	}
	if((t & 0x8000) != 0)
		y = 7 - y;
	a = reg[0x210b + (n >> 1)];
	if((n & 1) != 0)
		a >>= 4;
	else
		a &= 0xf;
	a = (a << 13) + (t & 0x3ff) * 8 * nb + y * 2;
	c[0] = vram[a++];
	c[0] |= vram[a] << 8;
	if(nb != 2){
		a += 15;
		c[0] |= vram[a++] << 16;
		c[0] |= vram[a] << 24;
		if(nb == 8){
			a += 15;
			c[1] = vram[a++];
			c[1] |= vram[a] << 8;
			a += 15;
			c[1] |= vram[a++] << 16;
			c[1] |= vram[a] << 24;
		}
	}
}

static int
palette(int n, int p)
{
	switch(mode){
	case 0:
		return p << 2 | n << 5;
	case 1:
		if(n >= 2)
			return p << 2;
	case 2:
	case 6:
		return p << 4;
	case 5:
		if(n == 0)
			return p << 4;
		return p << 2;
	case 3:
		if(n != 0)
			return p << 4;
	case 4:
		if(n != 0)
			return p << 2;
		if((reg[CGWSEL] & DIRCOL) != 0)
			return 0x10000;
	}
	return 0;
}

static void
shift(u32int *c, int nb, int n, int d)
{
	if(d){
		c[0] >>= n;
		if(nb == 8)
			c[1] >>= n;
	}else{
		c[0] <<= n;
		if(nb == 8)
			c[1] <<= n;
	}
}

static u8int
bgpixel(u32int *c, int nb, int d)
{
	u8int v;
	
	if(d){
		v = c[0] & 1 | c[0] >> 7 & 2;
		if(nb != 2){
			v |= c[0] >> 14 & 4 | c[0] >> 21 & 8;
			if(nb == 8){
				v |= c[1] << 4 & 16 | c[1] >> 3 & 32 | c[1] >> 10 & 64 | c[1] >> 17 & 128;
				c[1] >>= 1;
			}
		}
		c[0] >>= 1;
	}else{
		v = c[0] >> 7 & 1 | c[0] >> 14 & 2;
		if(nb != 2){
			v |= c[0] >> 21 & 4 | c[0] >> 28 & 8;
			if(nb == 8){
				v |= c[1] >> 3 & 16 | c[1] >> 10 & 32 | c[1] >> 17 & 64 | c[1] >> 24 & 128;
				c[1] <<= 1;
			}
		}
		c[0] <<= 1;
	}
	return v;
}

static struct bgctxt {
	u8int sz, szsh, nb, pri[2];
	u16int tx, ty, tnx, tny;
	u16int t;
	u32int c[2];
	int pal;
	u8int msz, mv, mx;
} bgctxts[4];

static void
bginit(int n, int nb, int prilo, int prihi)
{
	struct bgctxt *p;
	int sx, sy;

	p = bgctxts + n;
	p->szsh = (reg[BGMODE] & (1<<(4+n))) != 0 ? 4 : 3;
	p->sz = 1<<p->szsh;
	p->nb = nb;
	p->pri[0] = prilo;
	p->pri[1] = prihi;
	sx = hofs[n];
	sy = vofs[n] + ppuy;
	if(reg[MOSAIC] != 0 && (reg[MOSAIC] & (1<<n)) != 0){
		p->msz = (reg[MOSAIC] >> 4) + 1;
		if(p->msz != 1){
			sx -= p->mx = sx % p->msz;
			sy -= sy % p->msz;
		}
	}else
		p->msz = 1;
redo:
	p->tx = sx >> p->szsh;
	p->tnx = sx & (p->sz - 1);
	p->ty = sy >> p->szsh;
	p->tny = sy & (p->sz - 1);
	p->t = tile(n, p->tx, p->ty);
	chr(n, nb, p->sz, p->t, p->tnx, p->tny, p->c);
	p->pal = palette(n, p->t >> 10 & 7);
	if((p->tnx & 7) != 0)
		shift(p->c, nb, p->tnx & 7, p->t & 0x4000);
	if(p->msz != 1 && p->mx != 0 && sx % p->msz == 0){
		p->mv = bgpixel(p->c, nb, p->t & 0x4000);
		if(p->tnx + p->mx >= 8){
			sx += p->mx;
			goto redo;
		}else if(p->mx > 1)
			shift(p->c, nb, p->mx - 1, p->t & 0x4000);
	}

}

static void
bg(int n)
{
	struct bgctxt *p;
	u8int v;

	p = bgctxts + n;
	v = bgpixel(p->c, p->nb, p->t & 0x4000);
	if(p->msz != 1)
		if(p->mx++ == 0)
			p->mv = v;
		else{
			if(p->mx == p->msz)
				p->mx = 0;
			v = p->mv;
		}
	if(v != 0)
		pixel(n, p->pal + v, p->pri[(p->t & 0x2000) != 0]);
	if(++p->tnx == p->sz){
		p->tx++;
		p->tnx = 0;
		p->t = tile(n, p->tx, p->ty);
		p->pal = palette(n, p->t >> 10 & 7);
	}
	if((p->tnx & 7) == 0)
		chr(n, p->nb, p->sz, p->t, p->tnx, p->tny, p->c);
}

struct bg7ctxt {
	int x, y, x0, y0;
	u8int msz, mx, mv;
} b7[2];

void
calc7(void)
{
	s16int t;

	if((reg[0x2105] & 7) != 7)
		return;
	t = hofs[4] - m7[M7X];
	if((t & 0x2000) != 0)
		t |= ~0x3ff;
	else
		t &= 0x3ff;
	b7->x0 = (t * m7[M7A]) & ~63;
	b7->y0 = (t * m7[M7C]) & ~63;
	t = vofs[4] - m7[M7Y];
	if((t & 0x2000) != 0)
		t |= ~0x3ff;
	else
		t &= 0x3ff;
	b7->x0 += (t * m7[M7B]) & ~63;
	b7->y0 += (t * m7[M7D]) & ~63;
	b7->x0 += m7[M7X] << 8;
	b7->y0 += m7[M7Y] << 8;
}

static void
bg7init(int n)
{
	u8int m, y;
	struct bg7ctxt *p;
	
	p = b7 + n;
	m = reg[M7SEL];
	y = ppuy;
	p->msz = 1;
	if((reg[MOSAIC] & 1) != 0){
		p->msz = (reg[MOSAIC] >> 4) + 1;
		if(p->msz != 1)
			y -= y % p->msz;
	}
	if(n == 1)
		if((reg[MOSAIC] & 2) != 0)
			p->msz = (reg[MOSAIC] >> 4) + 1;
		else
			p->msz = 1;
	if((m & 2) != 0)
		y = 255 - y;
	p->x = b7->x0 + ((m7[M7B] * y) & ~63);
	p->y = b7->y0 + ((m7[M7D] * y) & ~63);
	if((m & 1) != 0){
		p->x += 255 * m7[M7A];
		p->y += 255 * m7[M7C];
	}
}

static void
bg7(int n)
{
	u16int x, y;
	u8int m, v, t;
	struct bg7ctxt *p;

	p = b7 + n;
	m = reg[M7SEL];
	x = p->x >> 8;
	y = p->y >> 8;
	if((m & 0x80) == 0){
		x &= 1023;
		y &= 1023;
	}else if(x > 1023 || y > 1023){
		if((m & 0x40) != 0){
			t = 0;
			goto lookup;
		}
		v = 0;
		goto end;
	}
	t = vram[x >> 2 & 0xfe | y << 5 & 0x7f00];
lookup:
	v = vram[t << 7 | y << 4 & 0x70 | x << 1 & 0x0e | 1];
end:
	if(p->msz != 1){
		if(p->mx == 0)
			p->mv = v;
		else
			v = p->mv;
		if(++p->mx == p->msz)
			p->mx = 0;
	}
	if(n == 1){
		if((v & 0x7f) != 0)
			if((v & 0x80) != 0)
				pixel(1, v & 0x7f, 0x71);
			else
				pixel(1, v, 0x11);
	}else if(v != 0)
		pixel(0, v, 0x40);
	if((m & 1) != 0){
		p->x -= m7[M7A];
		p->y -= m7[M7C];
	}else{
		p->x += m7[M7A];
		p->y += m7[M7C];
	}
}

static void
bgsinit(void)
{
	static int bitch[8];

	switch(mode){
	case 0:
		bginit(0, 2, 0x80, 0xb0);
		bginit(1, 2, 0x71, 0xa1);
		bginit(2, 2, 0x22, 0x52);
		bginit(3, 2, 0x13, 0x43);
		break;
	case 1:
		bginit(0, 4, 0x80, 0xb0);
		bginit(1, 4, 0x71, 0xa1);
		bginit(2, 2, 0x12, (reg[BGMODE] & 8) != 0 ? 0xd2 : 0x42);
		break;
	case 2:
		bginit(0, 4, 0x40, 0xa0);
		bginit(1, 4, 0x11, 0x71);
		break;
	case 3:
		bginit(0, 8, 0x40, 0xa0);
		bginit(1, 4, 0x11, 0x71);
		break;
	case 7:
		bg7init(0);
		if((reg[SETINI] & EXTBG) != 0)
			bg7init(1);
		break;
	default:
		bgctxts[0].sz = bgctxts[1].sz = 0;
		if(bitch[mode]++ == 0)
			print("bg mode %d not implemented\n", mode);
	}
}

static void
bgs(void)
{
	switch(mode){
	case 0:
		bg(0);
		bg(1);
		bg(2);
		bg(3);
		break;
	case 1:
		bg(0);
		bg(1);
		bg(2);
		break;
	case 2:
	case 3:
		bg(0);
		bg(1);
		break;
	case 7:
		bg7(0);
		if((reg[SETINI] & EXTBG) != 0)
			bg7(1);
		break;
	}
}

static void
sprites(void)
{
	static struct {
		short x;
		u8int y, i, c, sx, sy;
		u16int t0, t1;
	} s[32], *sp;
	static struct {
		short x;
		u8int sx, i, c, pal, pri;
		u32int *ch;
	} t[32], *tp;
	static u32int ch[34];
	static u8int *p, q, over;
	static int n, m;
	static int *sz;
	static int szs[] = {
		8, 8, 16, 16, 8, 8, 32, 32,
		8, 8, 64, 64, 16, 16, 32, 32,
		16, 16, 64, 64, 32, 32, 64, 64,
		16, 32, 32, 64, 16, 32, 32, 32
	};
	static u16int base[2];
	u8int dy, v, col, pri0, pri1, prio;
	u16int a;
	u32int w, *cp;
	int i, nt, dx;

	if(rx == 0){
		n = 0;
		over = 1;
		sp = s;
		sz = szs + ((reg[OBSEL] & 0xe0) >> 3);
		base[0] = (reg[OBSEL] & 0x07) << 14;
		base[1] = base[0] + (((reg[OBSEL] & 0x18) + 8) << 10);
	}
	if((rx & 1) == 0){
		p = oam + 2 * rx;
		if(p[1] == 0xf0)
			goto nope;
		q = (oam[512 + (rx >> 3)] >> (rx & 6)) & 3;
		dy = ppuy - p[1];
		sp->sx = sz[q & 2];
		sp->sy = sz[(q & 2) + 1];
		if(dy >= sp->sy)
			goto nope;
		sp->x = p[0];
		if((q & 1) != 0)
			sp->x |= 0xff00;
		if(sp->x < -(short)sp->sx && sp->x != -256)
			goto nope;
		if(n == 32){
			over |= 0x40;
			goto nope;
		}
		sp->i = rx >> 1;
		sp->y = p[1];
		sp->c = p[3];
		sp->t0 = p[2] << 5;
		sp->t1 = base[sp->c & 1];
		sp++;
		n++;
	}
nope:
	if(ppuy != 0){
		col = 0;
		pri0 = 0;
		pri1 = 128;
		if((reg[OAMADDH] & 0x80) != 0)
			prio = oamaddr >> 2;
		else
			prio = 0;
		for(i = 0, tp = t; i < m; i++, tp++){
			dx = rx - tp->x;
			if(dx < 0 || dx >= tp->sx)
				continue;
			w = *tp->ch;
			if((tp->c & 0x40) != 0){
				v = w & 1 | w >> 7 & 2 | w >> 14 & 4 | w >> 21 & 8;
				*tp->ch = w >> 1;
			}else{
				v = w >> 7 & 1 | w >> 14 & 2 | w >> 21 & 4 | w >> 28 & 8;
				*tp->ch = w << 1;
			}
			if((dx & 7) == 7)
				tp->ch++;
			nt = (tp->i - prio) & 0x7f;
			if(v != 0 && nt < pri1){
				col = tp->pal + v;
				pri0 = tp->pri;
				pri1 = nt;
			}
		}
		if(col > 0)
			pixel(OBJ, col, pri0);
	}
	if(rx == 255){
		cp = ch;
		m = n;
		for(sp = s + n - 1, tp = t + n - 1; sp >= s; sp--, tp--){
			tp->x = sp->x;
			tp->sx = 0;
			tp->c = sp->c;
			tp->pal = 0x80 | sp->c << 3 & 0x70;
			tp->pri = 3 * (0x10 + (sp->c & 0x30));
			if((tp->c & 8) != 0)
				tp->pri |= OBJ;
			else
				tp->pri |= OBJNC;
			tp->ch = cp;
			tp->i = sp->i;
			nt = sp->sx >> 3;
			dy = ppuy - sp->y;
			if((sp->c & 0x80) != 0)
				dy = sp->sy - 1 - dy;
			a = sp->t0 | (dy & 7) << 1;
			if(dy >= 8)
				a += (dy & ~7) << 6;
			if((sp->c & 0x40) != 0){
				a += sp->sx * 4;
				for(i = 0; i < nt; i++){
					if(cp < ch + nelem(ch)){
						w  = vram[sp->t1 | (a -= 16) & 0x1fff] << 16;
						w |= vram[sp->t1 | (a + 1) & 0x1fff] << 24;
						w |= vram[sp->t1 | (a -= 16) & 0x1fff] << 0;
						w |= vram[sp->t1 | (a + 1) & 0x1fff] << 8;
						*cp++ = w;
						tp->sx += 8;
					}else
						over |= 0x80;
				}
			}else
				for(i = 0; i < nt; i++){
					if(cp < ch + nelem(ch)){
						w  = vram[sp->t1 | a & 0x1fff];
						w |= vram[sp->t1 | ++a & 0x1fff] << 8;
						w |= vram[sp->t1 | (a += 15) & 0x1fff] << 16;
						w |= vram[sp->t1 | ++a & 0x1fff] << 24;
						*cp++ = w;
						tp->sx += 8;
						a += 15;
					}else
						over |= 0x80;
				}
		}
		reg[0x213e] = over;
	}
}

static u16int
colormath(void)
{
	u16int v, w, r, g, b;
	u8int m, m2, div;
	int cw;
	
	m = reg[CGWSEL];
	m2 = reg[CGADSUB];
	cw = -1;
	switch(m >> 6){
	default: v = 1; break;
	case 1: v = cw = window(COL); break;
	case 2: v = !(cw = window(COL)); break;
	case 3: v = 0; break;
	}
	if(v){
		if((pixelcol[0] & 0x10000) != 0)
			v = pixelcol[0];
		else
			v = cgram[pixelcol[0] & 0xff];
	}
	if((m2 & (1 << (pixelpri[0] & 0xf))) == 0)
		return v;
	switch((m >> 4) & 3){
	case 0: break;
	case 1: if(cw < 0) cw = window(COL); if(!cw) return v; break;
	case 2: if(cw < 0) cw = window(COL); if(cw) return v; break;
	default: return v;
	}
	div = (m2 & 0x40) != 0;
	if((m & 2) != 0){
		if((pixelcol[1] & 0x10000) != 0)
			w = pixelcol[1];
		else
			w = cgram[pixelcol[1] & 0xff];
		div = div && (pixelpri[1] & 0xf) != COL;
	}else
		w = subcolor;
	if((m2 & 0x80) != 0){
		r = (v & 0x7c00) - (w & 0x7c00);
		g = (v & 0x03e0) - (w & 0x03e0);
		b = (v & 0x001f) - (w & 0x001f);
		if(r > 0x7c00) r = 0;
		if(g > 0x03e0) g = 0;
		if(b > 0x001f) b = 0;
		if(div){
			r = (r >> 1) & 0xfc00;
			g = (g >> 1) & 0xffe0;
			b >>= 1;
		}
		return r | g | b;
	}else{
		r = (v & 0x7c00) + (w & 0x7c00);
		g = (v & 0x03e0) + (w & 0x03e0);
		b = (v & 0x001f) + (w & 0x001f);
		if(div){
			r = (r >> 1) & 0xfc00;
			g = (g >> 1) & 0xffe0;
			b >>= 1;
		}
		if(r > 0x7c00) r = 0x7c00;
		if(g > 0x03e0) g = 0x03e0;
		if(b > 0x001f) b = 0x001f;
		return r | g | b;
	}
}

void
ppustep(void)
{
	int yvbl;

	mode = reg[BGMODE] & 7;
	bright = reg[INIDISP] & 0xf;
	yvbl = (reg[SETINI] & OVERSCAN) != 0 ? 0xf0 : 0xe1;

	if(ppux >= XLEFT && ppux <= XRIGHT && ppuy < 0xf0){
		rx = ppux - XLEFT;
		if(ppuy < yvbl && (reg[INIDISP] & 0x80) == 0){
			pixelcol[0] = 0;
			pixelpri[0] = COL;
			pixelcol[1] = 0x10000 | subcolor;
			pixelpri[1] = COL;	
			bgs();
			sprites();
			if(ppuy != 0)
				pixeldraw(rx, ppuy - 1, colormath());
		}else if(ppuy != 0)
			pixeldraw(rx, ppuy - 1, ppuy >= yvbl ? 0x31c8 : 0);
	}

	if(ppux == 0x116 && ppuy <= yvbl)
		hdma |= reg[0x420c];
	if((reg[NMITIMEN] & HCNTIRQ) != 0 && htime == ppux && ((reg[NMITIMEN] & VCNTIRQ) == 0 || vtime == ppuy))
		irq |= IRQPPU;
	if(++ppux >= 340){
		ppux = 0;
		if(++ppuy >= 262){
			ppuy = 0;
			reg[RDNMI] &= ~VBLANK;
			hdma = reg[0x420c]<<8;
			flush();
		}
		if(ppuy < yvbl)
			bgsinit();
		if(ppuy == yvbl){
			reg[RDNMI] |= VBLANK;
			if((reg[NMITIMEN] & VBLANK) != 0)
				nmi = 2;
			if((reg[NMITIMEN] & AUTOJOY) != 0){
				memwrite(0x4016, 1);
				memwrite(0x4016, 0);
				reg[0x4218] = keylatch >> 16;
				reg[0x4219] = keylatch >> 24;
				keylatch = keylatch << 16 | 0xffff;
			}
		}
		if((reg[NMITIMEN] & (HCNTIRQ|VCNTIRQ)) == VCNTIRQ && vtime == ppuy)
			irq |= IRQPPU;
	}
}
