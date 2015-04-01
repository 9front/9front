#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Event evsamp, evenv;
s16int sbuf[2*4000], *sbufp;
enum {
	Freq = 44100,
	SRATEDIV = 8388608 / Freq,
	ENVDIV = 8388608 / 512
};
static int fd;

u16int envmod;
u8int sweepen, sweepctr;
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
u8int wave[32];
u8int wpos;
u16int lfsr;

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
	VAR(envmod), VAR(sweepen),
	VAR(sweepctr), VAR(sweepfreq), ARR(wave), VAR(wpos), VAR(lfsr),
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
		sndch[2].finc = 2097152ULL * 65536 / (Freq * (2048 - (v & 0x7ff)));
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
		--c->len;
	if(c->len == 0){
		c->vol = 0;
		return;
	}
	if((envmod & 7) != 7 || c->ectr == 0 || --c->ectr != 0)
		return;
	c->ectr = *c->env & 7;
	if((*c->env & 1<<3) != 0){
		if(c->vol < 15)
			c->vol++;
	}else
		if(c->vol > 0)
			c->vol--;
}

s8int
wavesamp(void)
{
	s8int x;
	int v;

	sndch[2].fctr = v = sndch[2].fctr + sndch[2].finc;
	if(sndch[2].len == 0 || (reg[NR30] & 1<<7) == 0)
		return 0;
	for(;;){
		x = wave[wpos];
		v -= 0x10000;
		if(v < 0)
			break;
		wpos = wpos + 1 & 31;
	}
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
			if((reg[0x7c/2] & 1<<3) != 0)
				lfsr |= 0x40;
			else
				lfsr |= 0x4000;
	}
	if((l & 1) != 0)
		return -sndch[3].vol;
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
	if(fr > 2047){
		sndch[0].len = 0;
		sndch[0].vol = 0;
		sweepen = 0;
	}else if(wb){
		sweepfreq = fr;
		reg[NR13] = fr;
		reg[NR14] = reg[NR14] & 0xf8 | fr >> 8;
		rate(0, fr);
		sweep(0);
	}
}

void
sndstart(chan *c, u16int v)
{
	u8int cnt;

	c->vol = *c->env >> 4;
	c->ectr = *c->env & 7;
	if(c->len == 0)
		c->len = 64;
	if(c == sndch){
		cnt = reg[NR10];
		sweepen = (cnt & 0x07) != 0 && (cnt & 0x70) != 0;
		sweepctr = cnt >> 4 & 7;
		sweepfreq = v & 0x7ff;
		if((cnt & 0x07) != 0)
			sweep(0);
	}
}

void
envtick(void *)
{
	addevent(&evenv, ENVDIV);

	env(&sndch[0]);
	env(&sndch[1]);
	if((envmod & 1) == 0 && sndch[2].len > 0 && (reg[NR34] & 0x40) != 0)
		sndch[2].len--;
	env(&sndch[3]);
	if((envmod & 3) == 2 && sweepen && --sweepctr == 0){
		sweepctr = reg[NR10] >> 4 & 7;
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
		ch[0] = -sndch[0].vol;
	sndch[1].fctr += sndch[1].finc;
	if(sndch[1].fctr >= sndch[1].fthr)
		ch[1] = sndch[1].vol;
	else
		ch[1] = -sndch[1].vol;
	ch[2] = wavesamp();
	ch[3] = lfsrsamp();
	
	cntl = reg[NR50];
	cnth = reg[NR51];
	s[0] = 0;
	s[1] = 0;
	for(i = 0; i < 4; i++){
		if((cnth & 1<<i) != 0)
			s[1] += ch[i] * (1 + (cntl & 7));
		if((cnth & 1<<4<<i) != 0)
			s[0] += ch[i] * (1 + (cntl >> 4 & 7));
	}
	if(s[0] < -0x200) s[0] = -0x200;
	else if(s[0] > 0x1ff) s[0] = 0x1ff;
	if(s[1] < -0x200) s[1] = -0x200;
	else if(s[1] > 0x1ff) s[1] = 0x1ff;
	
	if(sbufp < sbuf + nelem(sbuf)){
		sbufp[0] = s[0] << 6;
		sbufp[1] = s[1] << 6;
		sbufp += 2;
	}
}

void
sndwrite(u8int a, u8int v)
{
	static u16int thr[4] = {0x2000, 0x4000, 0x8000, 0xC000};
	
	if((reg[NR52] & 0x80) == 0 && a != NR52)
		return;
	switch(a){
	case NR11:
		sndch[0].fthr = thr[v >> 6 & 3];
		sndch[0].len = 64 - (v & 63);
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
		if((v & 1<<7) != 0 && sndch[2].len == 0)
			sndch[2].len = 256;
		break;
	case NR31:
		sndch[2].len = 256 - (v & 0xff);
		break;
	case NR33:
		rate(2, reg[NR34] << 8 & 0x700 | v);
		break;
	case NR34:
		rate(2, v << 8 & 0x700 | reg[NR33]);
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
		if((v & 0x80) == 0){
			memset(reg + NR10, 0, NR52 - NR10);
			sndch[0].len = 0;
			sndch[1].len = 0;
			sndch[2].len = 0;
			sndch[3].len = 0;
		}
	}
}

int
apuread(void)
{
	u8int v;
	
	v = reg[NR52] & 0xf0;
	if(sndch[0].len != 0) v |= 1;
	if(sndch[1].len != 0) v |= 2;
	if(sndch[2].len != 0) v |= 4;
	if(sndch[3].len != 0) v |= 8;
	return v;
}

u8int
waveread(u8int a)
{
	a <<= 1;
	return wave[a + wpos & 31] << 4 | wave[a + wpos + 1 & 31];
}

void
wavewrite(u8int a, u8int v)
{
	a <<= 1;
	wave[a + wpos & 31] = v >> 4;
	wave[a + wpos + 1 & 31] = v & 0x0f;
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
	evenv.f = envtick;
	addevent(&evenv, ENVDIV);
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
