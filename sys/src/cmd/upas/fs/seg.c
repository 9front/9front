#include "common.h"
#include <libsec.h>
#include "dat.h"

/*
 * unnatural acts with virtual memory
 */

typedef struct{
	int	ref;
	char	*va;
	long	sz;
}S;

static S		s[15];		/* 386 only gives 4 */
static int		nstab = nelem(s);
static long	ssem = 1;
//static ulong	thresh = 10*1024*1024;
static ulong	thresh = 1024;

void*
segmalloc(ulong sz)
{
	int i, j;
	void *va;

	if(sz < thresh)
		return emalloc(sz);
	semacquire(&ssem, 1);
	for(i = 0; i < nstab; i++)
		if(s[i].ref == 0)
			goto found;
notfound:
	/* errstr not informative; assume we hit seg limit */
	for(j = nstab - 1; j >= i; j--)
		if(s[j].ref)
			break;
	nstab = j;
	semrelease(&ssem, 1);
	return emalloc(sz);
found:
	/*
	 * the system doesn't leave any room for expansion
	 */
	va = segattach(SG_CEXEC, "memory", 0, sz + sz/10 + 4096);
	if(va == 0)
		goto notfound;
	s[i].ref++;
	s[i].va = va;
	s[i].sz = sz;
	semrelease(&ssem, 1);
	memset(va, 0, sz);
	return va;
}

void
segmfree(void *va)
{
	char *a;
	int i;

	a = va;
	for(i = 0; i < nstab; i++)
		if(s[i].va == a)
			goto found;
	free(va);
	return;
found:
	semacquire(&ssem, 1);
	s[i].ref--;
	s[i].va = 0;
	s[i].sz = 0;
	semrelease(&ssem, 1);
}

void*
segreallocfixup(int i, ulong sz)
{
	char buf[ERRMAX];
	void *va, *ova;

	rerrstr(buf, sizeof buf);
	if(strstr(buf, "segments overlap") == 0)
		sysfatal("segibrk: %r");
	va = segattach(SG_CEXEC, "memory", 0, sz);
	if(va == 0)
		sysfatal("segattach: %r");
	ova = s[i].va;
fprint(2, "fix memcpy(%p, %p, %lud)\n", va, ova, s[i].sz);
	memcpy(va, ova, s[i].sz);
	s[i].va = va;
	s[i].sz = sz;
	segdetach(ova);
	return va;
}

void*
segrealloc(void *va, ulong sz)
{
	char *a;
	int i;
	ulong sz0;

fprint(2, "segrealloc %p %lud\n", va, sz);
	if(va == 0)
		return segmalloc(sz);
	a = va;
	for(i = 0; i < nstab; i++)
		if(s[i].va == a)
			goto found;
	if(sz >= thresh)
	if(a = segmalloc(sz)){
		sz0 = msize(va);
		memcpy(a, va, sz0);
fprint(2, "memset(%p, 0, %lud)\n", a + sz0, sz - sz0);
		memset(a + sz0, 0, sz - sz0);
		return a;
	}
	return realloc(va, sz);
found:
	sz0 = s[i].sz;
fprint(2, "segbrk(%p, %p)\n", s[i].va, s[i].va + sz);
	va = segbrk(s[i].va, s[i].va + sz);
	if(va == (void*)-1 || va < end)
		return segreallocfixup(i, sz);
	a = va;
	if(sz > sz0)
{
fprint(2, "memset(%p, 0, %lud)\n", a + sz0, sz - sz0);
		memset(a + sz0, 0, sz - sz0);
}
	s[i].va = va;
	s[i].sz = sz;
	return va;
}

void*
emalloc(ulong n)
{
	void *p;
fprint(2, "emalloc %lud\n", n);
	p = mallocz(n, 1);
	if(!p)
		sysfatal("malloc %lud: %r", n);
	setmalloctag(p, getcallerpc(&n));
	return p;
}

void
main(void)
{
	char *p;
	int i;
	ulong sz;

	p = 0;
	for(i = 0; i < 6; i++){
		sz = i*512;
		p = segrealloc(p, sz);
		memset(p, 0, sz);
	}
	segmfree(p);
	exits("");
}
