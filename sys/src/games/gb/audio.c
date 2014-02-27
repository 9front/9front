#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static int fd;
static int sc, ch1c, ch2c, ch3c, ch4c, ch4sr = 1, ch1vec, ch2vec, ch4vec, ch1v, ch2v, ch4v;
extern int paused;

static short sbuf[2*2000], *sbufp;

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
soundlen(int len, int ctrl, int n)
{
	if(mem[ctrl] & 128){
		mem[0xFF26] |= (1<<n);
		mem[ctrl] &= ~128;
		switch(n){
		case 0:
			ch1v = mem[0xFF12];
			break;
		case 1:
			ch2v = mem[0xFF17];
			break;
		case 3:
			ch4v = mem[0xFF21];
			break;
		}
	}
	if((mem[ctrl] & 64) == 0){
		mem[0xFF26] |= (1<<n);
		return;
	}
	if((mem[0xFF26] & (1<<n)) == 0)
		return;
	if(mem[len] == ((n == 2) ? 255 : 63)){
		mem[0xFF26] &= ~(1<<n);
		return;
	}
	mem[len]++;
}

static void
envelope(int *v, int *c)
{
	int f;
	
	f = (*v & 7) * SAMPLE / 64;
	if(f == 0)
		return;
	if(*c >= f){
		if(*v & 8){
			if((*v >> 4) < 0xF)
				*v += 0x10;
		}else
			if((*v >> 4) > 0)
				*v -= 0x10;
		*c = 0;
	}
	(*c)++;
}

void
audiosample(void)
{
	int ch1s, ch2s, ch3s, ch4s, ch1f, ch2f, ch3f, ch4f, k, r, s;
	u8int f;
	
	if(sbufp == nil)
		return;
	if(sc >= SAMPLE/256){
		soundlen(0xFF11, 0xFF14, 0);
		soundlen(0xFF16, 0xFF19, 1);
		soundlen(0xFF1B, 0xFF1E, 2);
		soundlen(0xFF20, 0xFF23, 3);
		sc = 0;
	}
	sc++;
	envelope(&ch1v, &ch1vec);
	envelope(&ch2v, &ch2vec);
	envelope(&ch4v, &ch4vec);

	ch1f = freq(0xFF13);
	if(ch1c >= ch1f)
		ch1c = 0;
	if(ch1c >= thresh(ch1f, mem[0xFF11] >> 6))
		ch1s = 1;
	else
		ch1s = -1;
	ch1s *= ch1v >> 4;
	ch1s *= 8000 / 0xF;
	ch1c++;

	ch2f = freq(0xFF18);
	if(ch2c >= ch2f)
		ch2c = 0;
	if(ch2c >= thresh(ch1f, mem[0xFF16] >> 6))
		ch2s = 1;
	else
		ch2s = -1;
	ch2s *= ch2v >> 4;
	ch2s *= 8000 / 0xF;
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
		ch3s *= 8000 / 0xF;
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
	ch4c++;
	if(ch4sr & 1)
		ch4s = -1;
	else
		ch4s = 1;
	ch4s *= ch4v >> 4;
	ch4s *= 8000 / 0xF;
	
	f = mem[0xFF25];
	r = mem[0xFF26] & 15;
	r = r | (r << 4);
	f &= r;
	if(sbufp < sbuf + nelem(sbuf) - 1){
		*sbufp = 0;
		if(f & 0x01) *sbufp += ch1s;
		if(f & 0x02) *sbufp += ch2s;
		if(f & 0x04) *sbufp += ch3s;
		if(f & 0x08) *sbufp += ch4s;
		*++sbufp = 0;
		if(f & 0x10) *sbufp += ch1s;
		if(f & 0x20) *sbufp += ch2s;
		if(f & 0x40) *sbufp += ch3s;
		if(f & 0x80) *sbufp += ch4s;
		sbufp++;
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
	rc = write(fd, sbuf, (sbufp - sbuf) * 2);
	if(rc > 0)
		sbufp -= (rc+1)/2;
	if(sbufp < sbuf)
		sbufp = sbuf;
	return 0;
}

void
initaudio(void)
{
	mem[0xFF26] = 0xF;
	ch1v = 0xF0;
	ch2v = 0xF0;
	ch4v = 0xF0;
	fd = open("/dev/audio", OWRITE);
	if(fd < 0)
		return;
	sbufp = sbuf;
}
