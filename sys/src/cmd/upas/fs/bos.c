#include <u.h>
#include <libc.h>
#include <bio.h>

/*
 * assume:
 * - the stack segment can't be resized
 * - stacks may not be segattached (by name Stack, anyway)
 * - no thread library
 */
uintptr
absbos(void)
{
	char buf[64], *f[10], *s, *r;
	int n;
	uintptr p, q;
	Biobuf *b;

	p = 0xd0000000;	/* guess pc kernel */
	snprint(buf, sizeof buf, "/proc/%ud/segment", getpid());
	b = Bopen(buf, OREAD);
	if(b == nil)
		return p;
	for(; s = Brdstr(b, '\n', 1); free(s)){
		if((n = tokenize(s, f, nelem(f))) < 3)
			continue;
		if(strcmp(f[0], "Stack") != 0)
			continue;
		/*
		 * addressing from end because segment
		 * flags could become discontiguous  if
		 * additional flags are added
		 */
		q = strtoull(f[n - 3], &r, 16);
		if(*r == 0 && (char*)q > end)
			p = q;
	}
	Bterm(b);
	return p;
}
