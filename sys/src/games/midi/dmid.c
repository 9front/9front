#include <u.h>
#include <libc.h>
#include <bio.h>
#include "midifile.h"

typedef struct Opl Opl;
enum{
	Rwse = 0x01,
		Mwse = 1<<5,	/* wave selection enable */
	Rctl = 0x20,
	Rsca = 0x40,
		Mlvl = 63<<0,	/* total level */
		Mscl = 3<<6,	/* scaling level */
	Ratk = 0x60,
	Rsus = 0x80,
	Rnum = 0xa0,		/* f number lsb */
	Roct = 0xb0,
		Mmsb = 3<<0,	/* f number msb */
		Moct = 7<<2,
		Mkon = 1<<5,
	Rfed = 0xc0,
	Rwav = 0xe0,
	Rop3 = 0x105,
};

struct Inst{
	int fixed;
	int dbl;
	int fine;
	uchar n;
	uchar i[13];
	uchar i2[13];
	s16int base[2];
};

struct Opl{
	Chan *c;
	int n;
	int midn;
	int blk;
	int v;
	vlong t;
	uchar *i;
};
Opl opl[18], *ople = opl + nelem(opl);
int port[] = {
	0x0, 0x1, 0x2, 0x8, 0x9, 0xa, 0x10, 0x11, 0x12,
	0x100, 0x101, 0x102, 0x108, 0x109, 0x10a, 0x110, 0x111, 0x112
};
int sport[] = {
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8,
	0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108
};
uchar ovol[] = {
	0, 32, 48, 58, 64, 70, 74, 77, 80, 83, 86, 88, 90, 92, 93, 95, 96,
	98, 99, 100, 102, 103, 104, 105, 106, 107, 108, 108, 109, 110, 111,
	112, 112, 113, 114, 114, 115, 116, 116, 117, 118, 118, 119, 119,
	120, 120, 121, 121, 122, 122, 123, 123, 124, 124, 124, 125, 125,
	126, 126, 126, 127, 127, 127, 128, 128
};

double freq[128];
int opl2;

void
putcmd(u16int r, u8int v, u16int dt)
{
	uchar *p, u[5];

	p = u;
	*p++ = r;
	if(!opl2)
		*p++ = r >> 8;
	*p++ = v;
	*p++ = dt;
	*p++ = dt >> 8;
	write(1, u, p-u);
}

void
setinst(Opl *o, uchar *i)
{
	int p;

	p = sport[o - opl];
	putcmd(Roct+p, o->blk, 0);
	putcmd(Rfed+p, i[6] & ~0x30 | o->c->pan, 0);
	p = port[o - opl];
	putcmd(Rctl+p, i[0], 0);
	putcmd(Ratk+p, i[1], 0);
	putcmd(Rsus+p, i[2], 0);
	putcmd(Rwav+p, i[3] & 3, 0);
	putcmd(Rctl+3+p, i[7], 0);
	putcmd(Ratk+3+p, i[8], 0);
	putcmd(Rsus+3+p, i[9], 0);
	putcmd(Rwav+3+p, i[10] & 3, 0);
	o->i = i;
}

void
noteoff(Chan *c, int n, int)
{
	Opl *o;

	for(o=opl; o<ople; o++)
		if(o->c == c && o->midn == n){
			putcmd(Roct+sport[o-opl], o->blk, 0);
			o->n = -1;
		}
}

Opl *
getch(void)
{
	Opl *o, *p;

	p = opl;
	for(o=opl; o<ople; o++){
		if(o->n < 0)
			return o;
		if(o->t < p->t)
			p = o;
	}
	return p;
}

void
setoct(Opl *o)
{
	int n, b, f, d;
	double e;

	d = o->c->bend;
	d += o->i == o->c->i->i2 ? o->c->i->fine : 0;
	n = o->n + d / 0x1000 & 0x7f;
	e = freq[n] + (d % 0x1000) * (freq[n+1] - freq[n]) / 0x1000;
	if(o->c->i->fixed)
		e = (double)(int)e;
	f = (e * (1 << 20)) / samprate;
	for(b=1; b<8; b++, f>>=1)
		if(f < 1024)
			break;
	o->blk = b << 2 & Moct | f >> 8 & Mmsb;
	putcmd(Rnum+sport[o-opl], f & 0xff, 0);
	putcmd(Roct+sport[o-opl], Mkon | o->blk, 0);
}

void
setvol(Opl *o)
{
	int p, w, x;

	p = port[o - opl];
	w = o->v * o->c->vol / 127;
	w = ovol[w * 64 / 127] * 63 / 128;
	x = 63 + (o->i[5] & Mlvl) * w / 63 - w;
	putcmd(Rsca+p, o->i[4] & Mscl | x, 0);
	x = 63 + (o->i[12] & Mlvl) * w / 63 - w;
	putcmd(Rsca+p+3, o->i[11] & Mscl | x, 0);
}

void
putnote(Chan *c, int midn, int n, int v, vlong t, uchar *i)
{
	Opl *o;

	o = getch();
	o->c = c;
	o->n = n;
	o->midn = midn;
	o->v = v;
	o->t = t;
	if(o->i != i)
		setinst(o, i);
	setvol(o);
	setoct(o);
}

void
noteon(Chan *c, int n, int v, vlong t)
{
	int x, m;

	m = n;
	if(c - chan == 9){
		/* asspull workaround for percussions above gm set */
		if(m == 85)
			m = 37;
		if(m == 82)
			m = 44;
		if(m < 35 || m > 81)
			return;
		c->i = inst + 128 + m - 35;
	}
	if(c->i->fixed)
		m = c->i->n;
	if(v == 0){
		noteoff(c, n, 0);
		return;
	}
	x = m + (c->i->fixed ? 0 : c->i->base[0]);
	while(x < 0)
		x += 12;
	while(x > 8*12-1)
		x -= 12;
	putnote(c, n, x & 0xff, v, t, c->i->i);
	if(c->i->dbl){
		x = m + (c->i->fixed ? 0 : c->i->base[1]);
		while(x < 0)
			x += 12;
		while(x > 95)
			x -= 12;
		putnote(c, n, x & 0xff, v, t, c->i->i2);
	}
}

void
resetchan(Chan *c)
{
	Opl *o;

	for(o=opl; o<ople; o++)
		if(o->c == c && o->n >= 0){
			putcmd(Rfed+sport[o-opl], o->i[6] & ~0x30 | c->pan, 0);
			setvol(o);
			setoct(o);
		}
}

void
samp(double n)
{
	vlong t;
	double Δ;
	static double ε;

	Δ = delay(n) + ε;
	t = Δ;
	ε = Δ - t;
	while(t > 0){
		putcmd(0, 0, t > 0xffff ? 0xffff : t);
		t -= 0xffff;
	}
}

void
event(Track *t)
{
	int e;
	Msg msg;
	Chan *c;

	e = nextev(t);
	translate(t, e, &msg);
	c = msg.chan;
	switch(msg.type){
	case Cnoteoff: noteoff(msg.chan, msg.arg1, msg.arg2); break;
	case Cnoteon: noteon(msg.chan, msg.arg1, msg.arg2, t->t); break;
	case Cbankmsb: if(msg.arg2 < Ninst) c->i = inst + msg.arg2; break;
	case Cprogram: if(msg.arg2 < Ninst) c->i = inst + msg.arg1; break;
	case Cchanvol: /* wet floor */
	case Cpan:  /* wet floor */
	case Cpitchbend: resetchan(c); break;
	}
}

void
readinst(char *file)
{
	int n;
	uchar u[8];
	Inst *i;
	Chan *c;

	inbf = eopen(file, OREAD);
	Bread(inbf, u, sizeof u);
	if(memcmp(u, "#OPL_II#", sizeof u) != 0)
		sysfatal("invalid patch file");
	inst = emalloc(Ninst * sizeof *inst);
	for(i=inst; i<inst+Ninst; i++){
		n = get8(nil);
		i->fixed = n & 1<<0;
		i->dbl = opl2 ? 0 : n & 1<<2;
		get8(nil);
		i->fine = (get8(nil) - 128) * 64;
		i->n = get8(nil);
		Bread(inbf, i->i, sizeof i->i);
		get8(nil);
		n = get8(nil);
		n |= get8(nil) << 8;
		i->base[0] = (s16int)n;
		Bread(inbf, i->i2, sizeof i->i2);
		get8(nil);
		n = get8(nil);
		n |= get8(nil) << 8;
		i->base[1] = (s16int)n;
	}
	Bterm(inbf);
	inbf = nil;
	for(c=chan; c<chan+nelem(chan); c++)
		c->i = inst;
}

void
usage(void)
{
	fprint(2, "usage: %s [-2Ds] [-i inst] [mid]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int n;
	char *i;
	double f;
	Opl *o;

	samprate = 49716,		/* opl3 sampling rate */
	i = "/mnt/wad/genmidi";
	ARGBEGIN{
	case '2': opl2 = 1; ople = opl + 9; break;
	case 'D': trace = 1; break;
	case 'i': i = EARGF(usage()); break;
	case 's': stream = 1; break;
	default: usage();
	}ARGEND
	initmid();
	readinst(i);
	if(readmid(*argv) < 0)
		sysfatal("readmid: %r");
	f = pow(2, 1./12);
	for(n=0; n<nelem(freq); n++)
		freq[n] = 440 * pow(f, n - 69);
	for(o=opl; o<ople; o++)
		o->n = -1;
	putcmd(Rwse, Mwse, 0);
	putcmd(Rop3, 1, 0);
	evloop();
	exits(nil);
}
