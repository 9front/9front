#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/audio.h"

typedef ushort (*ac97rdfn)(Audio *, int);
typedef void (*ac97wrfn)(Audio *, int, ushort);

typedef struct Mixer Mixer;
typedef struct Volume Volume;

struct Mixer {
	QLock;
	ac97wrfn wr;
	ac97rdfn rr;
	int vra;
};

enum { Maxbusywait = 500000 };

enum {
	Reset = 0x0,
		Capmic = 0x1,
		Captonectl = 0x4,
		Capsimstereo = 0x8,
		Capheadphones = 0x10,
		Caploudness = 0x20,
		Capdac18 = 0x40,
		Capdac20 = 0x80,
		Capadc18 = 0x100,
		Capadc20 = 0x200,
		Capenh = 0xfc00,
	Master = 0x02,
	Headphone = 0x04,
	Monomaster = 0x06,
	Mastertone = 0x08,
	Pcbeep = 0x0A,
	Phone = 0x0C,
	Mic = 0x0E,
	Line = 0x10,
	Cd = 0x12,
	Video = 0x14,
	Aux = 0x16,
	Pcmout = 0x18,
		Mute = 0x8000,
	Recsel = 0x1A,
	Recgain = 0x1C,
	Micgain = 0x1E,
	General = 0x20,
	ThreeDctl = 0x22,
	Ac97RESER = 0x24,
	Powerdowncsr = 0x26,
		Adcpower = 0x1,
		Dacpower = 0x2,
		Anlpower = 0x4,
		Refpower = 0x8,
		Inpower = 0x100,
		Outpower = 0x200,
		Mixpower = 0x400,
		Mixvrefpower = 0x800,
		Aclinkpower = 0x1000,
		Clkpower = 0x2000,
		Auxpower = 0x4000,
		Eamppower = 0x8000,
	Extid = 0x28,
	Extcsr = 0x2A,
		Extvra = 1<<0,
		Extdra = 1<<1,
		Extspdif = 1<<2,
		Extvrm = 1<<3,
		Extiddsa0 = 0<<4,	/* extid only */
		Extiddsa1 = 1<<4,	/* extid only */
		Extiddsa2 = 2<<4,	/* extid only */
		Extiddsa3 = 3<<4,	/* extid only */
		Extcsrspsa34 = 0<<4,	/* extcsr only */
		Extcsrspsa78 = 1<<4,	/* extcsr only */
		Extcsrspsa69 = 2<<4,	/* extcsr only */
		ExtcsrspsaAB = 3<<4,	/* extcsr only */
		Extcdac = 1<<6,
		Extsdac = 1<<7,
		Extldac = 1<<8,
		Extidamap = 1<<9,	/* extid only */
		Extidrev11 = 0<<10,	/* extid only */
		Extidrev22 = 1<<10,	/* extid only */
		Extidrev23 = 2<<10,	/* extid only */
		Extidprim = 0<<14,	/* extid only */
		Extidsec0 = 1<<14,	/* extid only */
		Extidsec1 = 2<<14,	/* extid only */
		Extidsec2 = 3<<14,	/* extid only */
		Extcsrmadc = 1<<9,	/* extcsr only */
		Extcsrspcv = 1<<10,	/* extcsr only */
		Extcsrpri = 1<<11,	/* extcsr only */
		Extcsrprj = 1<<12,	/* extcsr only */
		Extcsrprk = 1<<13,	/* extcsr only */
		Extcsrprl = 1<<14,	/* extcsr only */
		Extcsrvcfg = 1<<15,	/* extcsr only */
	Pcmfrontdacrate = 0x2C,
	Pcmsurrounddacrate = 0x2E,
	Pcmlfedacrate = 0x30,
	Pcmadcrate = 0x32,
	Pcmmicadcrate = 0x34,
	CenterLfe = 0x36,
	LrSurround = 0x38,
	Spdifcsr = 0x3a,
		Spdifpro = 1<<0,
		Spdifnonaudio = 1<<1,
		Spdifcopy = 1<<2,
		Spdifpre = 1<<3,
		SpdifCC = 0x7f<<4,
		Spdifl = 1<<11,
		Spdif44k1 = 0<<12,
		Spdif32k = 1<<12,
		Spdif48k = 2<<12,
		Spdifdsr = 1<<14,
		Spdifv = 1<<15,
	VID1 = 0x7c,
	VID2 = 0x7e,
	Speed = 0x1234567,
};

enum {
	Left,
	Right,
	Stereo,
	Absolute,
};

enum {
	Vmaster,
	Vhead,
	Vaudio,
	Vcd,
	Vbass,
	Vtreb,
	Vbeep,
	Vphone,
	Vmic,
	Vline,
	Vvideo,
	Vaux,
	Vrecgain,
	Vmicgain,
};

struct Volume {
	int reg;
	int range;
	int type;
	int cap;
	char *name;
};

struct Topology {
	Volume *this;
	Volume *next[2];
};

Volume vol[] = {
[Vmaster]	{Master, 63, Stereo, 0, "master"},
[Vaudio]	{Pcmout, 31, Stereo,	0, "audio"},
[Vhead]	{Headphone, 31, Stereo, Capheadphones, "head"},
[Vbass]	{Mastertone, 15, Left, Captonectl, "bass"},
[Vtreb]	{Mastertone, 15, Right, Captonectl, "treb"},
[Vbeep]	{Pcbeep, 31, Right, 0, "beep"},
[Vphone]	{Phone, 31, Right, 0, "phone"},
[Vmic]	{Mic, 31, Right, Capmic, "mic"},
[Vline]	{Line, 31, Stereo, 0, "line"},
[Vcd]	{Cd, 31, Stereo,	0, "cd"},
[Vvideo]	{Video, 31, Stereo, 0, "video"},
[Vaux]	{Aux, 63, Stereo, 0, "aux"},
[Vrecgain]	{Recgain, 15, Stereo, 0, "recgain"},
[Vmicgain]	{Micgain, 15, Right, Capmic, "micgain"},
	{0, 0, 0, 0, 0},
};

long
ac97mixtopology(Audio *adev, void *a, long n, vlong off)
{
	Mixer *m;
	char *buf;
	long l;
	ulong caps;
	m = adev->mixer;
	qlock(m);
	caps = m->rr(adev, Reset);
	caps |= m->rr(adev, Extid) << 16;
	l = 0;
	buf = malloc(READSTR);
	l += snprint(buf+l, READSTR-l, "not implemented. have fun.\n");
	USED(caps);
	USED(l);
	qunlock(m);
	n = readstr(off, a, n, buf);
	free(buf);
	return n;
}

long
ac97mixread(Audio *adev, void *a, long n, vlong off)
{
	Mixer *m;
	char *nam, *buf;
	long l;
	ushort v;
	ulong caps;
	int i, rang, le, ri;
	buf = malloc(READSTR);
	m = adev->mixer;
	qlock(m);
	l = 0;
	caps = m->rr(adev, Reset);
	caps |= m->rr(adev, Extid) << 16;
	for(i = 0; vol[i].name != 0; ++i){
		if(vol[i].cap && ((vol[i].cap & caps) == 0))
			continue;
		v = m->rr(adev, vol[i].reg);
		nam = vol[i].name;
		rang = vol[i].range;
		if(vol[i].type == Absolute){
			l += snprint(buf+l, READSTR-l, "%s %d", nam, v);
		} else {
			ri = ((rang-(v&rang)) * 100) / rang;
			le = ((rang-((v>>8)&rang)) * 100) / rang;
			if(vol[i].type == Stereo)
				l += snprint(buf+l, READSTR-l, "%s %d %d", nam, le, ri);
			if(vol[i].type == Left)
				l += snprint(buf+l, READSTR-l, "%s %d", nam, le);
			if(vol[i].type == Right)
				l += snprint(buf+l, READSTR-l, "%s %d", nam, ri);
			if(v&Mute)
				l += snprint(buf+l, READSTR-l, " mute");
		}
		l += snprint(buf+l, READSTR-l, "\n");
	}
	qunlock(m);
	n = readstr(off, a, n, buf);
	free(buf);
	return n;
}

long
ac97mixwrite(Audio *adev, void *a, long n, vlong)
{
	Mixer *m;
	char *tok[4];
	int ntok, i, left, right, rang, reg;
	ushort v;
	m = adev->mixer;
	qlock(m);
	ntok = tokenize(a, tok, 4);
	for(i = 0; vol[i].name != 0; ++i){
		if(!strcmp(vol[i].name, tok[0])){
			rang = vol[i].range;
			reg = vol[i].reg;
			left = right = 0;
			if(ntok > 1)
				left = right = atoi(tok[1]);
			if(ntok > 2)
				right = atoi(tok[2]);

			if(vol[i].type == Absolute){
				m->wr(adev, reg, left);
			} else {
				left = rang - ((left*rang)) / 100;
				right = rang - ((right*rang)) / 100;
				switch(vol[i].type){
				default:
					break;
				case Left:
					v = m->rr(adev, reg);
					v = (v & 0x007f) | (left << 8);
					m->wr(adev, reg, v);
					break;
				case Right:
					v = m->rr(adev, reg);
					v = (v & 0x7f00) | right;
					m->wr(adev, reg, v);
					break;
				case Stereo:
					v = (left<<8) | right;
					m->wr(adev, reg, v);
					break;
				}
			}
			qunlock(m);
			return n;
		}
	}
	if(vol[i].name == nil){
		char *p;
		for(p = tok[0]; *p; ++p)
			if(*p < '0' || *p > '9') {
				qunlock(m);
				error("no such volume setting");
			}
		rang = vol[0].range;
		reg = vol[0].reg;
		left = right = rang - ((atoi(tok[0])*rang)) / 100;
		v = (left<<8) | right;
		m->wr(adev, reg, v);
	}
	qunlock(m);

	return n;
}

int
ac97hardrate(Audio *adev, int rate)
{
	Mixer *m;
	int oldrate;
	m = adev->mixer;
	oldrate = m->rr(adev, Pcmfrontdacrate);
	if(rate > 0)
		m->wr(adev, Pcmfrontdacrate, rate);
	return oldrate;
}

void
ac97mixreset(Audio *adev, ac97wrfn wr, ac97rdfn rr)
{
	Mixer *m;
	int i;
	ushort t;
	if(adev->mixer == nil)
		adev->mixer = malloc(sizeof(Mixer));
	m = adev->mixer;
	m->wr = wr;
	m->rr = rr;
	adev->volread = ac97mixread;
	adev->volwrite = ac97mixwrite;
	m->wr(adev, Reset, 0);
	m->wr(adev, Powerdowncsr, 0);

	t = (Adcpower | Dacpower | Anlpower | Refpower);
	for(i = 0; i < Maxbusywait; i++){
		if((m->rr(adev, Powerdowncsr) & t) == t)
			break;
		microdelay(1);
	}
	if(i == Maxbusywait)
		print("#A%d: ac97 exhausted waiting powerup\n", adev->ctlrno);

	t = m->rr(adev, Extid);
	print("#A%d: ac97 codec ext:%s%s%s%s%s%s%s\n", adev->ctlrno,
		(t & Extvra) ? " vra" : "",
		(t & Extdra) ? " dra" : "",
		(t & Extspdif) ? " spdif" : "",
		(t & Extvrm) ? " vrm" : "",
		(t & Extcdac) ? " cdac" : "",
		(t & Extsdac) ? " sdac" : "",
		(t & Extldac) ? " ldac" : "");

	if(t & Extvra){
		m->wr(adev, Extcsr, Extvra);
		m->vra = 1;
	} else {
		print("#A%d: ac97 vra extension not supported\n", adev->ctlrno);
		m->vra = 0;
	}
}
