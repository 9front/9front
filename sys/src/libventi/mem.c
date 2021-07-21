#include <u.h>
#include <libc.h>
#include <venti.h>

enum {
	IdealAlignment = 32,
	ChunkSize 	= 128*1024
};

void
vtfree(void *p)
{
	free(p);
}

void *
vtmalloc(ulong size)
{
	void *p = mallocz(size, 0);
	if(p == nil){
		fprint(2, "vtmalloc: out of memory allocating %lud", size);
		abort();
	}
	setmalloctag(p, getcallerpc(&size));
	return p;
}

void *
vtmallocz(ulong size)
{
	void *p = mallocz(size, 1);
	if(p == nil){
		fprint(2, "vtmallocz: out of memory allocating %lud", size);
		abort();
	}
	setmalloctag(p, getcallerpc(&size));
	return p;
}

void *
vtrealloc(void *p, ulong size)
{
	p = realloc(p, size);
	if(p == 0 && size != 0){
		fprint(2, "vtrealloc: out of memory allocating %lud", size);
		abort();
	}
	setrealloctag(p, getcallerpc(&size));
	return p;
}

void *
vtbrk(ulong n)
{
	static Lock lk;
	static uchar *buf;
	static ulong nbuf, nchunk;
	ulong align, pad;
	void *p;

	if(n >= IdealAlignment)
		align = IdealAlignment;
	else if(n > 8)
		align = 8;
	else	
		align = 4;

	if(n > ChunkSize){
		p = sbrk(n);
		if(p == (void*)-1)
			sysfatal("Failed to allocate permanent chunk size %lud", n);
		memset(p, 0, n);
		return (uchar*)p;
	}
	lock(&lk);
	pad = (align - (uintptr)buf) & (align-1);
	if(n + pad > nbuf) {
		buf = sbrk(ChunkSize);
		if(buf == (void*)-1)
			sysfatal("Failed to allocate permanent chunk size %ud", ChunkSize);
		memset(buf, 0, ChunkSize);
		nbuf = ChunkSize;
		pad = (align - (uintptr)buf) & (align-1);
		nchunk++;
	}

	assert(n + pad <= nbuf);	
	
	p = buf + pad;
	buf += pad + n;
	nbuf -= pad + n;
	unlock(&lk);

	return p;
}

