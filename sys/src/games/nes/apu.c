#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

enum { MAXBUF = 2000 };

enum {
	LEN = 0,
	ENV = 4,
	TRILIN = 6,
	SWEEP = 8,
	RELOAD = 10,
	DMCCTR = 11,
	DMCSHFT = 12,
};
u8int apuseq, apuctr[13];
u16int dmcaddr, dmccnt;
static int fd;
static short sbuf[2*MAXBUF], *sbufp;

int
targperiod(int i)
{
	int m, p, t;
	
	m = mem[0x4001 + i * 4];
	p = mem[0x4002 + i * 4];
	p |= (mem[0x4003 + i * 4] & 7) << 8;
	t = p >> (m & 7);
	if((m & 8) != 0){
		if(i == 0 && t != 0)
			t--;
		t = p - t;
	}else
		t += p;
	return t;
}

static void
declen(void)
{
	int i, m, p;
	u8int *a;
	
	for(i = 0; i < 4; i++){
		m = mem[0x4000 + i * 4];
		if(i == 2)
			m >>= 2;
		if((m & 0x20) != 0)
			continue;
		if(apuctr[LEN + i] != 0)
			apuctr[LEN + i]--;
	}
	for(i = 0, a = apuctr + SWEEP; i < 2; i++, a++){
		m = mem[0x4001 + i * 4];
		if((m & 0x80) != 0 && (m & 0x07) != 0 && (*a & 7) == 0){ 
			p = targperiod(i);
			if(p <= 0x7ff){
				mem[0x4002 + i * 4] = p;
				mem[0x4003 + i * 4] = p >> 8;
			}
		}
		if((*a & 0x80) != 0 || (*a & 7) == 0 && (m & 0x80) != 0)
			*a = (m & 0x70) >> 4;
		else if(*a != 0)
			(*a)--;
	}
}

static void
doenv(void)
{
	int i, m;
	u8int *a;
	
	for(i = 0, a = apuctr + ENV; i < 4; i++, a++){
		if(i == 2)
			continue;
		m = mem[0x4000 + 4 * i];
		if((apuctr[RELOAD] & (1<<i)) != 0){
			*a = 0xf0 | m & 0x0f;
			apuctr[RELOAD] &= ~(1<<i);
		}else if((*a & 0x0f) == 0){
			*a |= m & 0x0f;
			if((*a & 0xf0) == 0){
				if((m & 0x20) != 0)
					*a |= 0xf0;
			}else
				*a -= 0x10;
		}else
			(*a)--;
	}
	a = apuctr + TRILIN;
	if((apuctr[RELOAD] & (1<<2)) != 0)
		*a = mem[0x4008] & 0x7f;
	else if(*a != 0)
		(*a)--;
	if((mem[0x4008] & 0x80) == 0)
		apuctr[RELOAD] &= ~(1<<2);
}

void
apustep(void)
{
	int mode, len, env;
	
	mode = mem[APUFRAME];
	if((mode & 0x80) != 0){
		if(apuseq >= 4){
			env = len = 0;
			apuseq = 0;
		}else{
			env = 1;
			len = (apuseq & 1) == 0;
			apuseq++;
		}
	}else{
		env = 1;
		len = (apuseq & 1) != 0;
		if(apuseq >= 3){
			if((mode & 0x40) == 0)
				irq |= IRQFRAME;
			apuseq = 0;
		}else
			apuseq++;
	}
	if(len)
		declen();
	if(env)
		doenv();
}

static int
freq(int i)
{
	int f;
	
	f = mem[0x4002 + 4 * i];
	f |= (mem[0x4003 + 4 * i] & 0x7) << 8;
	return f;
}

static int
pulse(int i)
{
	static int c[2];
	int m, s, f;

	f = freq(i);
	if(f < 8 || targperiod(i) > 0x7ff)
		f = -1;
	else
		f = muldiv(16 * (f + 1), RATE, FREQ/12);
	if(c[i] >= f)
		c[i] = 0;
	else
		c[i]++;
	m = mem[0x4000 + 4 * i];
	if((m & 0x10) != 0)
		s = m & 0x0f;
	else
		s = apuctr[ENV + i] >> 4;
	if(c[i] >= f/2 || apuctr[LEN + i] == 0)
		s = 0;
	return s;
}

static int
tri(void)
{
	static int c;
	int f, i;
	
	f = freq(2);
	if(f <= 2)
		return 7;
	f = muldiv(32 * (f + 1), RATE, FREQ / 12);
	if(c >= f)
		c = 0;
	else
		c++;
	i = 32 * c / f;
	i ^= (i < 16) ? 0xf : 0x10;
	if(apuctr[LEN + 2] == 0 || (apuctr[TRILIN] & 0x7f) == 0)
		return 0;
	return i;
}

static int
noise(void)
{
	static int c, r=1;
	int m, f;
	static int per[] = {
		0x004, 0x008, 0x010, 0x020, 0x040, 0x060, 0x080, 0x0A0,
		0x0CA, 0x0FE, 0x17C, 0x1FC, 0x2FA, 0x3F8, 0x7F2, 0xFE4,
	};

	m = mem[0x400E];
	f = muldiv(per[m & 0x0f], RATE * 1000, FREQ/24);
	c += 1000;
	while(c >= f){
		r |= ((r ^ (r >> ((m & 0x80) != 0 ? 6 : 1))) & 1) << 15;
		r >>= 1;
		c -= f;
	}
	if(apuctr[LEN + 3] == 0 || (r & 1) != 0)
		return 0;
	m = mem[0x400C];
	if((m & 0x10) != 0)
		return m & 0xf;
	return apuctr[ENV + 3] >> 4;
}

static int
dmc(void)
{
	return mem[DMCBUF];
}

void
audiosample(void)
{
	double d;
	
	if(sbufp == nil)
		return;
	d = 95.88 / (8128.0 / (0.01 + pulse(0) + pulse(1)) + 100);
	d += 159.79 / (1.0 / (0.01 + tri()/8227.0 + noise()/12241.0 + dmc()/22638.0) + 100.0);
	if(sbufp < sbuf + nelem(sbuf) - 1){
		*sbufp++ = d * 10000;
		*sbufp++ = d * 10000;
	}
}

void
dmcstep(void)
{
	if((apuctr[DMCCTR] & 7) == 0){
		apuctr[DMCCTR] = 8;
		if(dmccnt != 0){
			apuctr[DMCSHFT] = memread(dmcaddr);
			if(dmcaddr == 0xFFFF)
				dmcaddr = 0x8000;
			else
				dmcaddr++;
			if(--dmccnt == 0){
				if((mem[DMCCTRL] & 0x40) != 0){
					dmcaddr = mem[DMCADDR] * 0x40 + 0xC000;
					dmccnt = mem[DMCLEN] * 0x10 + 1;
				}else if((mem[DMCCTRL] & 0x80) != 0)
					irq |= IRQDMC;
			}
		}else
			apuctr[DMCCTR] |= 0x80;
	}
	if((apuctr[DMCCTR] & 0x80) == 0){
		if((apuctr[DMCSHFT] & 1) != 0){
			if(mem[DMCBUF] < 126)
				mem[DMCBUF] += 2;
		}else{
			if(mem[DMCBUF] > 1)
				mem[DMCBUF] -= 2;
		}
	}
	apuctr[DMCSHFT] >>= 1;
	apuctr[DMCCTR]--;
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

u8int apulen[32] = {
	0x0A, 0xFE, 0x14, 0x02, 0x28, 0x04, 0x50, 0x06,
	0xA0, 0x08, 0x3C, 0x0A, 0x0E, 0x0C, 0x1A, 0x0E,
	0x0C, 0x10, 0x18, 0x12, 0x30, 0x14, 0x60, 0x16,
	0xC0, 0x18, 0x48, 0x1A, 0x10, 0x1C, 0x20, 0x1E,
};

u16int dmclen[16] = {
	0x1AC, 0x17C, 0x154, 0x140, 0x11E, 0x0FE, 0x0E2, 0x0D6,
	0x0BE, 0x0A0, 0x08E, 0x080, 0x06A, 0x054, 0x048, 0x036,
};
