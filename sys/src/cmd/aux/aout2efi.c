#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>

#define	HDRSZ	0x200	/* EFI header size */

enum {
	EFI_IMAGE_MACHINE_IA32		= 0x014c,
	EFI_IMAGE_MACHINE_x64		= 0x8664,
	EFI_IMAGE_MACHINE_AARCH64	= 0xAA64,
};

static int	is32;
static int	infd, outfd;
static uchar	buf[IOUNIT];

static void
pack(uchar **pp, char *fmt, ...)
{
	va_list args;
	int c;
	uchar *p;
	u32int u;
	u64int l;

	va_start(args, fmt);
	for(; *fmt; fmt++){
		c = *fmt;
		if(c == 'q' && is32)
			c = 'l';
		p = *pp;
		switch(c){
		case 'b':
			p[0] = va_arg(args, int);
			*pp += 1;
			break;
		case 'w':
			u = va_arg(args, u32int);
			p[0] = u;
			p[1] = u>>8;
			*pp += 2;
			break;
		case 'l':
			u = va_arg(args, u32int);
			p[0] = u;
			p[1] = u>>8;
			p[2] = u>>16;
			p[3] = u>>24;
			*pp += 4;
			break;
		case 'q':
			l = va_arg(args, u64int);
			p[0] = l;
			p[1] = l>>8;
			p[2] = l>>16;
			p[3] = l>>24;
			p[4] = l>>32;
			p[5] = l>>40;
			p[6] = l>>48;
			p[7] = l>>56;
			*pp += 8;
			break;
		case '0':
			*pp += va_arg(args, int);
			break;
		default:
			sysfatal("pack: %c", c);
		}
	}
	va_end(args);
}

static void
usage(void)
{
	fprint(2, "usage: %s a.out\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	Fhdr fhdr;
	u64int kzero;
	uchar *header;
	char *ofile, *iname;
	int arch, chars, relocs;
	long n, szofdat, szofimage;

	kzero = 0x8000;
	ofile = nil;
	relocs = 0;
	ARGBEGIN{
	case 'Z':
		kzero = strtoull(EARGF(usage()), 0, 0);
		break;
	case 'o':
		ofile = strdup(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND;

	if(argc != 1)
		usage();

	infd = open(argv[0], OREAD);
	if(infd < 0)
		sysfatal("infd: %r");

	if(crackhdr(infd, &fhdr) == 0)
		sysfatal("crackhdr: %r");
	switch(mach->mtype){
	case MI386:
		arch = EFI_IMAGE_MACHINE_IA32;
		is32 = 1;
		chars = 2103;
		break;
	case MAMD64:
		arch = EFI_IMAGE_MACHINE_x64;
		chars = 2223;
		break;
	case MARM64:
		arch = EFI_IMAGE_MACHINE_AARCH64;
		chars = 518;
		relocs = 1;
		break;
	default:
		sysfatal("archloch");
	}
	szofdat = fhdr.txtsz + fhdr.datsz;
	szofimage = szofdat + fhdr.bsssz + HDRSZ;

	iname = strrchr(argv[0], '/');
	if(iname != nil)
		iname++;
	else
		iname = argv[0];
	if(ofile == nil)
		ofile = smprint("%s.efi", iname);
	outfd = create(ofile, OWRITE|OTRUNC, 0666);
	if(outfd < 0)
		sysfatal("create: %r");

	header = buf;

	/* mzhdr */
	pack(&header, "bb0l",
		'M', 'Z',	/* e_magic */
		0x3a,		/* UNUSED */
		0x40);		/* e_lfanew */

	/* pehdr */
	pack(&header, "bbbbwwlllww",
		'P', 'E', 0, 0,
		arch,			/* Machine */
		1+relocs,		/* NumberOfSections */
		0,			/* TimeDateStamp UNUSED */
		0,			/* PointerToSymbolTable UNUSED */
		0,			/* NumberOfSymbols UNUSED */
		is32 ? 0xE0 : 0xF0,	/* SizeOfOptionalHeader */
		chars);			/* Characteristics */
	pack(&header, "wbblllll",
		is32 ? 0x10B : 0x20B,	/* Magic */
    		9,			/* MajorLinkerVersion UNUSED */
		0,			/* MinorLinkerVersion UNUSED */
		0,			/* SizeOfCode UNUSED */
		0,			/* SizeOfInitializedData UNUSED */
		0,			/* SizeOfUninitializedData UNUSED */
		fhdr.entry-kzero,	/* AddressOfEntryPoint */
		0);			/* BaseOfCode UNUSED */
	if(is32)
	pack(&header, "l", 0);	/* BaseOfData UNUSED */
	pack(&header, "qllwwwwwwllllwwqqqqll0",
		kzero,		/* ImageBase */
		HDRSZ,		/* SectionAlignment */
		HDRSZ,		/* FileAlignment */
		4,		/* MajorOperatingSystemVersion UNUSED */
		0,		/* MinorOperatingSystemVersion UNUSED */
		0,		/* MajorImageVersion UNUSED */
		0,		/* MinorImageVersion UNUSED */
		4,		/* MajorSubsystemVersion */
		0,		/* MinorSubsystemVersion UNUSED */
		0,		/* Win32VersionValue UNUSED */
		szofimage,	/* SizeOfImage */
 		HDRSZ,		/* SizeOfHeaders */
 		0,		/* CheckSum UNUSED */
		10,		/* Subsystem (10 = efi application) */
		0,		/* DllCharacteristics UNUSED */
		0,		/* SizeOfStackReserve UNUSED */
		0,		/* SizeOfStackCommit UNUSED */
		0,		/* SizeOfHeapReserve UNUSED */
		0,		/* SizeOfHeapCommit UNUSED */
		0,		/* LoaderFlags UNUSED */
		16,		/* NumberOfRvaAndSizes UNUSED */
		32*4);		/* RVA UNUSED */
	if(relocs)
	pack(&header, "bbbbbbbbllllllwwl",
		'.', 'r', 'e', 'l', 'o', 'c', 0, 0,
		0,		/* VirtualSize */
		0,		/* VirtualAddress */
		0,		/* SizeOfData */
		0,		/* PointerToRawData */
		0,		/* PointerToRelocations UNUSED */
		0,		/* PointerToLinenumbers UNUSED */
		0,		/* NumberOfRelocations UNUSED */
		0,		/* NumberOfLinenumbers UNUSED */
		0x42100040);	/* Characteristics (read, discardable) */
	pack(&header, "bbbbbbbbllllllwwl",
		'.', 't', 'e', 'x', 't', 0, 0, 0,
		szofdat,	/* VirtualSize */
		HDRSZ,		/* VirtualAddress */
		szofdat,	/* SizeOfData */
		HDRSZ,		/* PointerToRawData */
		0,		/* PointerToRelocations UNUSED */
		0,		/* PointerToLinenumbers UNUSED */
		0,		/* NumberOfRelocations UNUSED */
		0,		/* NumberOfLinenumbers UNUSED */
		0x86000020);	/* Characteristics (code, RWX) */

	if(write(outfd, buf, HDRSZ) != HDRSZ)
		sysfatal("write: %r");
	if(seek(infd, fhdr.hdrsz, 0) != fhdr.hdrsz)
		sysfatal("seek: %r");
	for(;;){
		n = read(infd, buf, sizeof(buf));
		if(n < 0)
			sysfatal("read: %r");
		if(n == 0)
			break;
		if(write(outfd, buf, n) != n)
			sysfatal("write: %r");
	}

	exits(nil);
}
