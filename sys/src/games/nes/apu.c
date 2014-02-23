#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

u8int apuseq, apuctr[10];
static int fd;

enum { RATE = 44100 };

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
		if(apuctr[i] != 0)
			apuctr[i]--;
	}
	for(i = 0, a = apuctr + 8; i < 2; i++, a++){
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
	
	for(i = 0, a = apuctr + 4; i < 4; i++, a++){
		if(i == 2)
			continue;
		m = mem[0x4000 + 4 * i];
		if((*a & 0x80) != 0)
			*a = *a & 0x70 | 0x0f;
		else if(*a == 0){
			if((m & 0x20) != 0)
				*a |= 0x0f;
		}else
			(*a)--;
	}
	a = apuctr + 6;
	if((*a & 0x80) != 0)
		*a = mem[0x4008];
	else if(*a != 0)
		(*a)--;
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
		s = m;
	else
		s = apuctr[i+4];
	s &= 0x0f;
	if(c[i] >= f/2 || apuctr[i] == 0)
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
	if(apuctr[2] == 0 || (apuctr[6] & 0x7f) == 0)
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
	if(apuctr[3] == 0 || (r & 1) != 0)
		return 0;
	m = mem[0x400C];
	if((m & 0x10) != 0)
		return m & 0xf;
	return apuctr[7] & 0xf;
}

static int
dmc(void)
{
	return 0;
}

static void
sample(short *s)
{
	double d;
	
	d = 95.88 / (8128.0 / (0.01 + pulse(0) + pulse(1)) + 100);
	d += 159.79 / (1.0 / (0.01 + tri()/8227.0 + noise()/12241.0 + dmc()/22638.0) + 100.0);
	*s++ = d * 20000;
	*s = d * 20000;
}

static void
audioproc(void *)
{
	static short samples[500 * 2];
	int i;

	for(;;){
		if(paused)
			memset(samples, 0, sizeof samples);
		else
			for(i = 0; i < sizeof samples/4; i++)
				sample(samples + 2 * i);
		write(fd, samples, sizeof samples);
	}
}

void
initaudio(void)
{
	fd = open("/dev/audio", OWRITE);
	if(fd < 0)
		return;
	proccreate(audioproc, nil, 8192);
}

u8int apulen[32] = {
	0x0A, 0xFE, 0x14, 0x02, 0x28, 0x04, 0x50, 0x06,
	0xA0, 0x08, 0x3C, 0x0A, 0x0E, 0x0C, 0x1A, 0x0E,
	0x0C, 0x10, 0x18, 0x12, 0x30, 0x14, 0x60, 0x16,
	0xC0, 0x18, 0x48, 0x1A, 0x10, 0x1C, 0x20, 0x1E,
};
