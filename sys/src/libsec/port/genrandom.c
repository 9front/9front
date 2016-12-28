#include "os.h"
#include <libsec.h>

static void
init(Chachastate *cs)
{
	ulong seed[11];
	int i;

	for(i=0; i<nelem(seed); i++)
		seed[i] = truerand();

	setupChachastate(cs, (uchar*)&seed[0], 32, (uchar*)&seed[8], 12, 20);
	memset(seed, 0, sizeof(seed));
}

static void
fill(Chachastate *cs, uchar *p, int n)
{
	Chachastate c;

	c = *cs;
	chacha_encrypt((uchar*)&cs->input[4], 32, &c);
	if(++cs->input[13] == 0)
		if(++cs->input[14] == 0)
			++cs->input[15];

	chacha_encrypt(p, n, &c);
	memset(&c, 0, sizeof(c));
}

void
genrandom(uchar *p, int n)
{
	static QLock lk;
	static Chachastate cs;

	qlock(&lk);
	if(cs.rounds == 0)
		init(&cs);
	cs.input[4] ^= getpid();	/* fork protection */
	fill(&cs, p, n);
	qunlock(&lk);
}
