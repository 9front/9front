#include <u.h>
#include <libc.h>
#include <mp.h>

/* these tests suck but better than nothing... but really should test more values than just 1<<i */

#define MPTOX(_name,_type,_func)  \
void \
_name(void) \
{ \
	mpint *m; \
	int i, sign, mag; \
	_type v, e; \
	int fail; \
	 \
	fail = 0; \
	m = mpnew(0); \
	for(i = 0; i < 256; i++){ \
		sign = i >= 128 ? -1 : 1; \
		mag = i % 128; \
		itomp(sign, m); \
		mpleft(m, mag, m); \
		v = _func(m); \
		e = 0xcafebabe; USED(e);
#define MPTOX_END(_func,_format)  \
		if(v != e){ \
			fprint(2, "FAIL: _func(%#B): return value: got "_format", expected "_format"\n", m, v, e); \
			fail=1; \
		} \
	} \
	mpfree(m); \
	if(!fail) \
		fprint(2, "_func: passed\n"); \
}

#define XTOMP(_name,_type,_func)  \
void \
_name(void) \
{ \
	mpint *m, *r; \
	int i, sign, mag; \
	_type v; \
	int fail; \
	 \
	fail = 0; \
	m = mpnew(0); \
	r = mpnew(0); \
	for(i = 0; i < 256; i++){ \
		sign = i >= 128 ? -1 : 1; \
		mag = i % 128; \
		itomp(sign, r); \
		mpleft(r, mag, r);
#define XTOMP_END(_func,_type,_format)  \
		_func(sign * ((_type)1<<mag), m); \
		if(mpcmp(r, m) != 0){ \
			fprint(2, "FAIL: _func("_format"): return value: got %#B, expected %#B\n", sign * ((_type)1<<mag), m, r); \
			fail=1; \
		} \
	} \
	mpfree(m); \
	mpfree(r); \
	if(!fail) \
		fprint(2, "_func: passed\n"); \
}

MPTOX(test_mptoi, int, mptoi)
	if(mag < 31)
		e = sign*(1<<mag);
	else
		e = sign > 0 ? (1<<31)-1 : 1<<31;
MPTOX_END(mptoi, "%#x")

MPTOX(test_mptoui, uint, mptoui)
	if(mag < 32 && sign > 0)
		e = 1<<mag;
	else
		e = sign > 0 ? -1 : 0;
MPTOX_END(mptoui, "%#ux")


MPTOX(test_mptov, vlong, mptov)
	if(mag < 63)
		e = sign*(1LL<<mag);
	else
		e = sign > 0 ? (1LL<<63)-1 : 1LL<<63;
MPTOX_END(mptov, "%#llx")

MPTOX(test_mptouv, uvlong, mptouv)
	if(mag < 64 && sign > 0)
		e = 1LL<<mag;
	else
		e = sign > 0 ? -1ULL : 0;
MPTOX_END(mptouv, "%#llux")

XTOMP(test_itomp, int, itomp)
	if(mag >= 31) continue;
XTOMP_END(vtomp, vlong, "%lld")

XTOMP(test_uitomp, uint, uitomp)
	if(mag >= 32 || sign < 0) continue;
XTOMP_END(uitomp, uint, "%lld")

XTOMP(test_vtomp, vlong, vtomp)
	if(mag >= 63) continue;
XTOMP_END(vtomp, vlong, "%lld")

XTOMP(test_uvtomp, uvlong, uvtomp)
	if(mag >= 64 || sign < 0) continue;
XTOMP_END(uvtomp, vlong, "%lld")


void
convtests(void)
{
	test_mptoi();
	test_mptoui();
	test_mptov();
	test_mptouv();
	test_itomp();
	test_uitomp();
	test_vtomp();
	test_uvtomp();
}

