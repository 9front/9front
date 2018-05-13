#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

Event evsamp;
int srate, sratediv;
s16int sbuf[2*4000], *sbufp, bias;
enum {
	Freq = 44100,
};
static int stime;
s8int snddma[2];
static int fd;

u16int envctr, envrel, envmod;
u8int sweepen, sweepctr;
u16int sweepfreq;
typedef struct chan chan;
struct chan {
	u8int n, ectr;
	u16int len;
	u16int *env, *freq;
	u16int fctr, fthr;
	u32int finc;
	u8int vol;
};
u8int wave[64], wpos, wbank;
u16int lfsr;

chan sndch[4] = {
	{
		.n = 0,
		.env = reg + 0x62/2,
		.freq = reg + 0x64/2,
	},
	{
		.n = 1,
		.env = reg + 0x68/2,
		.freq = reg + 0x6c/2,
	},
	{
		.n = 2,
	},
	{
		.n = 3,
		.env = reg + 0x78/2,
		.freq = reg + 0x7c/2,
	}
};

Var apuvars[] = {
	ARR(snddma), VAR(envctr), VAR(envrel), VAR(envmod), VAR(sweepen),
	VAR(sweepctr), VAR(sweepfreq), ARR(wave), VAR(wpos), VAR(wbank), VAR(lfsr),
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
		sndch[i].finc = 131072ULL * 65536 / (srate * (2048 - (v & 0x7ff)));
		break;
	case 2:
		sndch[2].finc = 2097152ULL * 65536 / (srate * (2048 - (v & 0x7ff)));
		break;
	case 3:
		sndch[3].finc = 524288ULL * 65536 / srate;
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
	if((envmod & 1) == 0 && c->len > 0 && (*c->freq & 1<<14) != 0)
		--c->len;
	if(c->len == 0){
		c->vol = 0;
		return;
	}
	if((envmod & 7) != 7 || c->ectr == 0 || --c->ectr != 0)
		return;
	c->ectr = *c->env >> 8 & 7;
	if((*c->env & 1<<11) != 0){
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
	u16int vol, cnt;
	int v;

	sndch[2].fctr = v = sndch[2].fctr + sndch[2].finc;
	if(sndch[2].len == 0 || (reg[0x70/2] & 1<<7) == 0)
		return 0;
	vol = reg[0x72/2];
	cnt = reg[0x70/2];
	for(;;){
		x = wave[wbank ^ wpos];
		v -= 0x10000;
		if(v < 0)
			break;
		wpos++;
		if((cnt & 1<<5) != 0)
			wpos &= 63;
		else
			wpos &= 31;
	}
	if((vol & 1<<15) != 0)
		x = (x >> 2) + (x >> 3);
	else if((vol & 3<<14) == 0)
		x = 0;
	else
		x = x >> (vol >> 14 & 3);
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
	
	cnt = reg[0x60/2];
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
		reg[0x64/2] = reg[0x64/2] & 0xfc00 | fr;
		rate(0, fr);
		sweep(0);
	}
}

void
sndstart(chan *c, u16int v)
{
	u16int cnt;

	c->vol = *c->env >> 12;
	c->ectr = *c->env >> 8 & 7;
	if(c->len == 0)
		c->len = 64;
	if(c == sndch){
		cnt = reg[0x60/2];
		sweepen = (cnt & 0x07) != 0 && (cnt & 0x70) != 0;
		sweepctr = cnt >> 4 & 7;
		sweepfreq = v & 0x7ff;
		if((cnt & 0x07) != 0)
			sweep(0);
	}
}

void
sampletick(void *)
{
	u16int cntl, cnth;
	s16int ch[6];
	s16int s[2];
	int i;
	
	addevent(&evsamp, sratediv);
	
	if(--envctr == 0){
		envctr = envrel;
		env(&sndch[0]);
		env(&sndch[1]);
		if((envmod & 1) == 0 && sndch[2].len > 0 && (reg[0x74/2] & 1<<14) != 0)
			sndch[2].len--;
		env(&sndch[3]);
		if((envmod & 3) == 2 && sweepen && --sweepctr == 0){
			sweepctr = reg[0x60/2] >> 4 & 7;
			sweep(1);
		}
		envmod++;
	}
	
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
	
	cntl = reg[SOUNDCNTL];
	cnth = reg[SOUNDCNTH];
	for(i = 0; i < 4; i++)
		ch[i] = ch[i] >> (cnth & 3);
	ch[5] = snddma[0] << 1 + (cnth >> 2 & 1);
	ch[6] = snddma[1] << 1 + (cnth >> 3 & 1);
	
	s[0] = 0;
	s[1] = 0;
	for(i = 0; i < 4; i++){
		if((cntl & 1<<8<<i) != 0)
			s[1] += ch[i] * (1 + (cntl & 7));
		if((cntl & 1<<12<<i) != 0)
			s[0] += ch[i] * (1 + (cntl >> 4 & 7));
	}
	for(i = 5; i < 6; i++){
		if((cnth & 1<<3<<i) != 0)
			s[1] += ch[i];
		if((cnth & 1<<4<<i) != 0)
			s[0] += ch[i]; 
	}
	s[0] += bias;
	s[1] += bias;
	if(s[0] < -0x200) s[0] = -0x200;
	else if(s[0] > 0x1ff) s[0] = 0x1ff;
	if(s[1] < -0x200) s[1] = -0x200;
	else if(s[1] > 0x1ff) s[1] = 0x1ff;
	
	stime -= Freq;
	while(stime < 0){
		if(sbufp < sbuf + nelem(sbuf)){
			sbufp[0] = s[0] << 6;
			sbufp[1] = s[1] << 6;
			sbufp += 2;
		}
		stime += srate;
	}
}


void
sndwrite(u16int a, u16int v)
{
	int sh, p, i;
	static u16int thr[4] = {0x2000, 0x4000, 0x8000, 0xC000};
	
	switch(a){
	case 0x62:
		sndch[0].fthr = thr[v >> 6 & 3];
		sndch[0].len = 64 - (v & 63);
		break;
	case 0x64:
		rate(0, v);
		if((v & 1<<15) != 0)
			sndstart(&sndch[0], v);
		break;
	case 0x68:
		sndch[1].fthr = thr[v >> 6 & 3];
		break;
	case 0x6c:
		rate(1, v);
		if((v & 1<<15) != 0)
			sndstart(&sndch[1], v);
		break;
	case 0x70:
		wbank = v >> 1 & 32;
		break;
	case 0x72:
		sndch[2].len = 256 - (v & 0xff);
		break;
	case 0x74:
		rate(2, v);
		if((v & 1<<15) != 0 && sndch[2].len == 0)
			sndch[2].len = 256;
		break;
	case 0x7c:
		rate(3, v);
		if((v & 1<<15) != 0){
			if((v & 1<<3) != 0)
				lfsr = 0x7f;
			else
				lfsr = 0x7fff;
			sndstart(&sndch[3], v);
		}
		break;
	case SOUNDBIAS*2:
		sh = 9 - (v >> 14 & 3);
		if(sratediv != 1<<sh){
			srate = 1 << 24 - sh;
			sratediv = 1 << sh;
			envrel = srate / 512;
			rate(0, reg[0x64/2]);
			rate(1, reg[0x6c/2]);
			rate(2, reg[0x74/2]);
			rate(3, reg[0x7c/2]);
		}
		bias = (v & 0x3ff) - 0x200;
		break;
	case 0x90: case 0x92: case 0x94: case 0x96:
	case 0x98: case 0x9a: case 0x9c: case 0x9e:
		p = ~reg[0x70/2] >> 1 & 32;
		for(i = a - 0x90; i < a - 0x90 + 2; i++){
			wave[(wpos + 2 * i) & 31 + p] = v >> 4 & 0xf;
			wave[(wpos + 2 * i + 1) & 31 + p] = v & 0xf;
			v >>= 8;
		}
		break;
	}
}

void
audioinit(void)
{
	fd = open("/dev/audio", OWRITE);
	if(fd < 0)
		sysfatal("open: %r");
	sbufp = sbuf;
	sndwrite(SOUNDBIAS*2, 0x200);
	evsamp.f = sampletick;
	addevent(&evsamp, sratediv);
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
