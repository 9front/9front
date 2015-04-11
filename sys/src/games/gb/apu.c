#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Event evsamp;
extern Event evenv, evwave;
s16int sbuf[2*4000], *sbufp;
enum {
	Freq = 44100,
	SRATEDIV = FREQ / Freq
};
static int fd;

u16int envmod;
u8int sweepen, sweepcalc, sweepctr;
u16int sweepfreq;
typedef struct chan chan;
struct chan {
	u8int n, ectr;
	u16int len;
	u8int *env, *freq;
	u16int fctr, fthr;
	u32int finc;
	u8int vol;
};
u8int wpos;
u16int lfsr;
u8int apustatus;
ulong waveclock;
u8int wavebuf;

chan sndch[4] = {
	{
		.n = 0,
		.env = reg + NR12,
		.freq = reg + NR14,
	},
	{
		.n = 1,
		.env = reg + NR22,
		.freq = reg + NR24,
	},
	{
		.n = 2,
	},
	{
		.n = 3,
		.env = reg + NR42,
		.freq = reg + NR44,
	}
};

Var apuvars[] = {
	VAR(apustatus), VAR(envmod), VAR(sweepen), VAR(sweepcalc),
	VAR(sweepctr), VAR(sweepfreq), VAR(wpos), VAR(lfsr), VAR(waveclock), VAR(wavebuf),
	VAR(sndch[0].ectr), VAR(sndch[0].len), VAR(sndch[0].fctr), VAR(sndch[0].fthr), VAR(sndch[0].finc), VAR(sndch[0].vol),
	VAR(sndch[1].ectr), VAR(sndch[1].len), VAR(sndch[1].fctr), VAR(sndch[1].fthr), VAR(sndch[1].finc), VAR(sndch[1].vol),
	VAR(sndch[2].ectr), VAR(sndch[2].len), VAR(sndch[2].fctr), VAR(sndch[2].fthr), VAR(sndch[2].finc), VAR(sndch[2].vol),
	VAR(sndch[3].ectr), VAR(sndch[3].len), VAR(sndch[3].fctr), VAR(sndch[3].fthr), VAR(sndch[3].finc), VAR(sndch[3].vol),
	{nil, 0, 0},
};

void
rate(int i, u16int v)
{
	switch(i){
	case 0: case 1:
		sndch[i].finc = 131072ULL * 65536 / (Freq * (2048 - (v & 0x7ff)));
		break;
	case 2:
		sndch[2].finc = 4 * (2048 - (v & 0x7ff));
		break;
	case 3:
		sndch[3].finc = 524288ULL * 65536 / Freq;
		if((v & 7) != 0)
			sndch[3].finc /= v & 7;
		else
			sndch[3].finc <<= 1;
		sndch[3].finc >>= (v >> 4 & 15) + 1;
	}
}

void
env(chan *c)
{
	if((envmod & 1) == 0 && c->len > 0 && (*c->freq & 1<<6) != 0)
		if(--c->len == 0){
			apustatus &= ~(1<<c->n);
			c->vol = 0;
			return;
		}
	if((apustatus & 1<<c->n) == 0 || (envmod & 7) != 7 || c->ectr == 0 || --c->ectr != 0)
		return;
	c->ectr = *c->env & 7;
	if((*c->env & 1<<3) != 0){
		if(c->vol < 15)
			c->vol++;
	}else
		if(c->vol > 0)
			c->vol--;
}

void
wavetick(void *)
{
	addevent(&evwave, sndch[2].finc);
	wpos = wpos + 1 & 31;
	wavebuf = reg[WAVE + (wpos >> 1)];
	waveclock = clock;
}

s8int
wavesamp(void)
{
	u8int x;

	if((apustatus & 1<<4) == 0)
		return 0;
	x = wavebuf;
	if((wpos & 1) == 0)
		x >>= 4;
	else
		x &= 0xf;
	if((reg[NR32] & 3<<5) == 0)
		x = 0;
	else
		x = x >> (reg[NR32] >> 5 & 3) - 1;
	return x;
}

s8int
lfsrsamp(void)
{
	int v;
	u16int l;

	sndch[3].fctr = v = sndch[3].fctr + sndch[3].finc;
	for(;;){
		l = lfsr;
		v -= 0x10000;
		if(v < 0)
			break;
		lfsr >>= 1;
		if(((l ^ lfsr) & 1) != 0)
			if((reg[NR43] & 1<<3) != 0)
				lfsr |= 0x40;
			else
				lfsr |= 0x4000;
	}
	if((l & 1) != 0)
		return 0;
	else
		return sndch[3].vol;
}

void
sweep(int wb)
{
	u16int fr;
	int d;
	u16int cnt;
	
	cnt = reg[NR10];
	d = sweepfreq >> (cnt & 7);
	if((cnt & 1<<3) != 0)
		d = -d;
	fr = sweepfreq + d;
	sweepcalc |= cnt;
	if(fr > 2047){
		sndch[0].len = 0;
		sndch[0].vol = 0;
		apustatus &= ~1;
		sweepen = 0;
	}else if(wb && (cnt & 7) != 0){
		sweepfreq = fr;
		reg[NR13] = fr;
		reg[NR14] = reg[NR14] & 0xf8 | fr >> 8;
		rate(0, fr);
		sweep(0);
	}
}

void
sndstart(chan *c, u8int v)
{
	u8int cnt;

	c->vol = *c->env >> 4;
	c->ectr = *c->env & 7;
	if(c->len == 0)
		c->len = 64;
	apustatus |= 1<<c->n;
	if(c == sndch){
		cnt = reg[NR10];
		sweepen = (cnt & 0x07) != 0 || (cnt & 0x70) != 0;
		sweepctr = cnt >> 4 & 7;
		sweepctr += sweepctr - 1 & 8;
		sweepfreq = v << 8 & 0x700 | reg[NR13];
		sweepcalc = 0;
		if((cnt & 0x07) != 0)
			sweep(0);
	}
	if((*c->freq & 0x40) == 0 && (v & 0x40) != 0 && (envmod & 1) != 0 && --c->len == 0 || (*c->env & 0xf8) == 0){
		apustatus &= ~(1<<c->n);
		c->vol = 0;
	}
}

void
envtick(void *)
{
	addevent(&evenv, FREQ / 512);

	env(&sndch[0]);
	env(&sndch[1]);
	if((envmod & 1) == 0 && sndch[2].len > 0 && (reg[NR34] & 0x40) != 0)
		if(--sndch[2].len == 0){
			apustatus &= ~4;
			delevent(&evwave);
		}
	env(&sndch[3]);
	if((envmod & 3) == 2 && sweepen && --sweepctr == 0){
		sweepctr = reg[NR10] >> 4 & 7;
		sweepctr += sweepctr - 1 & 8;
		if((reg[NR10] & 0x70) != 0)
			sweep(1);
	}
	envmod++;
}

void
sampletick(void *)
{
	u8int cntl, cnth;
	s16int ch[4];
	s16int s[2];
	int i;
	
	addevent(&evsamp, SRATEDIV);
	
	sndch[0].fctr += sndch[0].finc;
	if(sndch[0].fctr >= sndch[0].fthr)
		ch[0] = sndch[0].vol;
	else
		ch[0] = 0;
	sndch[1].fctr += sndch[1].finc;
	if(sndch[1].fctr >= sndch[1].fthr)
		ch[1] = sndch[1].vol;
	else
		ch[1] = 0;
	ch[2] = wavesamp();
	ch[3] = lfsrsamp();
	
	cntl = reg[NR50];
	cnth = reg[NR51];
	s[0] = 0;
	s[1] = 0;
	for(i = 0; i < 4; i++){
		if(i == 2 ? ((reg[NR30] & 0x80) == 0) : ((*sndch[i].env & 0xf8) == 0))
			continue;
		ch[i] = ch[i] * 2 - 15;
		if((cnth & 1<<i) != 0)
			s[0] += ch[i];
		if((cnth & 1<<4<<i) != 0)
			s[1] += ch[i];
	}
	s[0] *= 1 + (cntl & 7);
	s[1] *= 1 + (cntl >> 4 & 7);
	
	if(sbufp < sbuf + nelem(sbuf)){
		sbufp[0] = s[0] * 30;
		sbufp[1] = s[1] * 30;
		sbufp += 2;
	}
}

void
sndwrite(u8int a, u8int v)
{
	static u16int thr[4] = {0x2000, 0x4000, 0x8000, 0xC000};
	static u8int clrreg[] = {
		0x80, 0x3f, 0x00, 0xff, 0xbf,
		0xff, 0x3f, 0x00, 0xff, 0xbf,
		0x7f, 0xff, 0x9f, 0xff, 0xbf,
		0xff, 0xff, 0x00, 0x00, 0xbf,
		0x00, 0x00
	};
	
	if((reg[NR52] & 0x80) == 0 && a != NR52 && ((mode & CGB) != 0 || a != NR11 && a != NR21 && a != NR31 && a != NR41))
		return;
	switch(a){
	case NR10:
		if((sweepcalc & 0x08) != 0 && (reg[NR10] & ~v & 0x08) != 0){
			sndch[0].vol = 0;
			apustatus &= ~1;
			sweepcalc = 0;
		}
		break;
	case NR11:
		sndch[0].fthr = thr[v >> 6 & 3];
		sndch[0].len = 64 - (v & 63);
		break;
	case NR12:
		if((v & 0xf8) == 0){
			sndch[0].vol = 0;
			apustatus &= ~1;
		}
		break;
	case NR13:
		rate(0, reg[NR14] << 8 & 0x700 | v);
		break;
	case NR14:
		rate(0, v << 8 & 0x700 | reg[NR13]);
		if((v & 1<<7) != 0)
			sndstart(&sndch[0], v);
		break;
	case NR21:
		sndch[1].fthr = thr[v >> 6 & 3];
		sndch[1].len = 64 - (v & 63);
		break;
	case NR22:
		if((v & 0xf8) == 0){
			sndch[1].vol = 0;
			apustatus &= ~2;
		}
		break;
	case NR23:
		rate(1, reg[NR24] << 8 & 0x700 | v);
		break;
	case NR24:
		rate(1, v << 8 & 0x700 | reg[NR23]);
		if((v & 1<<7) != 0)
			sndstart(&sndch[1], v);
		break;
	case NR30:
		if((v & 0x80) == 0){
			apustatus &= ~4;
			delevent(&evwave);
		}
		break;
	case NR31:
		sndch[2].len = 256 - (v & 0xff);
		break;
	case NR33:
		rate(2, reg[NR34] << 8 & 0x700 | v);
		break;
	case NR34:
		rate(2, v << 8 & 0x700 | reg[NR33]);
		if((v & 0x80) != 0){
			if(sndch[2].len == 0)
				sndch[2].len = 256;
			wpos = 0;
			if((reg[NR30] & 0x80) != 0){
				apustatus |= 4;
				delevent(&evwave);
				addevent(&evwave, sndch[2].finc);
			}
		}
		break;
	case NR41:
		sndch[3].len = 64 - (v & 63);
		break;
	case NR42:
		if((v & 0xf8) == 0){
			sndch[3].vol = 0;
			apustatus &= ~8;
		}
		break;
	case NR43:
		rate(3, v);
		break;
	case NR44:
		if((v & 1<<7) != 0){
			if((reg[NR43] & 1<<3) != 0)
				lfsr = 0x7f;
			else
				lfsr = 0x7fff;
			sndstart(&sndch[3], v);
		}
		break;
	case NR52:
		apustatus = v & 0xf0 | apustatus & 0x0f;
		if((v & 0x80) == 0){
			memcpy(reg + NR10, clrreg, NR52 - NR10);
			if((mode & CGB) != 0){
				sndch[0].len = 0;
				sndch[1].len = 0;
				sndch[2].len = 0;
				sndch[3].len = 0;
				apustatus = 0;
				delevent(&evwave);
			}
		}else if((reg[NR52] & 0x80) == 0){
			envmod = 0;
			delevent(&evenv);
			addevent(&evenv, FREQ / 512);
			sndch[0].fctr = 0;
			sndch[1].fctr = 0;
			sndch[3].fctr = 0;
		}
	}
	reg[a] = v;
}

u8int
waveread(u8int a)
{
	if((apustatus & 4) != 0)
		if((mode & CGB) != 0 || clock - waveclock == 0)
			return wavebuf;
		else
			return 0xff;
	return reg[WAVE + a];
}

void
wavewrite(u8int a, u8int v)
{
	reg[WAVE + a] = v;
}

void
audioinit(void)
{
	fd = open("/dev/audio", OWRITE);
	if(fd < 0)
		sysfatal("open: %r");
	sbufp = sbuf;
	evsamp.f = sampletick;
	addevent(&evsamp, SRATEDIV);
}

int
audioout(void)
{
	int rc;
	static int cl;

	if(sbufp == nil)
		return -1;
	if(sbufp == sbuf)
		return 0;
	cl = clock;
	rc = write(fd, sbuf, (sbufp - sbuf) * 2);
	if(rc > 0)
		sbufp -= (rc+1)/2;
	if(sbufp < sbuf)
		sbufp = sbuf;
	return 0;
}
