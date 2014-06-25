/*
 * Transform a Plan 9 386 bootable image to make it compatible with
 * the Xen binary image loader:
 *
 * - pad the beginning of the text with zeroes so that the image can be loaded at
 *    guest 'physical' address 0
 * - insert a Xen header
 * - pad the end of the text so that data segment is page-aligned in the file
 * - adjust the linenumber-pc table so Plan 9 debuggers won't be confused
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>

#define PAGE	4096
#define PLAN9HDR	32
#define XENHDR	32
#define	KZERO	0x80000000
#define FLAG_VALID	(1<<16)
#define FLAG_PAE	(1<<14)

void
lput(long n)
{
	char buf[sizeof(long)];
	int i;

	for (i = sizeof(long)-1; i >= 0; i--) {
		buf[i] = n;
		n >>= 8;
	}
	write(1, buf, sizeof(long));
}

void
rput(long n)
{
	char buf[sizeof(long)];
	int i;

	for (i = 0; i < sizeof(long); i++) {
		buf[i] = n;
		n >>= 8;
	}
	write(1, buf, sizeof(long));
}

void
copy(long n)
{
	char buf[PAGE];
	int m;

	while (n > 0) {
		m = sizeof buf;
		if (m > n)
			m = n;
		read(0, buf, m);
		write(1, buf, m);
		n -= m;
	}
}

void pad(int n)
{
	char buf[PAGE];
	int m;

	memset(buf, 0, sizeof buf);
	while (n > 0) {
		m = sizeof buf;
		if (m > n)
			m = n;
		write(1, buf, m);
		n -= m;
	}
}

/*
 * See /sys/src/cmd/8l/span.c:/^asmlc
 */
void adjustlnpc(int v)
{
	char buf[PAGE];
	int n, s;

	n = 0;
	while (v) {
		s = 127;
		if (v < 127)
			s = v;
		buf[n++] = s+128;
		if (n == sizeof buf) {
			write(1, buf, n);
			n = 0;
		}
		v -= s;
	}
	if (n > 0)
		write(1, buf, n);
}

void
main(int argc, char **argv)
{
	Fhdr fhdr;
	long newtxtsz;
	long newentry;
	long newlnpcsz;
	long prepad, postpad;
	long flags;

	flags = FLAG_VALID;
	if (argc > 1 && strcmp(argv[1], "-p") == 0)
		flags |= FLAG_PAE;

	crackhdr(0, &fhdr);

	newtxtsz = ((fhdr.txtsz+PLAN9HDR+PAGE-1)&~(PAGE-1)) - PLAN9HDR;
	newentry = KZERO+PLAN9HDR;
	prepad = fhdr.entry - newentry;
	postpad = newtxtsz - fhdr.txtsz;
	newtxtsz += prepad;
	newlnpcsz = fhdr.lnpcsz;
	if (newlnpcsz)
		newlnpcsz += (prepad+126)/127;

	/* plan 9 header */
	lput(4*11*11+7);		/* magic */
	lput(newtxtsz);			/* sizes */
	lput(fhdr.datsz);
	lput(fhdr.bsssz);
	lput(fhdr.symsz);		/* nsyms */
	lput(newentry);		/* va of entry */
	lput(fhdr.sppcsz);		/* sp offsets */
	lput(newlnpcsz);		/* line offsets */

	/* xen header */
	rput(0x336EC578);	/* magic */
	rput(flags);		/* flags */
	rput(-(0x336EC578+flags));	/* checksum */
	rput(newentry);	/* header_addr */
	rput(KZERO);	/* load_addr */
	rput(KZERO+newtxtsz+fhdr.datsz);	/* load_end_addr */
	rput(KZERO+newtxtsz+fhdr.datsz+fhdr.bsssz);	/* bss_end_addr */
	rput(fhdr.entry);	/* entry_addr */

	pad(prepad-XENHDR);

	seek(0, fhdr.txtoff, 0);
	copy(fhdr.txtsz);
	pad(postpad);
	copy(fhdr.datsz);
	copy(fhdr.symsz);
	if (newlnpcsz) {
		adjustlnpc(prepad);
		copy(fhdr.lnpcsz);
	}
	exits(0);
}
