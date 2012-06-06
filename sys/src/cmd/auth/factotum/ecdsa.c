#include "dat.h"

enum {
	CHaveKey,
	CHaveText,
	Maxphase
};

static ECdomain dom;

static char *phasenames[] = {
	"CHaveKey",
	"CHaveText",
};

struct State {
	ECpriv p;
	char *sign;
};

static int
decryptkey(Fsstate *fss, char *key, char *password)
{
	uchar keyenc[53], hash[32], ivec[AESbsize];
	AESstate s;
	State *st;
	char buf[100];

	if(base58dec(key, keyenc, 53) < 0)
		return failure(fss, "invalid base58");
	sha2_256((uchar *)password, strlen(password), hash, nil);
	sha2_256(hash, 32, hash, nil);
	genrandom(ivec, sizeof ivec);
	setupAESstate(&s, hash, 32, keyenc+37);
	aesCBCdecrypt(keyenc, 37, &s);
	memset(buf, 0, sizeof buf);
	base58enc(keyenc, buf, 37);
	if(keyenc[0] != 0x80)
		return failure(fss, "invalid key '%s'", buf);
	sha2_256(keyenc, 33, hash, nil);
	sha2_256(hash, 32, hash, nil);
	if(memcmp(keyenc + 33, hash, 4) != 0)
		return failure(fss, "checksum error");
	st = fss->ps;
	st->p.d = betomp(keyenc + 1, 32, nil);
	st->p.x = mpnew(0);
	st->p.y = mpnew(0);
	ecmul(&dom, dom.G, st->p.d, &st->p);
	return RpcOk;
}

static int
ecdsainit(Proto *, Fsstate *fss)
{
	int iscli;
	Key *k;
	Keyinfo ki;
	int ret;
	char *key, *password;
	Attr *attr;

	if(dom.p == nil){
		dom.p = strtomp("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F", nil, 16, nil);
		dom.a = uitomp(0, nil);
		dom.b = uitomp(7, nil);
		dom.n = strtomp("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141", nil, 16, nil);
		dom.h = uitomp(1, nil);
		dom.G = strtoec(&dom, "0279BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798", nil, nil);
	}
	if((iscli = isclient(_strfindattr(fss->attr, "role"))) < 0)
		return failure(fss, nil);
	if(iscli==0)
		return failure(fss, "ecdsa server unimplemented");
	mkkeyinfo(&ki, fss, nil);
	ret = findkey(&k, &ki, "key? !password?");
	if(ret == RpcOk){
		key = _strfindattr(k->attr, "key");
		password = _strfindattr(k->privattr, "!password");

	}else{
		if(!_strfindattr(fss->attr, "dom"))
			return ret;
		attr = _copyattr(fss->attr);
		_delattr(attr, "key");
		mkkeyinfo(&ki, fss, attr);
		ret = findkey(&k, &ki, "dom? !password?");
		if(ret != RpcOk)
			return ret;
		key = _strfindattr(fss->attr, "key");
		password = _strfindattr(k->privattr, "!password");
	}
	if(key == nil || password == nil)
		return RpcNeedkey;
	fss->ps = malloc(sizeof(State));
	ret = decryptkey(fss, key, password);
	if(ret != RpcOk)
		return ret;
	
	setattrs(fss->attr, k->attr);
	fss->phasename = phasenames;
	fss->maxphase = Maxphase;
	fss->phase = CHaveKey;
	return RpcOk;
}

static char *
derencode(mpint *r, mpint *s)
{
	uchar buf[100], rk[33], sk[33];
	char *str;
	int rl, sl, i;
	
	mptobe(r, rk, 32, nil);
	mptobe(s, sk, 32, nil);
	rl = (mpsignif(r) + 7)/8;
	sl = (mpsignif(s) + 7)/8;
	if(rk[0] & 0x80){
		memmove(rk + 1, rk, 32);
		rk[0] = 0;
		rl++;
	}
	if(sk[0] & 0x80){
		memmove(sk + 1, sk, 32);
		sk[0] = 0;
		sl++;
	}
	buf[0] = 0x30;
	buf[1] = 4 + rl + sl;
	buf[2] = 0x02;
	buf[3] = rl;
	memmove(buf + 4, rk, rl);
	buf[4 + rl] = 0x02;
	buf[5 + rl] = sl;
	memmove(buf + 6 + rl, sk, sl);
	str = malloc(1024);
	for(i = 0; i < 6 + rl + sl; i++)
		sprint(str + 2 * i, "%.2x", buf[i]);
	return str;
}

static int
ecdsawrite(Fsstate *fss, void *va, uint n)
{
	State *st;
	uchar hash[32];
	mpint *r, *s;
	
	st = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "write");
	case CHaveKey:
		sha2_256(va, n, hash, nil);
		sha2_256(hash, 32, hash, nil);
		r = mpnew(0);
		s = mpnew(0);
		ecdsasign(&dom, &st->p, hash, 32, r, s);
		st->sign = derencode(r, s);
		mpfree(r);
		mpfree(s);
		fss->phase = CHaveText;
		return RpcOk;
	}
}

static int
ecdsaread(Fsstate *fss, void *va, uint *n)
{
	switch(fss->phase){
	default:
		return phaseerror(fss, "read");
	case CHaveText:
		*n = snprint(va, *n, ((State *)fss->ps)->sign);
		fss->phase = Established;
		return RpcOk;
	}
}

static void
ecdsaclose(Fsstate *fss)
{
	State *st;
	
	st = fss->ps;
	if(st->p.x != nil){
		mpfree(st->p.x);
		mpfree(st->p.y);
		mpfree(st->p.d);
	}
	if(st->sign != nil)
		free(st->sign);
	free(st);
	fss->ps = nil;
}

Proto ecdsa = {
	.name = "ecdsa",
	.init = ecdsainit,
	.read = ecdsaread,
	.write = ecdsawrite,
	.close = ecdsaclose,
	.addkey = replacekey,
	.keyprompt= "key? !password?",
};
