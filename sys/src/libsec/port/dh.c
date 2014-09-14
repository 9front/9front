#include "os.h"
#include <mp.h>
#include <libsec.h>

mpint*
dh_new(DHstate *dh, mpint *p, mpint *g)
{
	memset(dh, 0, sizeof(*dh));
	dh->g = mpcopy(g);
	dh->p = mpcopy(p);
	if(dh->g != nil && dh->p != nil){
		dh->x = mprand(mpsignif(dh->p), genrandom, nil);
		dh->y = mpnew(0);
		if(dh->x != nil && dh->y != nil){
			mpexp(dh->g, dh->x, dh->p, dh->y);
			return dh->y;
		}
	}
	dh_finish(dh, nil);
	return nil;
}

mpint*
dh_finish(DHstate *dh, mpint *pub)
{
	mpint *k;

	k = nil;
	if(pub != nil && dh->x != nil && dh->p != nil){
		if((k = mpnew(0)) != nil)
			mpexp(pub, dh->x, dh->p, k);
	}
	mpfree(dh->g);
	mpfree(dh->p);
	mpfree(dh->x);
	mpfree(dh->y);
	memset(dh, 0, sizeof(*dh));
	return k;
}

