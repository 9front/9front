#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/audioif.h"

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

	Recsel = 0x1A,
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
	Vspeed,
	Vdelay,
};

static Volume voltab[] = {
	[Vmaster] "master", 0x02, -63, Stereo, 0,
	[Vaudio] "audio", 0x18, -31, Stereo, 0,
	[Vhead] "head", 0x04, -31, Stereo, Capheadphones,
	[Vbass] "bass", 0x08, 15, Left, Captonectl,
	[Vtreb] "treb", 0x08, 15, Right, Captonectl,
	[Vbeep] "beep", 0x0a, -31, Right, 0,
	[Vphone] "phone", 0x0c, -31, Right, 0,
	[Vmic] "mic", 0x0e, -31, Right, Capmic,
	[Vline] "line", 0x10, -31, Stereo, 0,
	[Vcd] "cd", 0x12, -31, Stereo,	0,
	[Vvideo] "video", 0x14, -31, Stereo, 0,
	[Vaux] "aux", 0x16, -63, Stereo, 0,
	[Vrecgain] "recgain", 0x1c, 15, Stereo, 0,
	[Vmicgain] "micgain", 0x1e, 15, Right, Capmic,
	[Vspeed] "speed", 0x2c, 0, Absolute, 0,
	[Vdelay] "delay", 0, 0, Absolute, 0,
	0
};

typedef struct Mixer Mixer;
struct Mixer
{
	ushort (*rr)(Audio *, int);
	void (*wr)(Audio *, int, ushort);
	int vra;
};

static int
ac97volget(Audio *adev, int x, int a[2])
{
	Mixer *m = adev->mixer;
	Volume *vol;
	ushort v;

	vol = voltab+x;
	switch(vol->type){
	case Absolute:
		if(x == Vdelay){
			a[0] = adev->delay;
			break;
		}
		a[0] = m->rr(adev, vol->reg);
		break;
	default:
		v = m->rr(adev, vol->reg);
		if(v & 0x8000){
			a[0] = a[1] = vol->range < 0 ? 0x7f : 0;
		} else {
			a[0] = ((v>>8) & 0x7f);
			a[1] = (v & 0x7f);
		}
	}
	return 0;
}

static int
ac97volset(Audio *adev, int x, int a[2])
{
	Mixer *m = adev->mixer;
	Volume *vol;
	ushort v, w;

	vol = voltab+x;
	switch(vol->type){
	case Absolute:
		if(x == Vdelay){
			adev->delay = a[0];
			return 0;
		}
		m->wr(adev, vol->reg, a[0]);		
		if(x == Vspeed){
			m->wr(adev, 0x32, a[0]);	/* adc rate */
			adev->speed = m->rr(adev, vol->reg);
		}
		break;
	case Left:
		v = a[0] & 0x7f;
		w = m->rr(adev, vol->reg) & 0x7f;
		m->wr(adev, vol->reg, (v<<8)|w);
		break;
	case Right:
		v = m->rr(adev, vol->reg) & 0x7f00;
		w = a[1] & 0x7f;
		m->wr(adev, vol->reg, v|w);
		break;
	case Stereo:
		v = a[0] & 0x7f;
		w = a[1] & 0x7f;
		m->wr(adev, vol->reg, (v<<8)|w);
		break;
	}
	return 0;
}


static long
ac97mixread(Audio *adev, void *a, long n, vlong)
{
	Mixer *m = adev->mixer;
	ulong caps;

	caps = m->rr(adev, Reset);
	caps |= m->rr(adev, Extid) << 16;
	return genaudiovolread(adev, a, n, 0, voltab, ac97volget, caps);
}

static long
ac97mixwrite(Audio *adev, void *a, long n, vlong)
{
	Mixer *m = adev->mixer;
	ulong caps;

	caps = m->rr(adev, Reset);
	caps |= m->rr(adev, Extid) << 16;
	return genaudiovolwrite(adev, a, n, 0, voltab, ac97volset, caps);
}

void
ac97mixreset(Audio *adev, void (*wr)(Audio*,int,ushort), ushort (*rr)(Audio*,int))
{
	Mixer *m;
	ushort t;

	m = malloc(sizeof(Mixer));
	if(m == nil){
		print("ac97mix: no memory for Mixer\n");
		return;
	}
	m->wr = wr;
	m->rr = rr;
	m->wr(adev, Reset, 0);
	m->wr(adev, Powerdowncsr, 0);
	delay(1000);
	t = (Adcpower | Dacpower | Anlpower | Refpower);
	if((m->rr(adev, Powerdowncsr) & t) != t)
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

	adev->mixer = m;
	adev->volread = ac97mixread;
	adev->volwrite = ac97mixwrite;
}
