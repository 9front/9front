#include <u.h>
#include <libc.h>
#include <avl.h>
#include <bio.h>
#include "mix.h"

typedef
struct Mixchar {
	Rune r;
	int m;
} Mixchar;

static Rune mixtor[] = {
	' ',
	'A',
	'B',
	'C',
	'D',
	'E',
	'F',
	'G',
	'H',
	'I',
	L'Δ',
	'J',
	'K',
	'L',
	'M',
	'N',
	'O',
	'P',
	'Q',
	'R',
	L'Σ',
	L'Π',
	'S',
	'T',
	'U',
	'V',
	'W',
	'X',
	'Y',
	'Z',
	'0',
	'1',
	'2',
	'3',
	'4',
	'5',
	'6',
	'7',
	'8',
	'9',
	'.',
	',',
	'(',
	')',
	'+',
	'-',
	'*',
	'/',
	'=',
	'$',
	'<',
	'>',
	'@',
	';',
	':',
	'\''
};

static Mixchar rtomix[nelem(mixtor)];

static int
runecmp(void *a, void *b)
{
	Rune ra, rb;

	ra = ((Mixchar*)a)->r;
	rb = ((Mixchar*)b)->r;

	if(ra < rb)
		return -1;
	if(ra > rb)
		return 1;
	return 0;
}

void
cinit(void)
{
	int i;
	Mixchar *a;

	for(i = 0; i < nelem(rtomix); i++) {
		a = rtomix+i;
		a->r = mixtor[i];
		a->m = i;
	}
	qsort(rtomix, nelem(rtomix), sizeof(*rtomix), runecmp);
}

int
runetomix(Rune r)
{
	Mixchar *c, l;

	l.r = r;
	c = (Mixchar*)bsearch(&l, rtomix, nelem(rtomix), sizeof(*rtomix), runecmp);
	if(c == nil) {
		print("Not found!!\n");
		return -1;
	}

	return c->m;
}

Rune
mixtorune(int m)
{
	if(m < nelem(mixtor))
		return mixtor[m];
	return -1;
}
