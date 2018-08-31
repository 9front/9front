#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>

typedef struct Inst Inst;
typedef struct Opl Opl;
typedef struct Chan Chan;
typedef struct Trk Trk;
enum{
	Rate = 44100,
	Ninst = 128 + 81-35+1,

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
Inst inst[Ninst];

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

struct Chan{
	Inst *i;
	int v;
	int bend;
	int pan;
};
Chan chan[16];
struct Trk{
	u8int *s;
	u8int *p;
	u8int *e;
	uvlong t;
	int ev;
};
Trk *tr;

double freq[128];
int mfmt, ntrk, div = 1, tempo, opl2, stream;
uvlong T;
Channel *echan;
Biobuf *ib, *ob;

void *
emalloc(ulong n)
{
	void *p;

	p = mallocz(n, 1);
	if(p == nil)
		sysfatal("mallocz: %r");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

Biobuf *
bfdopen(int fd, int mode)
{
	Biobuf *bf;

	bf = Bfdopen(fd, mode);
	if(bf == nil)
		sysfatal("bfdopen: %r");
	Blethal(bf, nil);
	return bf;
}

Biobuf *
bopen(char *file, int mode)
{
	int fd;

	fd = open(file, mode);
	if(fd < 0)
		sysfatal("bopen: %r");
	return bfdopen(fd, mode);
}

void
bread(void *u, int n)
{
	if(Bread(ib, u, n) != n)
		sysfatal("bread: short read");
}

u8int
get8(Trk *x)
{
	u8int v;

	if(x == nil){
		Bread(ib, &v, 1);
		return v;
	}
	if(x->p >= x->e)
		sysfatal("track overflow");
	return *x->p++;
}

u16int
get16(Trk *x)
{
	u16int v;

	v = get8(x) << 8;
	return v | get8(x);
}

u32int
get32(Trk *x)
{
	u32int v;

	v = get16(x) << 16;
	return v | get16(x);
}

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
	Bwrite(ob, u, p-u);
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
	f = (e * (1 << 20)) / 49716;
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
	w = o->v * o->c->v / 127;
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

uvlong
tc(int n)
{
	return ((uvlong)n * tempo * Rate / div) / 1000000;
}

void
skip(Trk *x, int n)
{
	while(n-- > 0)
		get8(x);
}

int
getvar(Trk *x)
{
	int v, w;

	w = get8(x);
	v = w & 0x7f;
	while(w & 0x80){
		if(v & 0xff000000)
			sysfatal("invalid variable-length number");
		v <<= 7;
		w = get8(x);
		v |= w & 0x7f;
	}
	return v;
}

int
peekvar(Trk *x)
{
	int v;
	uchar *p;

	p = x->p;
	v = getvar(x);
	x->p = p;
	return v;
}

void
samp(uvlong t´)
{
	int dt;
	static uvlong t;

	dt = t´ - t;
	t += dt;
	while(dt > 0){
		putcmd(0, 0, dt > 0xffff ? 0xffff : dt);
		dt -= 0xffff;
	}
}

void
ev(Trk *x)
{
	int e, n, m;
	Chan *c;

	samp(x->t += tc(getvar(x)));
	e = get8(x);
	if((e & 0x80) == 0){
		x->p--;
		e = x->ev;
		if((e & 0x80) == 0)
			sysfatal("invalid event");
	}else
		x->ev = e;
	c = chan + (e & 15);
	n = get8(x);
	switch(e >> 4){
	case 0x8: noteoff(c, n, get8(x)); break;
	case 0x9: noteon(c, n, get8(x), x->t); break;
	case 0xb:
		m = get8(x);
		switch(n){
		case 0x00: case 0x01: case 0x20: break;
		case 0x07: c->v = m; resetchan(c); break;
		case 0x0a: c->pan = m < 32 ? 1<<4 : m > 96 ? 1<<5 : 3<<4; resetchan(c); break;
		default: fprint(2, "unknown controller %d\n", n);
		}
		break;
	case 0xc: c->i = inst + n; break;
	case 0xe:
		n = get8(x) << 7 | n;
		c->bend = n - 0x4000 / 2;
		resetchan(c);
		break;
	case 0xf:
		if((e & 0xf) == 0){
			while(get8(x) != 0xf7)
				;
			return;
		}
		m = get8(x);
		switch(n){
		case 0x2f: x->p = x->e; return;
		case 0x51: tempo = get16(x) << 8; tempo |= get8(x); break;
		default: skip(x, m);
		}
		break;
	case 0xa:
	case 0xd: get8(x); break;
	default: sysfatal("invalid event %#ux\n", e >> 4);
	}
}

void
tproc(void *)
{
	vlong t, Δt;
	uchar u[4];
	Trk x;

	x.e = u + sizeof u;
	t = nsec();
	for(;;){
		if(nbrecv(echan, u) > 0){
			u[0] = 0;
			x.p = u;
			ev(&x);
		}
		putcmd(0, 0, 1);
		t += 10000000 / (Rate / 100);
		Δt = (t - nsec()) / 1000000;
		if(Δt > 0)
			sleep(Δt);
	}
}

void
readinst(char *file)
{
	int n;
	uchar u[8];
	Inst *i;

	ib = bopen(file, OREAD);
	bread(u, sizeof u);
	if(memcmp(u, "#OPL_II#", sizeof u) != 0)
		sysfatal("invalid patch file");
	for(i=inst; i<inst+nelem(inst); i++){
		n = get8(nil);
		i->fixed = n & 1<<0;
		i->dbl = opl2 ? 0 : n & 1<<2;
		get8(nil);
		i->fine = (get8(nil) - 128) * 64;
		i->n = get8(nil);
		bread(i->i, sizeof i->i);
		get8(nil);
		n = get8(nil);
		n |= get8(nil) << 8;
		i->base[0] = (s16int)n;
		bread(i->i2, sizeof i->i2);
		get8(nil);
		n = get8(nil);
		n |= get8(nil) << 8;
		i->base[1] = (s16int)n;
	}
	Bterm(ib);
}

void
readmid(char *file)
{
	u32int n;
	uchar *s;
	Trk *x;

	ib = file != nil ? bopen(file, OREAD) : bfdopen(0, OREAD);
	if(stream)
		return;
	if(get32(nil) != 0x4d546864 || get32(nil) != 6)
		sysfatal("invalid header");
	mfmt = get16(nil);
	ntrk = get16(nil);
	if(ntrk == 1)
		mfmt = 0;
	if(mfmt < 0 || mfmt > 1)
		sysfatal("unsupported format %d", mfmt);
	div = get16(nil);
	tr = emalloc(ntrk * sizeof *tr);
	for(x=tr; x<tr+ntrk; x++){
		if(get32(nil) != 0x4d54726b)
			sysfatal("invalid track");
		n = get32(nil);
		s = emalloc(n);
		bread(s, n);
		x->s = s;
		x->p = s;
		x->e = s + n;
	}
	Bterm(ib);
}

void
usage(void)
{
	fprint(2, "usage: %s [-2s] [-i inst] [mid]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	int n, t, mint;
	char *i;
	double f;
	uchar u[4];
	Chan *c;
	Opl *o;
	Trk *x, *minx;

	i = "/mnt/wad/genmidi";
	ARGBEGIN{
	case '2': opl2 = 1; ople = opl + 9; break;
	case 'i': i = EARGF(usage()); break;
	case 's': stream = 1; break;
	default: usage();
	}ARGEND
	readinst(i);
	readmid(*argv);
	ob = bfdopen(1, OWRITE);
	f = pow(2, 1./12);
	for(n=0; n<nelem(freq); n++)
		freq[n] = 440 * pow(f, n - 69);
	for(c=chan; c<chan+nelem(chan); c++){
		c->v = 0x5a;
		c->bend = 0;
		c->pan = 3<<4;
		c->i = inst;
	}
	for(o=opl; o<ople; o++)
		o->n = -1;
	tempo = 500000;
	putcmd(Rwse, Mwse, 0);
	putcmd(Rop3, 1, 0);
	if(stream){
		if(proccreate(tproc, nil, mainstacksize) < 0)
			sysfatal("proccreate: %r");
		if((echan = chancreate(sizeof u, 0)) == nil)
			sysfatal("chancreate: %r");
		for(;;){
			if((n = Bread(ib, u, sizeof u)) != sizeof u)
				break;
			send(echan, u);
		}
		threadexitsall(n < 0 ? "read: %r" : nil);
	}
	for(;;){
		minx = nil;
		mint = 0;
		for(x=tr; x<tr+ntrk; x++){
			if(x->p >= x->e)
				continue;
			t = x->t + tc(peekvar(x));
			if(t < mint || minx == nil){
				mint = t;
				minx = x;
			}
		}
		if(minx == nil)
			exits(nil);
		ev(minx);
	}
}
