#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static int sdiv[2], fdiv[2], cdiv[2], ch[2], sr[2] = {-1,-1};
static short sbuf[2000*2], *sbufp;
static int audfd;

#define div(n) if(++cdiv[i] < n) break; cdiv[i] = 0

static void
channel(int i)
{
	sdiv[i] += HZ/114;
	for(; sdiv[i] >= RATE; sdiv[i] -= RATE)
		if(fdiv[i] >= (reg[AUDF0 + i] & 31)){
			fdiv[i] = 0;
			switch(reg[AUDC0 + i] & 15){
			case 0: ch[i] = 1; break;
			case 2: div(15);
			case 1: ch[i] = sr[i] & 1; sr[i] = sr[i] >> 1 & 7 | (sr[i] << 2 ^ sr[i] << 3) & 8; break;
			case 4: case 5: ch[i] ^= 1; break;
			case 12: case 13: div(3); ch[i] ^= 1; break;
			case 6: case 10: div(16); ch[i] ^= 1; break;
			case 14: div(46); ch[i] ^= 1; break;
			case 15: div(3);
			case 7: case 9: ch[i] = sr[i] & 1; sr[i] = sr[i] >> 1 & 15 | (sr[i] << 2 ^ sr[i] << 4) & 16; break;
			case 8: ch[i] = sr[i] & 1; sr[i] = sr[i] >> 1 & 255 | (sr[i] << 4 ^ sr[i] << 8) & 256; break;
			case 3:
				ch[i] = sr[i] & 1;
				sr[i] = sr[i] & 15 | sr[i] >> 1 & 240 | (sr[i] << 2 ^ sr[i] << 3) & 256;
				if((sr[i] & 256) != 0)
					sr[i] = sr[i] & 496 | sr[i] >> 1 & 7 | (sr[i] << 2 ^ sr[i]) << 3 & 8;
				break;
			}
		}else
			fdiv[i]++;
}

void
sample(void)
{
	int d;

	if(sbufp == nil)
		return;
	channel(0);
	channel(1);
	d = ch[0] * (reg[AUDV0] & 15) + ch[1] * (reg[AUDV1] & 15);
	if(sbufp < sbuf + nelem(sbuf) - 1){
		*sbufp++ = d * 1000;
		*sbufp++ = d * 1000;
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
	rc = write(audfd, sbuf, (sbufp - sbuf) * 2);
	if(rc > 0)
		sbufp -= (rc+1)/2;
	if(sbufp < sbuf)
		sbufp = sbuf;
	return 0;
}

void
initaudio(void)
{
	audfd = open("/dev/audio", OWRITE);
	if(audfd < 0)
		return;
	sbufp = sbuf;
}
