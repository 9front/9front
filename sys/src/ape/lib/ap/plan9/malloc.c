#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lock.h>

enum
{
	MAGIC		= 0xbada110c,
	MAX2SIZE	= 32,
	CUTOFF		= 12,
};

#define NPAD(t, align) \
	((sizeof(t) + align - 1) & ~(align - 1))
typedef struct Bucket Bucket;
typedef struct Header Header;
struct Header {
	int	size;
	int	magic;
	Bucket	*next;
};

struct Bucket
{
	union {
		Header;
		char _pad[NPAD(Header, 16)];
	};
	char	data[1];
};

typedef struct Arena Arena;
struct Arena
{
	Bucket	*btab[MAX2SIZE];	
	Lock;
};
static Arena arena;

#define datoff		((int)((Bucket*)0)->data)
#define nil		((void*)0)

extern	void	*sbrk(unsigned long);

void*
malloc(size_t size)
{
	uintptr_t next;
	int pow, n;
	Bucket *bp, *nbp;

	for(pow = 1; pow < MAX2SIZE; pow++) {
		if(size <= (1<<pow))
			goto good;
	}

	return nil;
good:
	/* Allocate off this list */
	lock(&arena);
	bp = arena.btab[pow];
	if(bp) {
		arena.btab[pow] = bp->next;
		unlock(&arena);

		if(bp->magic != 0)
			abort();

		bp->magic = MAGIC;
		return bp->data;
	}

	size = sizeof(Bucket)+(1<<pow);
	size += 7;
	size &= ~7;

	if(pow < CUTOFF) {
		n = (CUTOFF-pow)+2;
		bp = sbrk(size*n);
		if(bp == (void*)-1){
			unlock(&arena);
			return nil;
		}

		next = (uintptr_t)bp+size;
		nbp = (Bucket*)next;
		arena.btab[pow] = nbp;
		for(n -= 2; n; n--) {
			next = (uintptr_t)nbp+size;
			nbp->next = (Bucket*)next;
			nbp->size = pow;
			nbp = nbp->next;
		}
		nbp->size = pow;
	}
	else {
		bp = sbrk(size);
		if(bp == (void*)-1){
			unlock(&arena);
			return nil;
		}
	}
	unlock(&arena);
		
	bp->size = pow;
	bp->magic = MAGIC;

	return bp->data;
}

void
free(void *ptr)
{
	Bucket *bp, **l;

	if(ptr == nil)
		return;

	/* Find the start of the structure */
	bp = (Bucket*)((uintptr_t)ptr - datoff);

	if(bp->magic != MAGIC)
		abort();

	bp->magic = 0;
	l = &arena.btab[bp->size];
	lock(&arena);
	bp->next = *l;
	*l = bp;
	unlock(&arena);
}

void*
realloc(void *ptr, size_t n)
{
	void *new;
	size_t osize;
	Bucket *bp;

	if(ptr == nil)
		return malloc(n);

	/* Find the start of the structure */
	bp = (Bucket*)((uintptr_t)ptr - datoff);

	if(bp->magic != MAGIC)
		abort();

	/* enough space in this bucket */
	osize = 1<<bp->size;
	if(osize >= n)
		return ptr;

	new = malloc(n);
	if(new == nil)
		return nil;

	memmove(new, ptr, osize);
	free(ptr);

	return new;
}
