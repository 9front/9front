#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static int fd;
int ch1c, ch2c, ch3c, ch4c, ch4sr = 1;

enum { SAMPLE = 44100 };

static int
thresh(int f, int b)
{
	switch(b){
	case 0: return f/8;
	case 1: return f/4;
	case 2: return f/2;
	default: return 3*f/4;
	}
}

static int
freq(int lower)
{
	int f;
	
	f = mem[lower+1] & 7;
	f = (f << 8) | mem[lower];
	f = muldiv(2048 - f, SAMPLE, 131072);
	return f;
}

static void
dosample(short *smp)
{
	int ch1s, ch2s, ch3s, ch4s, ch1f, ch2f, ch3f, ch4f, k, r, s;
	u8int f;

	ch4s = 0;
	
	ch1f = freq(0xFF13);
	if(ch1c >= ch1f)
		ch1c = 0;
	if(ch1c >= thresh(ch1f, mem[0xFF11] >> 6))
		ch1s = 1;
	else
		ch1s = -1;
	ch1s *= mem[0xFF12] >> 4;
	ch1s *= 10000 / 0xF;
	ch1c++;

	ch2f = freq(0xFF18);
	if(ch2c >= ch2f)
		ch2c = 0;
	if(ch2c >= thresh(ch1f, mem[0xFF16] >> 6))
		ch2s = 1;
	else
		ch2s = -1;
	ch2s *= mem[0xFF17] >> 4;
	ch2s *= 10000 / 0xF;
	ch2c++;
	
	ch3f = freq(0xFF1D) * 100 / 32;
	if(ch3f == 0)
		ch3f = 1;
	ch3s = 0;
	if(mem[0xFF1A] & 0x80){
		if(ch3c >= freq(0xFF1D))
			ch3c = 0;
		k = ch3c * 100 / ch3f;
		ch3s = mem[0xFF30 + (k >> 1)];
		if(k & 1)
			ch3s &= 0xF;
		else
			ch3s >>= 4;
		switch(mem[0xFF1C]){
		case 0:
			ch3s = 0;
			break;
		case 2:
			ch3s >>= 1;
			break;
		case 3:
			ch3s >>= 2;
			break;
		}
		ch3s *= 10000 / 0xF;
		ch3c++;	
	}
	
	r = mem[0xFF22] & 7;
	s = mem[0xFF22] >> 4;
	if(r != 0)
		ch4f = 524288 / r;
	else
		ch4f = 524288 * 2;
	ch4f >>= s+1;
	if(ch4f == 0)
		ch4f = 1;
	ch4f = SAMPLE / ch4f;
	if(ch4c >= ch4f){
		ch4sr <<= 1;
		if(mem[0xFF22] & 4)
			k = ((ch4sr >> 6) ^ (ch4sr >> 7)) & 1;
		else
			k = ((ch4sr >> 14) ^ (ch4sr >> 15)) & 1;
		ch4sr |= k;
		ch4c = 0;
	}
	if(ch4sr & 1)
		ch4s = -1;
	else
		ch4s = 1;
	ch4s *= mem[0xFF21] >> 4;
	ch4s *= 10000 / 0xF;
	
	smp[0] = 0;
	smp[1] = 0;
	f = mem[0xFF25];
	if(f & 0x01) smp[0] += ch1s;
	if(f & 0x02) smp[0] += ch2s;
	if(f & 0x04) smp[0] += ch3s;
	if(f & 0x08) smp[0] += ch4s;
	if(f & 0x10) smp[1] += ch1s;
	if(f & 0x20) smp[1] += ch2s;
	if(f & 0x40) smp[1] += ch3s;
	if(f & 0x80) smp[1] += ch4s;
}

void
audioproc(void *)
{
	short samples[10 * 2];
	int i;

	for(;;){
		for(i = 0; i < sizeof samples/4; i++)
			dosample(samples + 2 * i);
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
