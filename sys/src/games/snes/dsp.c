#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int dsp[256], dspstate;
u16int dspcounter, noise = 0x8000;
static s16int samp[2], echoin[2], echobuf[2*2*8];
static u16int echoaddr;
static int echobp, echopos;

enum {
	VOLL = 0,
	VOLR = 1,
	PITCHL = 2,
	PITCHH = 3,
	SRCN = 4,
	ADSR1 = 5,
	ADSR2 = 6,
	GAIN = 7,
	ENVX = 8,
	OUTX = 9,
	MVOLL = 0x0c,
	EFB = 0x0d,
	MVOLR = 0x1c,
	EVOLL = 0x2c,
	PMON = 0x2d,
	EVOLR = 0x3c,
	NON = 0x3d,
	EON = 0x4d,
	KON = 0x4c,
	KOFF = 0x5c,
	DIR = 0x5d,
	FLG = 0x6c,
	ESA = 0x6d,
	ENDX = 0x7c,
	EDL = 0x7d,
	NEWKON = 0x8e,
	INT = 0x80,
};

enum {
	READY = 6,
};

enum { RELEASE, ATTACK, DECAY, SUSTAIN };

enum { Freq = 44100 };
static s16int sbuf[2*2000], *sbufp;
static int fd;

void
audioinit(void)
{
	fd = open("/dev/audio", OWRITE);
	if(fd < 0)
		sysfatal("open: %r");
	sbufp = sbuf;
}

static int
hermite(int *x, int t)
{
	int y;

	y = (x[0] - x[6]) / 2 + (x[4] - x[2]) * 3 / 2;
	y = y * t >> 15;
	y += x[6] - x[4] * 5 / 2 + x[2] * 2 - x[0] / 2;
	y = y * t >> 15;
	y += (x[2] - x[6]) / 2;
	y = y * t >> 15;
	y += x[4];
	return y;
}

static void
audiosample(s16int *s)
{
	static int x[8], t;

	x[0] = s[0];
	x[1] = s[1];
	do {
		sbufp[0] = hermite(x, t);
		sbufp[1] = hermite(x + 1, t);
		sbufp += 2;
		t += (32000<<15)/Freq;
	} while(t < 1<<15);
	t -= 1<<15;
	memmove(x + 2, x, sizeof(x) - 2 * sizeof(x[0]));
}

int
audioout(void)
{
	int rc;

	if(sbufp == nil)
		return -1;
	if(sbufp == sbuf)
		return 0;
	rc = warp10 ? (sbufp - sbuf) * 2 : write(fd, sbuf, (sbufp - sbuf) * 2);
	if(rc > 0)
		sbufp -= (rc+1)/2;
	if(sbufp < sbuf)
		sbufp = sbuf;
	return 0;
}

static u16int
spc16(u16int p)
{
	u16int v;
	
	v = spcmem[p++];
	v |= spcmem[p] << 8;
	return v;
}

static void
spcput16(u16int p, u16int v)
{
	spcmem[p++] = v;
	spcmem[p] = v >> 8;
}

u8int
dspread(u8int p)
{
	p &= 0x7f;
	return dsp[p];
}

void
dspwrite(u8int p, u8int v)
{
	if(p >= 0x80)
		return;
	switch(p){
	case KON:
		dsp[NEWKON] = v;
		break;
	case ENDX:
		v = 0;
		break;
	}
	dsp[p] = v;
}

int
envyes(int r)
{
	static u16int modulus[] = {
		0, 2048, 1536, 1280, 1024, 768, 640, 512, 384,
		320, 256, 192, 160, 128, 96, 80, 64, 48,
		40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3,
	};
	u16int c;
	
	if(r == 0)
		return 0;
	if(r >= 30){
		if(r == 31)
			return 1;
		return (dspcounter & 1) == 0;
	}
	c = dspcounter;
	switch(r % 3){
	case 0: c += 536; break;
	case 2: c += 1040; break;
	}
	return c % modulus[r] == 0;
}

static s16int
clamp16(int v)
{
	if(v < -32768)
		return -32768;
	if(v > 32767)
		return 32767;
	return v;
}

typedef struct {
	int n;
	u8int *r;
	
	u16int hdrp, dp, sp;
	u8int hdr, bp;
	u16int brr;

	u8int envst;
	u16int env, envbent;
	
	u8int init;
	u16int interp;
	s16int buf[24];
	
	u16int pitch;
	s16int sample, modin;
} vctxt;
vctxt vctxts[8];

static void
env(vctxt *p)
{
	u8int *r, a, g;
	
	r = p->r;
	a = r[INT|ADSR1];
	if((a & 0x80) != 0)
		g = r[ADSR2];
	else
		g = r[GAIN];
	if(p->envst == RELEASE)
		p->env -= 8;
	else if((a & 0x80) != 0){
		switch(p->envst){
		case ATTACK:
			if(envyes((a & 0xf) << 1 | 1))
				p->env += (a & 0xf) == 0xf ? 1024 : 32;
			break;
		case DECAY:
			if(envyes(a >> 3 & 0xe | 0x10))
				p->env -= ((p->env - 1) >> 8) + 1;
			break;
		case SUSTAIN:
			if(envyes(g & 0x1f))
				p->env -= ((p->env - 1) >> 8) + 1;
			break;
		}
	}else{
		if((g & 0x80) != 0){
			if(envyes(g & 0x1f))
				switch((g >> 5) & 3){
				case 0: p->env -= 32; break;
				case 1: p->env -= ((p->env - 1) >> 8) + 1; break;
				case 2: p->env += 32; break;
				case 3:
					if(p->envbent < 0x600)
						p->env += 32;
					else
						p->env += 8;
					break;
				}
		}else
			p->env = g << 4;
	}
	p->envbent = p->env & 0x7ff;
	if((p->env & 0x8000) != 0)
		p->env = 0;
	else if(p->env > 0x7ff){
		p->env = 0x7ff;
		if(p->envst == ATTACK){
			p->envst = DECAY;
			return;
		}
	}
	if(p->envst == DECAY && (p->env >> 8) == (g >> 5))
		p->envst = SUSTAIN;
}

static void
decode(vctxt *p)
{
	int i, d, s1, s2;
	u8int f, s;
	s16int brr;
	
	f = (p->hdr >> 2) & 3;
	s = p->hdr >> 4;
	brr = p->brr;
	s1 = p->buf[p->bp + 11];
	s2 = p->buf[p->bp + 10];
	for(i = 0; i < 4; i++){
		d = brr >> 12;
		if(s >= 13)
			d = d & ~0x7ff;
		else{
			d <<= s;
			d >>= 1;
		}
		s2 >>= 1;
		switch(f){
		case 1:
			d += s1 >> 1;
			d += (-s1) >> 5;
			break;
		case 2:
			d += s1;
			d -= s2;
			d += s2 >> 4;
			d += (s1 * -3) >> 6;
			break;
		case 3:
			d += s1;
			d -= s2;
			d += (s1 * -13) >> 7;
			d += (s2 * 3) >> 4;
			break;
		}
		d = (s16int)(clamp16(d) << 1);
		p->buf[p->bp] = d;
		p->buf[p->bp++ + 12] = d;
		s2 = s1;
		s1 = d;
		brr <<= 4;
	}
	if(p->bp == 12)
		p->bp = 0;
}

static s16int
interp(u16int x, s16int *s)
{
	extern u16int gauss[];
	int v;
	u8int i, d;

	i = x >> 12;
	d = x >> 4;
	v  = ((int)gauss[255 - d] * s[i]) >> 11;
	v += ((int)gauss[511 - d] * s[i+1]) >> 11;
	v += ((int)gauss[256 + d] * s[i+2]) >> 11;
	v = (s16int)v;
	v += ((int)gauss[d] * s[i+3]) >> 11;
	return clamp16(v) & ~1;
}

static void
voice(int n, int s)
{
	vctxt *p;
	u8int *r, m;
	u16int a;
	int v;
	
	p = vctxts + n;
	r = p->r;
	switch(s){
	case 1:
		r[INT|SRCN] = r[SRCN];
		break;
	case 2:
		a = (dsp[INT|DIR] << 8) + (r[INT|SRCN] << 2);
		if(p->init == READY)
			a += 2;
		p->sp = spc16(a);
		p->pitch = r[PITCHL];
		r[INT|ADSR1] = r[ADSR1];
		break;
	case 3:
		p->pitch |= r[PITCHH] << 8 & 0x3f00;
		if(n == 0)
			break;
	case 0x31:
		p->hdr = spcmem[p->hdrp];
		p->brr = spcmem[p->dp] << 8;
		if(n == 0)
			break;
	case 0x32:
		if((dsp[INT|PMON] & 0xfe & 1<<n) != 0)
			p->pitch += ((p[-1].modin >> 5) * p->pitch) >> 10;
		if(p->init < READY){
			p->pitch = 0;
			p->env = 0;
		}
		if((dsp[INT|NON] & 1<<n) != 0)
			p->sample = noise;
		else
			p->sample = interp(p->interp, p->buf + p->bp);
		p->sample = (((int)p->sample * p->env) >> 11) & ~1;
		p->modin = p->sample;
		r[INT|OUTX] = p->sample >> 8;
		r[INT|ENVX] = p->env >> 4;
		if((dsp[FLG] & 0x80) != 0 || (p->hdr & 3) == 1){
			p->envst = RELEASE;
			p->env = 0;
		}
		if((dsp[INT|KOFF] & 1<<n) != 0)
			p->envst = RELEASE;
		if((dsp[INT|KON] & 1<<n) != 0){
			p->envst = ATTACK;
			p->init = 0;
		}
		if(p->init >= READY - 1)
			env(p);
		break;
	case 4:
		v = (p->sample * ((s8int)r[VOLL])) >> 7;
		samp[0] = clamp16(samp[0] + v);
		if((dsp[INT|EON] & (1<<n)) != 0)
			echoin[0] = clamp16(echoin[0] + v);
		if(p->init == 1){
			p->hdrp = p->sp;
			p->dp = p->hdrp + 1;
			p->bp = 0;
		}
		if(p->interp >= 0x4000 || p->init <= 4 && p->init >= 2){
			p->brr |= spcmem[++p->dp];
			decode(p);
			if(++p->dp == p->hdrp + 9){
				if((p->hdr & 1) != 0){
					dsp[INT|ENDX] |= 1<<n;
					p->hdrp = p->sp;
				}else
					p->hdrp += 9;
				p->dp = p->hdrp + 1;
			}
		}
		p->interp = (p->interp & 0x3fff) + p->pitch;
		if(p->interp >= 0x7fff)
			p->interp = 0x7fff;
		break;
	case 5:
		v = (p->sample * ((s8int)r[VOLR])) >> 7;
		samp[1] = clamp16(samp[1] + v);
		if((dsp[INT|EON] & (1<<n)) != 0)
			echoin[1] = clamp16(echoin[1] + v); 
		break;
	case 7:
		m = 1<<n;
		if(p->init == 0)
			dsp[INT|ENDX] &= ~m;
		dsp[ENDX] = dsp[ENDX] & ~m | dsp[INT|ENDX] & m;
		break;
	case 8:
		r[OUTX] = r[INT|OUTX];
		break;
	case 9:
		r[ENVX] = r[INT|ENVX];
		if(p->init < READY)
			p->init++;
		break;
	}
}

static void
echo(int s)
{
	static s16int echoout[2];
	s16int *x;
	s8int h;
	int a, b;

	x = echobuf + echobp;
	switch(s){
	case 22:
		echoaddr = (dsp[INT|ESA] << 8) + echopos;
		x[0] = x[16] = spc16(echoaddr);
		h = dsp[0x0f];
		echoout[0] = x[2] * h >> 7;
		echoout[1] = x[3] * h >> 7;
		break;
	case 23:
		h = dsp[0x1f];
		echoout[0] += x[4] * h >> 7;
		echoout[1] += x[5] * h >> 7;
		h = dsp[0x2f];
		echoout[0] += x[6] * h >> 7;
		echoout[1] += x[7] * h >> 7;
		x[1] = x[17] = spc16(echoaddr + 2);
		break;
	case 24:
		h = dsp[0x3f];
		echoout[0] += x[8] * h >> 7;
		echoout[1] += x[9] * h >> 7;
		h = dsp[0x4f];
		echoout[0] += x[10] * h >> 7;
		echoout[1] += x[11] * h >> 7;
		h = dsp[0x5f];
		echoout[0] += x[12] * h >> 7;
		echoout[1] += x[13] * h >> 7;
		break;
	case 25:
		h = dsp[0x6f];
		echoout[0] += x[14] * h >> 7;
		echoout[1] += x[15] * h >> 7;
		h = dsp[0x7f];
		echoout[0] += x[16] * h >> 7;
		echoout[1] += x[17] * h >> 7;
		echoout[0] &= ~1;
		echoout[1] &= ~1;
		break;
	case 26:
		a = (samp[0] * (s8int)dsp[MVOLL]) >> 7;
		b = (echoout[0] * (s8int)dsp[EVOLL]) >> 7;
		samp[0] = clamp16(a + b);
		a = echoout[0] * (s8int)dsp[EFB] >> 7;
		echoin[0] = clamp16(echoin[0] + a) & ~1;
		a = echoout[1] * (s8int)dsp[EFB] >> 7;
		echoin[1] = clamp16(echoin[1] + a) & ~1;
		break;
	case 27:
		a = (samp[1] * (s8int)dsp[MVOLR]) >> 7;
		b = (echoout[1] * (s8int)dsp[EVOLR]) >> 7;
		samp[1] = clamp16(a + b);
		break;
	case 28:
		dsp[INT|FLG] = dsp[FLG];
		break;
	case 29:
		dsp[INT|ESA] = dsp[ESA];
		if(echopos == 0)
			dsp[INT|EDL] = dsp[EDL] & 0xf;
		echopos += 4;
		if(echopos >= dsp[INT|EDL] << 11)
			echopos = 0;
		if((dsp[INT|FLG] & 0x20) == 0)
			spcput16(echoaddr, echoin[0]);
		echoin[0] = 0;
		dsp[INT|FLG] = dsp[FLG];
		break;
	case 30:
		if((dsp[INT|FLG] & 0x20) == 0)
			spcput16(echoaddr + 2, echoin[1]);
		echoin[1] = 0;
		echobp = echobp + 2 & 15;
		break;
	}
}

void
dspstep(void)
{
	if(sbufp == nil || sbufp >= sbuf+nelem(sbuf)-2)
		return;
	switch(dspstate++ & 31){
	case  0: voice(0, 5); voice(1, 2); break;
	case  1: voice(0, 6); voice(1, 3); break;
	case  2: voice(0, 7); voice(1, 4); voice(3, 1); break;
	case  3: voice(0, 8); voice(1, 5); voice(2, 2); break;
	case  4: voice(0, 9); voice(1, 6); voice(2, 3); break;
	case  5: voice(1, 7); voice(2, 4); voice(4, 1); break;
	case  6: voice(1, 8); voice(2, 5); voice(3, 2); break;
	case  7: voice(1, 9); voice(2, 6); voice(3, 3); break;
	case  8: voice(2, 7); voice(3, 4); voice(5, 1); break;
	case  9: voice(2, 8); voice(3, 5); voice(4, 2); break;
	case 10: voice(2, 9); voice(3, 6); voice(4, 3); break;
	case 11: voice(3, 7); voice(4, 4); voice(6, 1); break;
	case 12: voice(3, 8); voice(4, 5); voice(5, 2); break;
	case 13: voice(3, 9); voice(4, 6); voice(5, 3); break;
	case 14: voice(4, 7); voice(5, 4); voice(7, 1); break;
	case 15: voice(4, 8); voice(5, 5); voice(6, 2); break;
	case 16: voice(4, 9); voice(5, 6); voice(6, 3); break;
	case 17: voice(5, 7); voice(6, 4); voice(0, 1); break;
	case 18: voice(5, 8); voice(6, 5); voice(7, 2); break;
	case 19: voice(5, 9); voice(6, 6); voice(7, 3); break;
	case 20: voice(6, 7); voice(7, 4); voice(1, 1); break;
	case 21: voice(6, 8); voice(7, 5); voice(0, 2); break;
	case 22: voice(6, 9); voice(7, 6); voice(0, 3); echo(22); break;
	case 23: voice(7, 7); echo(23); break;
	case 24: voice(7, 8); echo(24); break;
	case 25: voice(7, 9); voice(0, 0x31); echo(25); break;
	case 26: echo(26); break;
	case 27:
		dsp[INT|PMON] = dsp[PMON];
		echo(27);
		if((dsp[FLG] & 0x40) != 0)
			samp[0] = samp[1] = 0;
		audiosample(samp);
		samp[0] = samp[1] = 0;
		break;
	case 28:
		dsp[INT|NON] = dsp[NON];
		dsp[INT|EON] = dsp[EON];
		dsp[INT|DIR] = dsp[DIR];
		echo(28);
		break;
	case 29:
		echo(29);
		if((dspstate & 32) == 0)
			dsp[NEWKON] &= ~dsp[INT|KON];
		break;
	case 30:
		voice(0, 0x32);
		echo(30);
		if((dspstate & 32) == 0){
			dsp[INT|KOFF] = dsp[KOFF];
			dsp[INT|KON] = dsp[NEWKON];
		}
		if(dspcounter-- == 0)
			dspcounter = 0x77ff;
		if(envyes(dsp[FLG] & 0x1f))
			noise = (noise << 13 ^ noise << 14) & 0x8000 | noise >> 1 & ~1;
		break;
	case 31: voice(0, 4); voice(2, 1); break;
	}
}

void
dspreset(void)
{
	vctxt *p;
	int i;
	
	for(i = 0; i < 8; i++){
		p = vctxts + i;
		p->n = i;
		p->r = dsp + (i << 4);
	}
	dsp[FLG] = 0xe0;
}

void
dspsave(void)
{
	vctxt *p;

	for(p = vctxts; p < vctxts + nelem(vctxts); p++){
		put16(p->hdrp);
		put16(p->dp);
		put16(p->sp);
		put8(p->hdr);
		put8(p->bp);
		put16(p->brr);
		put8(p->envst);
		put16(p->env);
		put16(p->envbent);
		put8(p->init);
		put16(p->interp);
		put16(p->pitch);
		put16(p->modin);
	}
}

void
dspload(void)
{
	vctxt *p;

	for(p = vctxts; p < vctxts + nelem(vctxts); p++){
		p->hdrp = get16();
		p->dp = get16();
		p->sp = get16();
		p->hdr = get8();
		p->bp = get8();
		p->brr = get16();
		p->envst = get8();
		p->env = get16();
		p->envbent = get16();
		p->init = get8();
		p->interp = get16();
		p->pitch = get16();
		p->modin = get16();
	}
}

u16int gauss[512] = {
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001,
	0x001, 0x001, 0x001, 0x002, 0x002, 0x002, 0x002, 0x002,
	0x002, 0x002, 0x003, 0x003, 0x003, 0x003, 0x003, 0x004,
	0x004, 0x004, 0x004, 0x004, 0x005, 0x005, 0x005, 0x005,
	0x006, 0x006, 0x006, 0x006, 0x007, 0x007, 0x007, 0x008,
	0x008, 0x008, 0x009, 0x009, 0x009, 0x00A, 0x00A, 0x00A,
	0x00B, 0x00B, 0x00B, 0x00C, 0x00C, 0x00D, 0x00D, 0x00E,
	0x00E, 0x00F, 0x00F, 0x00F, 0x010, 0x010, 0x011, 0x011,
	0x012, 0x013, 0x013, 0x014, 0x014, 0x015, 0x015, 0x016,
	0x017, 0x017, 0x018, 0x018, 0x019, 0x01A, 0x01B, 0x01B,
	0x01C, 0x01D, 0x01D, 0x01E, 0x01F, 0x020, 0x020, 0x021,
	0x022, 0x023, 0x024, 0x024, 0x025, 0x026, 0x027, 0x028,
	0x029, 0x02A, 0x02B, 0x02C, 0x02D, 0x02E, 0x02F, 0x030,
	0x031, 0x032, 0x033, 0x034, 0x035, 0x036, 0x037, 0x038,
	0x03A, 0x03B, 0x03C, 0x03D, 0x03E, 0x040, 0x041, 0x042,
	0x043, 0x045, 0x046, 0x047, 0x049, 0x04A, 0x04C, 0x04D,
	0x04E, 0x050, 0x051, 0x053, 0x054, 0x056, 0x057, 0x059,
	0x05A, 0x05C, 0x05E, 0x05F, 0x061, 0x063, 0x064, 0x066,
	0x068, 0x06A, 0x06B, 0x06D, 0x06F, 0x071, 0x073, 0x075,
	0x076, 0x078, 0x07A, 0x07C, 0x07E, 0x080, 0x082, 0x084,
	0x086, 0x089, 0x08B, 0x08D, 0x08F, 0x091, 0x093, 0x096,
	0x098, 0x09A, 0x09C, 0x09F, 0x0A1, 0x0A3, 0x0A6, 0x0A8,
	0x0AB, 0x0AD, 0x0AF, 0x0B2, 0x0B4, 0x0B7, 0x0BA, 0x0BC,
	0x0BF, 0x0C1, 0x0C4, 0x0C7, 0x0C9, 0x0CC, 0x0CF, 0x0D2,
	0x0D4, 0x0D7, 0x0DA, 0x0DD, 0x0E0, 0x0E3, 0x0E6, 0x0E9,
	0x0EC, 0x0EF, 0x0F2, 0x0F5, 0x0F8, 0x0FB, 0x0FE, 0x101,
	0x104, 0x107, 0x10B, 0x10E, 0x111, 0x114, 0x118, 0x11B,
	0x11E, 0x122, 0x125, 0x129, 0x12C, 0x130, 0x133, 0x137,
	0x13A, 0x13E, 0x141, 0x145, 0x148, 0x14C, 0x150, 0x153,
	0x157, 0x15B, 0x15F, 0x162, 0x166, 0x16A, 0x16E, 0x172,
	0x176, 0x17A, 0x17D, 0x181, 0x185, 0x189, 0x18D, 0x191,
	0x195, 0x19A, 0x19E, 0x1A2, 0x1A6, 0x1AA, 0x1AE, 0x1B2,
	0x1B7, 0x1BB, 0x1BF, 0x1C3, 0x1C8, 0x1CC, 0x1D0, 0x1D5,
	0x1D9, 0x1DD, 0x1E2, 0x1E6, 0x1EB, 0x1EF, 0x1F3, 0x1F8,
	0x1FC, 0x201, 0x205, 0x20A, 0x20F, 0x213, 0x218, 0x21C,
	0x221, 0x226, 0x22A, 0x22F, 0x233, 0x238, 0x23D, 0x241,
	0x246, 0x24B, 0x250, 0x254, 0x259, 0x25E, 0x263, 0x267,
	0x26C, 0x271, 0x276, 0x27B, 0x280, 0x284, 0x289, 0x28E,
	0x293, 0x298, 0x29D, 0x2A2, 0x2A6, 0x2AB, 0x2B0, 0x2B5,
	0x2BA, 0x2BF, 0x2C4, 0x2C9, 0x2CE, 0x2D3, 0x2D8, 0x2DC,
	0x2E1, 0x2E6, 0x2EB, 0x2F0, 0x2F5, 0x2FA, 0x2FF, 0x304,
	0x309, 0x30E, 0x313, 0x318, 0x31D, 0x322, 0x326, 0x32B,
	0x330, 0x335, 0x33A, 0x33F, 0x344, 0x349, 0x34E, 0x353,
	0x357, 0x35C, 0x361, 0x366, 0x36B, 0x370, 0x374, 0x379,
	0x37E, 0x383, 0x388, 0x38C, 0x391, 0x396, 0x39B, 0x39F,
	0x3A4, 0x3A9, 0x3AD, 0x3B2, 0x3B7, 0x3BB, 0x3C0, 0x3C5,
	0x3C9, 0x3CE, 0x3D2, 0x3D7, 0x3DC, 0x3E0, 0x3E5, 0x3E9,
	0x3ED, 0x3F2, 0x3F6, 0x3FB, 0x3FF, 0x403, 0x408, 0x40C,
	0x410, 0x415, 0x419, 0x41D, 0x421, 0x425, 0x42A, 0x42E,
	0x432, 0x436, 0x43A, 0x43E, 0x442, 0x446, 0x44A, 0x44E,
	0x452, 0x455, 0x459, 0x45D, 0x461, 0x465, 0x468, 0x46C,
	0x470, 0x473, 0x477, 0x47A, 0x47E, 0x481, 0x485, 0x488,
	0x48C, 0x48F, 0x492, 0x496, 0x499, 0x49C, 0x49F, 0x4A2,
	0x4A6, 0x4A9, 0x4AC, 0x4AF, 0x4B2, 0x4B5, 0x4B7, 0x4BA,
	0x4BD, 0x4C0, 0x4C3, 0x4C5, 0x4C8, 0x4CB, 0x4CD, 0x4D0,
	0x4D2, 0x4D5, 0x4D7, 0x4D9, 0x4DC, 0x4DE, 0x4E0, 0x4E3,
	0x4E5, 0x4E7, 0x4E9, 0x4EB, 0x4ED, 0x4EF, 0x4F1, 0x4F3,
	0x4F5, 0x4F6, 0x4F8, 0x4FA, 0x4FB, 0x4FD, 0x4FF, 0x500,
	0x502, 0x503, 0x504, 0x506, 0x507, 0x508, 0x50A, 0x50B,
	0x50C, 0x50D, 0x50E, 0x50F, 0x510, 0x511, 0x511, 0x512,
	0x513, 0x514, 0x514, 0x515, 0x516, 0x516, 0x517, 0x517,
	0x517, 0x518, 0x518, 0x518, 0x518, 0x518, 0x519, 0x519
};
