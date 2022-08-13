#include <u.h>
#include <libc.h>
#include <dtracy.h>

int
dtaunpackid(DTAgg *a)
{
	a->type = a->id >> 28 & 15;
	a->keysize = a->id >> 13 & 0x7ff8;
	switch(a->type){
	case AGGCNT:
	case AGGSUM:
	case AGGMIN:
	case AGGMAX:
		a->recsize = 8 + a->keysize + 8;
		return 0;
	case AGGAVG:
		a->recsize = 8 + a->keysize + 16;
		return 0;
	case AGGSTD:
		a->recsize = 8 + a->keysize + 32;
		return 0;
	default:
		return -1;
	}
}

static u64int
hash(uchar *s, int n, int m)
{
	u64int h;
	int i;
	
	h = 0xcbf29ce484222325ULL;
	for(i = 0; i < n; i++){
		h ^= s[i];
		h *= 0x100000001b3ULL;
	}
	for(; i < m; i++)
		h *= 0x100000001b3ULL;
	return h;
}

static int
keyeq(uchar *a, uchar *b, int n, int m)
{
	int i;
	
	for(i = 0; i < n; i++)
		if(a[i] != b[i])
			return 0;
	for(; i < m; i++)
		if(a[i] != 0)
			return 0;
	return 1;
}

/* calculate v*v with 128 bits precision and add it to the 128-bit word at q */
static void
addsquare(u64int *q, s64int v)
{
	u32int v0;
	s32int v1;
	s64int s0, s1, s2;
	u64int r;
	
	v0 = v;
	v1 = v>>32;
	s0 = (s64int)v0 * (s64int)v0;
	s1 = (s64int)v0 * (s64int)v1;
	s2 = (s64int)v1 * (s64int)v1;
	r = s0 + (s1<<33);
	if(r < (u64int)s0) q[1]++;
	q[0] += r;
	if(q[0] < r) q[1]++;
	q[1] += s2 + (s1>>31);
}

static void
updaterecord(int type, u64int *q, s64int val)
{
	switch(type){
	case AGGCNT: q[0] += 1; break;
	case AGGSUM: q[0] += val; break;
	case AGGAVG: q[0] += val; q[1]++; break;
	case AGGMIN: if(val < q[0]) q[0] = val; break;
	case AGGMAX: if(val > q[0]) q[0] = val; break;
	case AGGSTD: q[0] += val; q[1]++; addsquare(&q[2], val); break;
	}
}

static void
createrecord(int type, u64int *q, s64int val)
{
	switch(type){
	case AGGCNT: q[0] = 1; break;
	case AGGSUM: case AGGMIN: case AGGMAX: q[0] = val; break;
	case AGGAVG: q[0] = val; q[1] = 1; break;
	case AGGSTD: q[0] = val; q[1] = 1; q[2] = 0; q[3] = 0; addsquare(&q[2], val); break;
	}
}

/* runs in probe context */
void
dtarecord(DTChan *ch, int mach, DTAgg *a, uchar *key, int nkey, s64int val)
{
	u64int h;
	u32int *p, *q;
	DTBuf *c;
	
	c = ch->aggwrbufs[mach];
	h = hash(key, nkey, a->keysize);
	p = (u32int*)(c->data + DTABUCKETS + (h % DTANUMBUCKETS) * 4);
	while(*p != DTANIL){
		assert((uint)*p < DTABUCKETS);
		q = (u32int*)(c->data + *p);
		if(q[1] == a->id && keyeq((uchar*)(q + 2), key, nkey, a->keysize) == 0){
			updaterecord(a->type, (u64int*)(q + 2 + a->keysize / 4), val);
			return;
		}
		p = q;
	}
	if(c->wr + a->recsize > DTABUCKETS)
		return;
	*p = c->wr;
	q = (u32int*)(c->data + c->wr);
	q[0] = DTANIL;
	q[1] = a->id;
	if(nkey == a->keysize)
		memmove(&q[2], key, nkey);
	else if(nkey < a->keysize){
		memmove(&q[2], key, nkey);
		memset((uchar*)q + 8 + nkey, 0, a->keysize - nkey);
	}else
		memmove(&q[2], key, a->keysize);
	createrecord(a->type, (u64int*)(q + 2 + a->keysize / 4), val);
	c->wr += a->recsize;
}
