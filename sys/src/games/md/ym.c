#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int ym[512];
enum {
	MODE = 0x27,
	FDIV = 1048576 * 2 * 144,
};

#define min(a, b) ((a)<=(b)?(a):(b))

typedef struct oper oper;
typedef struct fm fm;

struct oper {
	enum {OFF, ATTACK, DECAY, SUSTAIN, RELEASE} st;
	u8int *r;
	int env;
	u8int ar, sr, dr, rs, rr, mult;
	u16int tl, sl;
	u8int keyon, keyoff, rate;
	int det;
	u8int kc;
	u32int phi, amp, dp;
	int val, val0;
};

struct fm {
	oper op[4];
	u8int st, alg, en, fbs;
	float samp;
} fms[6];
u8int ymstat;
static u32int cyc;
static u16int tima;
static u8int timb;

static short sbuf[2 * 2000], *sbufp;
static int fd;
static int sint[256], expt[256];

static void
timers(void)
{
	u8int m;
	static u8int bdiv;

	m = ym[0x27];
	if((m & 1) != 0){
		tima = (tima + 1) & 0x3ff;
		if(tima == 0 && (m & 4) != 0){
			ymstat |= 1;
			tima = ym[0x24] | ym[0x25] << 8 & 0x300;
		}
	}
	if(++bdiv == 8){
		bdiv = 0;
		if((m & 2) != 0){
			timb++;
			if(timb == 0 && (m & 8) != 0){
				ymstat |= 2;
				timb = ym[0x26];
			}
		}
	}
}

static void
calcfreq(int n)
{
	int i, fr;
	fm *f;
	oper *p;
	uchar *r;
	static u8int det[4][32] = {
		{0},
		{0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
		 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8},
		{1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5,
		 5, 6, 6, 7, 8, 8, 9,10,11,12,13,14,16,16,16,16},
		{2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
		 8, 8, 9,10,11,12,13,14,16,17,19,20,22,22,22,22}
	};		
	
	r = ym;
	f = &fms[n];
	if(n >= 3){
		n -= 3;
		r += 0x100;
	}
	fr = r[0xa0 + n] | r[0xa4 + n] << 8 & 0x3f00;
	for(i = 0; i < 3; i++){
		p = &f->op[i];
		if(n == 2 && (ym[MODE] & 0xc0) == 0x40 && i != 0)
			fr = r[0xa7+i] | r[0xab+i] << 8 & 0x3f00;
		p->kc = fr >> 9 & 0x1e;
		if((fr & 0x780) >= 0x380 && (fr & 0x780) != 0x400)
			p->kc |= 1;
		p->dp = ((fr & 0x7ff) << (fr >> 11)) >> 1;
		if((p->det & 4) != 0)
			p->dp -= det[p->det & 3][p->kc];
		else
			p->dp += det[p->det][p->kc];
		if(p->mult != 0)
			p->dp = (p->dp & 0x1ffff) * p->mult;
		else
			p->dp = (u16int)(p->dp >> 1);
		
	}
}

void
ymwrite(u8int a, u8int v, u8int c)
{
	int ch, i;
	oper *p;
	fm *f;

	ym[c ? a|0x100 : a] = v;
	if(a >= 0x30 && a < 0xa0){
		ch = a & 3;
		if(ch == 3)
			return;
		ch += c;
		f = &fms[ch];
		i = a >> 3 & 1 | a >> 1 & 2;
		p = &f->op[i];
		switch(a & 0xf0){
		case 0x30:
			p->mult = v & 0x0f;
			p->det = v >> 4 & 7;
			calcfreq(ch);
			break;
		case 0x40:
			p->tl = v << 3 & 0x3f8;
			break;
		case 0x50:
			p->ar = v << 1 & 0x3e;
			p->rs = 3 - (v >> 6);
			break;
		case 0x60:
			p->dr = v << 1 & 0x3e;
			break;
		case 0x70:
			p->sr = v << 1 & 0x3e;
			break;
		case 0x80:
			p->sl = v << 2 & 0x3c0;
			p->rr = v << 2 & 0x3c | 0x02;
			break;
		};
	}else{
		ch = c + (a & 3);
		switch(a){
		case MODE:
			if((v & 0x10) != 0)
				ymstat &= ~2;
			if((v & 0x20) != 0)
				ymstat &= ~1;
			calcfreq(2);
			calcfreq(5);
			break;
		case 0x28:
			ch = v & 3;
			if(ch == 3)
				break;
			if((v & 4) != 0)
				ch += 3;
			f = &fms[ch];
			for(i = 0; i < 4; i++){
				p = &f->op[i];
				if((v & 1<<4+i) != 0){
					if(p->st == OFF || p->st == RELEASE)
						p->keyon++;
				}else
					if(p->st != OFF)
						p->keyoff++;
			}
			break;
		case 0xa0: case 0xa1: case 0xa2:
		case 0xa4: case 0xa5: case 0xa6:
			calcfreq(ch);
			break;
		case 0xa8: case 0xa9: case 0xaa:
		case 0xac: case 0xad: case 0xae:
			calcfreq((a & 0x100) != 0 ? 5 : 2);
			break;
		case 0xb0: case 0xb1: case 0xb2:
			fms[ch].alg = v & 7;
			fms[ch].fbs = 7 - (v >> 3 & 7);
			break;
		case 0xb4: case 0xb5: case 0xb6:
			fms[ch].en = v;
			break;
		}
	}
}

static void
tables(void)
{
	int i;
	double x;
	
	for(i = 0; i < 256; i++){
		x = sin(((i << 1) + 1) * PI / 1024);
		x = -log(x)/log(2);
		sint[i] = x * 256 + 0.5;
	}
	for(i = 0; i < 256; i++){
		x = pow(2, -(i+1)/256.0);
		expt[i] = x * 2048 + 0.5;
	}
}

void
ymreset(void)
{
	int i, j;
	
	for(i = 0; i < 6; i++){
		fms[i].en = 0xc0;
		for(j = 0; j < 4; j++)
			fms[i].op[j].rs = 3;
	}
	tables();
}

static u8int
rate(oper *p, u8int r)
{
	if(r == 0)
		return 0;
	r += p->kc >> p->rs;
	if(r > 63)
		return 63;
	return r;
}

static void
env(oper *p)
{
	int v, sh, ai;
	static u8int ait[64][8] = {
		{0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0}, {0,1,0,1,0,1,0,1}, {0,1,0,1,0,1,0,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,0,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,0,1,1,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
		{0,1,0,1,0,1,0,1}, {0,1,0,1,1,1,0,1}, {0,1,1,1,0,1,1,1}, {0,1,1,1,1,1,1,1},
		{1,1,1,1,1,1,1,1}, {1,1,1,2,1,1,1,2}, {1,2,1,2,1,2,1,2}, {1,2,2,2,1,2,2,2},
		{2,2,2,2,2,2,2,2}, {2,2,2,4,2,2,2,4}, {2,4,2,4,2,4,2,4}, {2,4,4,4,2,4,4,4},
		{4,4,4,4,4,4,4,4}, {4,4,4,8,4,4,4,8}, {4,8,4,8,4,8,4,8}, {4,8,8,8,4,8,8,8},
		{8,8,8,8,8,8,8,8}, {8,8,8,8,8,8,8,8}, {8,8,8,8,8,8,8,8}, {8,8,8,8,8,8,8,8},
	};
	if(p->keyon > 0){
		p->st = ATTACK;
		p->rate = rate(p, p->ar);
		p->env = 0;
		p->keyon = 0;
	}else if(p->keyoff > 0){
		p->st = RELEASE;
		p->rate = rate(p, p->rr);
		p->keyoff = 0;
	}
	if(p->rate > 0){
		sh = p->rate >> 2;
		if(sh < 11)
			sh = 11 - sh;
		else
			sh = 0;
		if((cyc & (1 << sh) - 1) == 0){
			ai = ait[p->rate][(cyc >> sh) & 7];
			switch(p->st){
			case ATTACK:
				p->env += ai * ((1024 - p->env >> 4) + 1);
				if(p->env >= 1024){
					p->env = 0;
					p->st = DECAY;
					p->rate = rate(p, p->dr);
				}
				break;
			case DECAY:
				p->env += ai;
				if(p->env >= 1024)
					p->env = 1023;
				if(p->env >= p->sl){
					p->st = SUSTAIN;
					p->rate = rate(p, p->sr);
				}
				break;
			case SUSTAIN:
			case RELEASE:
				p->env += ai;
				if(p->env >= 1024){
					p->env = 1023;
					p->st = OFF;
					p->rate = 0;
				}
				break;
			}
		}
	}
	if(p->st == OFF){
		p->amp = 1023;
		return;
	}
	v = p->env;
	if(p->st == ATTACK)
		v ^= 1023;
	v += p->tl;
	if(v > 1023)
		p->amp = 1023;
	else
		p->amp = v;
}

static void
opr(oper *p, int inp)
{
	int v, x, r;

	p->phi += p->dp;
	v = (p->phi >> 10) + inp;
	if((v & 0x100) != 0)
		v ^= 0xff;
	x = sint[v & 0xff] + (p->amp << 2);
	r = expt[x & 0xff] << 2 >> (x >> 8);
	p->val0 = p->val;
	if((v & 0x200) != 0)
		p->val = -r;
	else
		p->val = r;
}

void
ymstep(void)
{
	static int ymch, ymop, ymcyc;
	fm *f;
	oper *p;
	int x;
	
	f = fms + ymch;
	p = f->op + ymop;
	x = 0;
	if(ymop == 0){
		if(f->fbs != 7)
			x = p->val + p->val0 >> f->fbs + 2;
	}else{
		switch(f->alg << 4 | ymop){
		default: x = p[-1].val; break;
		case 0x11: break;
		case 0x12: x = f->op[0].val + f->op[1].val; break;
		case 0x21: break;
		case 0x23: x = f->op[0].val + f->op[2].val; break;
		case 0x32: break;
		case 0x33: x = f->op[1].val + f->op[2].val; break;
		case 0x42: break;
		case 0x52: case 0x53: x = f->op[0].val; break;
		case 0x62: case 0x63: break;
		case 0x71: case 0x72: case 0x73: break;
		}
		x >>= 1;
	}
	if(ymcyc == 0)
		env(p);
	opr(p, x);
	if(ymop == 3){
		switch(f->alg){
		default: x = p->val >> 5; break;
		case 4: x = (f->op[1].val >> 5) + (p->val >> 5); break;
		case 5: case 6: x = (f->op[1].val >> 5) + (f->op[2].val >> 5) + (p->val >> 5); break;
		case 7: x = (f->op[0].val >> 5) + (f->op[1].val >> 5) + (f->op[2].val >> 5) + (p->val >> 5); break;
		}
		if(x > 256) x = 256;
		if(x < -256) x = -256;
		f->samp = x / 256.0;
		if(++ymch == 6){
			ymch = 0;
			if(++ymcyc == 3){
				cyc++;
				ymcyc = 0;
				timers();
			}
		}
		ymop = 0;
	}else
		ymop++;
}

void
audiosample(void)
{
	int i;
	u8int x;
	float v, vl, vr;
	
	if(sbufp == nil)
		return;
	vl = vr = 0;
	for(i = 0; i < 6; i++){
		if(i == 5 && (ym[0x2b] & 0x80) != 0)
			v = ym[0x2a] / 255.0;
		else
			v = fms[i].samp;
		x = fms[i].en;
		if((x & 0x80) != 0)
			vl += v;
		if((x & 0x40) != 0)
			vr += v;
	}
	if(sbufp < sbuf + nelem(sbuf) - 1){
		*sbufp++ = vl * 5000;
		*sbufp++ = vr * 5000;
	}
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

void
initaudio(void)
{
	fd = open("/dev/audio", OWRITE);
	if(fd < 0)
		return;
	sbufp = sbuf;
}
