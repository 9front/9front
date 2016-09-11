#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<libsec.h>

/* machine specific hardware random number generator */
void (*hwrandbuf)(void*, ulong) = nil;

static struct
{
	QLock;
	Chachastate;
} *rs;

typedef struct Seedbuf Seedbuf;
struct Seedbuf
{
	ulong		randomcount;
	uchar		buf[64];
	uchar		nbuf;
	uchar		next;
	ushort		bits;

	SHA2_512state	ds;
};

static void
randomsample(Ureg*, Timer *t)
{
	Seedbuf *s = t->ta;

	if(s->randomcount == 0 || s->nbuf >= sizeof(s->buf))
		return;
	s->bits = (s->bits<<2) ^ s->randomcount;
	s->randomcount = 0;
	if(++s->next < 8/2)
		return;
	s->next = 0;
	s->buf[s->nbuf++] ^= s->bits;
}

static void
randomseed(void*)
{
	Seedbuf *s;

	s = secalloc(sizeof(Seedbuf));

	if(hwrandbuf != nil)
		(*hwrandbuf)(s->buf, sizeof(s->buf));

	/* Frequency close but not equal to HZ */
	up->tns = (vlong)(MS2HZ+3)*1000000LL;
	up->tmode = Tperiodic;
	up->tt = nil;
	up->ta = s;
	up->tf = randomsample;
	timeradd(up);
	while(s->nbuf < sizeof(s->buf)){
		if(++s->randomcount <= 100000)
			continue;
		if(anyhigher())
			sched();
	}
	timerdel(up);

	sha2_512(s->buf, sizeof(s->buf), s->buf, &s->ds);
	setupChachastate(rs, s->buf, 32, s->buf+32, 12, 20);
	qunlock(rs);

	secfree(s);

	pexit("", 1);
}

void
randominit(void)
{
	rs = secalloc(sizeof(*rs));
	qlock(rs);	/* randomseed() unlocks once seeded */
	kproc("randomseed", randomseed, nil);
}

ulong
randomread(void *p, ulong n)
{
	Chachastate c;

	if(n == 0)
		return 0;

	if(hwrandbuf != nil)
		(*hwrandbuf)(p, n);

	/* copy chacha state, rekey and increment iv */
	qlock(rs);
	c = *rs;
	chacha_encrypt((uchar*)&rs->input[4], 32, &c);
	if(++rs->input[13] == 0)
		if(++rs->input[14] == 0)
			++rs->input[15];
	qunlock(rs);

	/* encrypt the buffer, can fault */
	chacha_encrypt((uchar*)p, n, &c);

	/* prevent state leakage */
	memset(&c, 0, sizeof(c));

	return n;
}

/* used by fastrand() */
void
genrandom(uchar *p, int n)
{
	randomread(p, n);
}

/* used by rand(),nrand() */
long
lrand(void)
{
	/* xoroshiro128+ algorithm */
	static int seeded = 0;
	static uvlong s[2];
	static Lock lk;
	ulong r;

	if(seeded == 0){
		randomread(s, sizeof(s));
		seeded = (s[0] | s[1]) != 0;
	}

	lock(&lk);
	r = (s[0] + s[1]) >> 33;
	s[1] ^= s[0];
 	s[0] = (s[0] << 55 | s[0] >> 9) ^ s[1] ^ (s[1] << 14);
 	s[1] = (s[1] << 36 | s[1] >> 28);
	unlock(&lk);

 	return r;
}
