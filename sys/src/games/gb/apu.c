#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

double TAU = 25000;

Event evsamp;
extern Event evenv;
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
	u8int *env, *freq;
	int per;
	u16int len;
	u8int n, ectr;
	u8int vol, ctr, samp;
};
u8int wpos;
u16int lfsr;
u8int apustatus;
ulong waveclock;
u8int wavebuf;
double samp[2];

chan sndch[4] = {
	{
		.n = 0,
		.env = reg + NR12,
		.freq = reg + NR14,
		.per = 8 * 2048,
	},
	{
		.n = 1,
		.env = reg + NR22,
		.freq = reg + NR24,
		.per = 8 * 2048,
	},
	{
		.n = 2,
	},
	{
		.n = 3,
		.env = reg + NR42,
		.freq = reg + NR44,
		.per = 32
	}
};
Event chev[4] = {
	{.aux = &sndch[0]},
	{.aux = &sndch[1]},
	{.aux = &sndch[2]},
	{.aux = &sndch[3]}
};

Var apuvars[] = {
	VAR(apustatus), VAR(envmod), VAR(sweepen), VAR(sweepcalc),
	VAR(sweepctr), VAR(sweepfreq), VAR(wpos), VAR(lfsr), VAR(waveclock), VAR(wavebuf),
	VAR(sndch[0].ectr), VAR(sndch[0].len), VAR(sndch[0].per), VAR(sndch[0].ctr), VAR(sndch[0].vol), VAR(sndch[0].samp),
	VAR(sndch[1].ectr), VAR(sndch[1].len), VAR(sndch[1].per), VAR(sndch[1].ctr), VAR(sndch[1].vol), VAR(sndch[1].samp),
	VAR(sndch[2].ectr), VAR(sndch[2].len), VAR(sndch[2].per), VAR(sndch[2].vol), VAR(sndch[2].samp),
	VAR(sndch[3].ectr), VAR(sndch[3].len), VAR(sndch[3].per), VAR(sndch[3].vol), VAR(sndch[3].samp),
	{nil, 0, 0},
};

static void
rate(chan *c, u16int v)
{
	switch(c->n){
	case 0: case 1:
		c->per = 8 * (2048 - (v & 0x7ff));
		break;
	case 2:
		c->per = 4 * (2048 - (v & 0x7ff));
		break;
	case 3:
		c->per = 32;
		if((v & 7) != 0)
			c->per *= v & 7;
		else
			c->per >>= 1;
		c->per <<= (v >> 4 & 15);
	}
}

static void
filter(int t)
{
	static int ov0, ov1;
	static u32int oclock;
	double e;
	u8int cntl, cnth;
	int i, v;

	e = exp((clock + t - oclock) * -(TAU / FREQ));
	samp[0] = e * samp[0] + (1 - e) * ov0;
	samp[1] = e * samp[1] + (1 - e) * ov1;
	oclock = clock + t;
	cntl = reg[NR50];
	cnth = reg[NR51];
	ov0 = 0;
	ov1 = 0;
	for(i = 0; i < 4; i++){
		if(i == 2 ? ((reg[NR30] & 0x80) == 0) : ((*sndch[i].env & 0xf8) == 0))
			continue;
		v = sndch[i].samp * 2 - 15;
		if((cnth & 1<<4<<i) != 0)
			ov0 += v;
		if((cnth & 1<<i) != 0)
			ov1 += v;
	}
	ov0 *= 1 + (cntl >> 4 & 7);
	ov1 *= 1 + (cntl & 7);
}

static void
chansamp(chan *c, int t)
{
	u8int ov;
	
	ov = c->samp;
	switch(c->n){
	case 0: case 1:
		c->samp = c->vol;
		switch(reg[NR21] >> 6){
		case 0: if(c->ctr < 7) c->samp = 0; break;
		case 1: if(c->ctr < 6) c->samp = 0; break;
		case 2: if(c->ctr < 4) c->samp = 0; break;
		case 3: if(c->ctr >= 6) c->samp = 0; break;
		}
		break;
	case 2:
		if((apustatus & 1<<4) == 0){
			c->samp = 0;
			break;
		}
		c->samp = wavebuf;
		if((wpos & 1) == 0)
			c->samp >>= 4;
		else
			c->samp &= 0xf;
		if((reg[NR32] & 3<<5) == 0)
			c->samp = 0;
		else
			c->samp = c->samp >> (reg[NR32] >> 5 & 3) - 1;
		break;
	case 3:
		c->samp = (lfsr & 1) != 0 ? 0 : c->vol;
	}
	if(ov != c->samp)
		filter(t);
}

void
chantick(void *vc)
{
	chan *c;
	u16int l;
	
	c = vc;
	switch(c->n){
	case 0: case 1:
		c->ctr = c->ctr - 1 & 7;
		break;
	case 2:
		wpos = wpos + 1 & 31;
		wavebuf = reg[WAVE + (wpos >> 1)];
		waveclock = clock;
		break;
	case 3:
		l = lfsr;
		lfsr >>= 1;
		if(((l ^ lfsr) & 1) != 0)
			if((reg[NR43] & 1<<3) != 0)
				lfsr |= 0x40;
			else
				lfsr |= 0x4000;
		break;
	}
	chansamp(c, chev[c->n].time);
	addevent(&chev[c->n], c->per);
}

static void
env(chan *c, int t)
{
	if((envmod & 1) == 0 && c->len > 0 && (*c->freq & 1<<6) != 0)
		if(--c->len == 0){
			apustatus &= ~(1<<c->n);
			c->vol = 0;
			chansamp(c, t);
			return;
		}
	if((apustatus & 1<<c->n) == 0 || (envmod & 7) != 7 || c->ectr == 0 || --c->ectr != 0)
		return;
	c->ectr = *c->env & 7;
	if((*c->env & 1<<3) != 0){
		if(c->vol < 15){
			c->vol++;
			chansamp(c, t);
		}
	}else
		if(c->vol > 0){
			c->vol--;
			chansamp(c, t);
		}
}

static void
sweep(int wb, int t)
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
		chansamp(&sndch[0], t);
		apustatus &= ~1;
		sweepen = 0;
	}else if(wb && (cnt & 7) != 0){
		sweepfreq = fr;
		reg[NR13] = fr;
		reg[NR14] = reg[NR14] & 0xf8 | fr >> 8;
		rate(&sndch[0], fr);
		sweep(0, t);
	}
}

void
sndstart(chan *c, u8int v)
{
	u8int cnt;

	filter(0);
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
			sweep(0, 0);
	}
	if((*c->freq & 0x40) == 0 && (v & 0x40) != 0 && (envmod & 1) != 0 && --c->len == 0 || (*c->env & 0xf8) == 0){
		apustatus &= ~(1<<c->n);
		c->vol = 0;
	}
	chansamp(c, 0);
}

void
envtick(void *)
{
	env(&sndch[0], evenv.time);
	env(&sndch[1], evenv.time);
	if((envmod & 1) == 0 && sndch[2].len > 0 && (reg[NR34] & 0x40) != 0)
		if(--sndch[2].len == 0){
			apustatus &= ~4;
			delevent(&chev[2]);
		}
	env(&sndch[3], evenv.time);
	if((envmod & 3) == 2 && sweepen && --sweepctr == 0){
		sweepctr = reg[NR10] >> 4 & 7;
		sweepctr += sweepctr - 1 & 8;
		if((reg[NR10] & 0x70) != 0)
			sweep(1, evenv.time);
	}
	envmod++;
	addevent(&evenv, FREQ / 512);
}

void
sampletick(void *)
{
	filter(evsamp.time);
	if(sbufp < sbuf + nelem(sbuf)){
		sbufp[0] = samp[0] * 30;
		sbufp[1] = samp[1] * 30;
		sbufp += 2;
	}
	addevent(&evsamp, SRATEDIV);
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
		sndch[0].len = 64 - (v & 63);
		break;
	case NR12:
		if((v & 0xf8) == 0){
			sndch[0].vol = 0;
			apustatus &= ~1;
		}
		break;
	case NR13:
		rate(&sndch[0], reg[NR14] << 8 & 0x700 | v);
		break;
	case NR14:
		rate(&sndch[0], v << 8 & 0x700 | reg[NR13]);
		if((v & 1<<7) != 0)
			sndstart(&sndch[0], v);
		break;
	case NR21:
		sndch[1].len = 64 - (v & 63);
		break;
	case NR22:
		if((v & 0xf8) == 0){
			sndch[1].vol = 0;
			apustatus &= ~2;
		}
		break;
	case NR23:
		rate(&sndch[1], reg[NR24] << 8 & 0x700 | v);
		break;
	case NR24:
		rate(&sndch[1], v << 8 & 0x700 | reg[NR23]);
		if((v & 1<<7) != 0)
			sndstart(&sndch[1], v);
		break;
	case NR30:
		if((v & 0x80) == 0){
			apustatus &= ~4;
			delevent(&chev[2]);
		}
		break;
	case NR31:
		sndch[2].len = 256 - (v & 0xff);
		break;
	case NR33:
		rate(&sndch[2], reg[NR34] << 8 & 0x700 | v);
		break;
	case NR34:
		rate(&sndch[2], v << 8 & 0x700 | reg[NR33]);
		if((v & 0x80) != 0){
			if(sndch[2].len == 0)
				sndch[2].len = 256;
			wpos = 0;
			if((reg[NR30] & 0x80) != 0){
				apustatus |= 4;
				delevent(&chev[2]);
				addevent(&chev[2], sndch[2].per);
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
		rate(&sndch[3], v);
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
	case NR50: case NR51:
		filter(0);
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
				delevent(&chev[2]);
			}
		}else if((reg[NR52] & 0x80) == 0){
			envmod = 0;
			delevent(&evenv);
			addevent(&evenv, FREQ / 512);
			sndch[0].ctr = 0;
			sndch[1].ctr = 0;
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
	addevent(&chev[0], 8 * 2048);
	addevent(&chev[1], 8 * 2048);
	addevent(&chev[3], 8 * 2048);
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
	rc = warp10 ? (sbufp - sbuf) * 2 : write(fd, sbuf, (sbufp - sbuf) * 2);
	if(rc > 0)
		sbufp -= (rc+1)/2;
	if(sbufp < sbuf)
		sbufp = sbuf;
	return 0;
}
