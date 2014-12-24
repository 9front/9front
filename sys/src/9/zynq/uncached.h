#define free			ucfree
#define malloc			myucalloc
#define mallocz			ucallocz
#define smalloc			myucalloc
#define xspanalloc		ucallocalign

#define allocb			ucallocb
#define iallocb			uciallocb
#define freeb			ucfreeb

static void *
ucallocz(uint n, int)
{
	char *p = ucalloc(n);

	if (p)
		memset(p, 0, n);
	else
		panic("ucalloc: out of memory");
	return p;
}

static void *
myucalloc(uint n)
{
	return ucallocz(n, 1);
}
