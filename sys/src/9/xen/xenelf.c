#include <u.h>
#include <libc.h>
#include "/sys/src/libmach/elf.h"

enum {
	Page = 4096,
};

#define ROUND(n) ((n+Page-1)&~(Page-1))

Shdr isect, csect;

static ushort
GETS(void *a)
{
	uchar *p = a;
	return p[0] | p[1]<<8;
}

static ulong
GETL(void *a)
{
	uchar *p = a;
	return p[0] | p[1]<<8 | p[2]<<16 | p[3]<<24;
}

static void
PUTS(void *a, ushort v)
{
	uchar *p = a;
	p[0] = v;
	p[1] = v>>8;
}

static void
PUTL(void *a, ulong v)
{
	uchar *p = a;
	p[0] = v;
	p[1] = v>>8;
	p[2] = v>>16;
	p[3] = v>>24;
}

void
copy(int fin, int fout, ulong src, ulong dst, ulong size)
{
	char buf[Page];
	int n;

	seek(fin, src, 0);
	seek(fout, dst, 0);
	n = Page;
	while (size > 0) {
		if (n > size)
			n = size;
		read(fin, buf, n);
		write(fout, buf, n);
		size -= n;
	}
}

void
main(int argc, char **argv)
{
	Ehdr e;
	Shdr s;
	Phdr p;
	int efd, ofd, ns, i, n;
	ulong shoff, off, noff, size, msize;
	char *sname, *sval;

	if (argc != 5)
		sysfatal("Usage: xenelf input-elf-file output-elf-file section-name section-contents");
	efd = open(argv[1], OREAD);
	if (efd < 0)
		sysfatal("%s: %r", argv[1]);
	ofd = create(argv[2], OWRITE, 0666);
	if (ofd < 0)
		sysfatal("%s: %r", argv[2]);
	sname = argv[3];
	sval = argv[4];

	read(efd, &e, sizeof e);
	//if (e.shstrndx)
	//	sysfatal("section header string index already present");
	
	/* page-align loadable segments in file */
	ns = GETS(&e.phnum);
	shoff = GETL(&e.phoff);
	noff = shoff+ns*sizeof(Phdr);
	noff = ROUND(noff);
	for (i = 0; i < ns; i++) {
		seek(efd, shoff+i*sizeof(Phdr), 0);
		read(efd, &p, sizeof p);
		off = GETL(&p.offset);
		PUTL(&p.offset, noff);
		size = GETL(&p.filesz);
		copy(efd, ofd, off, noff, size);
		if (GETL(&p.type) == LOAD) {
			size = ROUND(size);
			PUTL(&p.filesz, size);
			if ((msize = GETL(&p.memsz)) != 0 && size > msize)
				PUTL(&p.memsz, size);
		} else {
			/* memory size for symtab segment is actually line number table size */
			msize = GETL(&p.memsz);
			copy(efd, ofd, off+size, noff+size, msize);
			noff += msize;
		}
		noff += size;
		seek(ofd, shoff+i*sizeof(Phdr), 0);
		write(ofd, &p, sizeof p);
	}

	/* append single-entry shstrndx */
	PUTL(&isect.offset, seek(ofd, noff, 0));
	n = strlen(sname);
	PUTL(&isect.size, n+2);
	write(ofd, sname+n, 1);
	write(ofd, sname, n+1);
	
	/* append comment section contents */
	PUTL(&csect.name, 1);
	PUTL(&csect.offset, seek(ofd, 0, 2));
	n = strlen(sval);
	PUTL(&csect.size, n+1);
	write(ofd, sval, n+1);
	
	/* copy existing section headers to end */
	ns = 0; //GETS(&e.shnum);
	shoff = GETL(&e.shoff);
	PUTL(&e.shoff, seek(ofd, 0, 2));
	for (i = 0; i < ns; i++) {
		seek(efd, shoff+i*sizeof(Shdr), 0);
		read(efd, &s, sizeof s);
		seek(ofd, 0, 2);
		write(ofd, &s, sizeof s);
	}
	
	/* append section header for comment section */
	write(ofd, &csect, sizeof csect);
	++ns;

	/* append section header for shstrndx */
	PUTS(&e.shstrndx, ns);
	++ns;
	write(ofd, &isect, sizeof isect);

	/* rewrite elf header */
	PUTS(&e.shentsize, sizeof(Shdr));
	PUTS(&e.shnum, ns);
	seek(ofd, 0, 0);
	write(ofd, &e, sizeof e);

	exits(0);
}
