#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <mach.h>

/*
 * file - determine type of file
 */
#define	LENDIAN(p)	((p)[0] | ((p)[1]<<8) | ((p)[2]<<16) | ((p)[3]<<24))

uchar	buf[6001];
short	cfreq[140];
short	wfreq[50];
int	nbuf;
Dir*	mbuf;
int	fd;
char 	*fname;
char	*slash;

enum
{
	Cword,
	Fword,
	Aword,
	Alword,
	Lword,
	I1,
	I2,
	I3,
	Clatin	= 128,
	Cbinary,
	Cnull,
	Ceascii,
	Cutf,
};
struct
{
	char*	word;
	int	class;
} dict[] =
{
	"PATH",		Lword,
	"TEXT",		Aword,
	"adt",		Alword,
	"aggr",		Alword,
	"alef",		Alword,
	"array",	Lword,
	"block",	Fword,
	"char",		Cword,
	"common",	Fword,
	"con",		Lword,
	"data",		Fword,
	"dimension",	Fword,
	"double",	Cword,
	"extern",	Cword,
	"bio",		I2,
	"float",	Cword,
	"fn",		Lword,
	"function",	Fword,
	"h",		I3,
	"implement",	Lword,
	"import",	Lword,
	"include",	I1,
	"int",		Cword,
	"integer",	Fword,
	"iota",		Lword,
	"libc",		I2,
	"long",		Cword,
	"module",	Lword,
	"real",		Fword,
	"ref",		Lword,
	"register",	Cword,
	"self",		Lword,
	"short",	Cword,
	"static",	Cword,
	"stdio",	I2,
	"struct",	Cword,
	"subroutine",	Fword,
	"u",		I2,
	"void",		Cword,
};

/* codes for 'mode' field in language structure */
enum	{
		Normal	= 0,
		First,		/* first entry for language spanning several ranges */
		Multi,		/* later entries "   "       "  ... */
		Shared,		/* codes used in several languages */
	};

struct
{
	int	mode;		/* see enum above */
	int 	count;
	int	low;
	int	high;
	char	*name;

} language[] =
{
	Normal,	0,	0x0100,	0x01FF,	"Extended Latin",
	Normal,	0,	0x0370,	0x03FF,	"Greek",
	Normal,	0,	0x0400,	0x04FF,	"Cyrillic",
	Normal,	0,	0x0530,	0x058F,	"Armenian",
	Normal,	0,	0x0590,	0x05FF,	"Hebrew",
	Normal,	0,	0x0600,	0x06FF,	"Arabic",
	Normal,	0,	0x0900,	0x097F,	"Devanagari",
	Normal,	0,	0x0980,	0x09FF,	"Bengali",
	Normal,	0,	0x0A00,	0x0A7F,	"Gurmukhi",
	Normal,	0,	0x0A80,	0x0AFF,	"Gujarati",
	Normal,	0,	0x0B00,	0x0B7F,	"Oriya",
	Normal,	0,	0x0B80,	0x0BFF,	"Tamil",
	Normal,	0,	0x0C00,	0x0C7F,	"Telugu",
	Normal,	0,	0x0C80,	0x0CFF,	"Kannada",
	Normal,	0,	0x0D00,	0x0D7F,	"Malayalam",
	Normal,	0,	0x0E00,	0x0E7F,	"Thai",
	Normal,	0,	0x0E80,	0x0EFF,	"Lao",
	Normal,	0,	0x1000,	0x105F,	"Tibetan",
	Normal,	0,	0x10A0,	0x10FF,	"Georgian",
	Normal,	0,	0x3040,	0x30FF,	"Japanese",
	Normal,	0,	0x3100,	0x312F,	"Chinese",
	First,	0,	0x3130,	0x318F,	"Korean",
	Multi,	0,	0x3400,	0x3D2F,	"Korean",
	Shared,	0,	0x4e00,	0x9fff,	"CJK",
	Normal,	0,	0,	0,	0,		/* terminal entry */
};


enum
{
	Fascii,		/* printable ascii */
	Flatin,		/* latin 1*/
	Futf,		/* UTF character set */
	Fbinary,	/* binary */
	Feascii,	/* ASCII with control chars */
	Fnull,		/* NULL in file */
} guess;

void	bump_utf_count(Rune);
int	cistrncmp(char*, char*, int);
void	filetype(int);
int	getfontnum(uchar*, uchar**);
int	isas(void);
int	isc(void);
int	iscint(void);
int	isenglish(void);
int	ishp(void);
int	ishtml(void);
int	isrfc822(void);
int	ismbox(void);
int	islimbo(void);
int	istga(void);
int	ismp3(void);
int	ismp4(void);
int	ismung(void);
int	isp9bit(void);
int	isp9font(void);
int	isrtf(void);
int	ismsdos(void);
int	isicocur(void);
int	iself(void);
int	istring(void);
int	isoffstr(void);
int	iff(void);
int	long0(void);
int	longoff(void);
int	istar(void);
int	isface(void);
int	isexec(void);
int	isudiff(void);
int	p9bitnum(char*, int*);
int	p9subfont(uchar*);
void	print_utf(void);
void	type(char*, int);
int	utf_count(void);
void	wordfreq(void);

int	(*call[])(void) =
{
	long0,		/* recognizable by first 4 bytes */
	istring,	/* recognizable by first string */
	iself,		/* ELF (foreign) executable */
	isexec,		/* native executables */
	iff,		/* interchange file format (strings) */
	longoff,	/* recognizable by 4 bytes at some offset */
	isoffstr,	/* recognizable by string at some offset */
	isudiff,	/* unified diff output */
	isrfc822,	/* email file */
	ismbox,		/* mail box */
	istar,		/* recognizable by tar checksum */
	iscint,		/* compiler/assembler intermediate */
	ishtml,		/* html keywords */
	islimbo,	/* limbo source */
	isc,		/* c & alef compiler key words */
	isas,		/* assembler key words */
	isp9font,	/* plan 9 font */
	isp9bit,	/* plan 9 image (as from /dev/window) */
	isrtf,		/* rich text format */
	ismsdos,	/* msdos exe (virus file attachement) */
	isicocur,		/* windows icon or cursor file */
	isface,		/* ascii face file */
	istga,
	ismp4,
	ismp3,

	/* last resorts */
	ismung,		/* entropy compressed/encrypted */
	isenglish,	/* char frequency English */
	0
};

int mime;

char OCTET[] =	"application/octet-stream";
char PLAIN[] =	"text/plain";

void
main(int argc, char *argv[])
{
	int i, j, maxlen;
	char *cp;
	Rune r;

	ARGBEGIN{
	case 'm':
		mime = 1;
		break;
	default:
		fprint(2, "usage: file [-m] [file...]\n");
		exits("usage");
	}ARGEND;

	maxlen = 0;
	if(mime == 0 || argc > 1){
		for(i = 0; i < argc; i++) {
			for (j = 0, cp = argv[i]; *cp; j++, cp += chartorune(&r, cp))
					;
			if(j > maxlen)
				maxlen = j;
		}
	}
	if (argc <= 0) {
		if(!mime)
			print ("stdin: ");
		filetype(0);
	}
	else {
		for(i = 0; i < argc; i++)
			type(argv[i], maxlen);
	}
	exits(0);
}

void
type(char *file, int nlen)
{
	Rune r;
	int i;
	char *p;

	if(nlen > 0){
		slash = 0;
		for (i = 0, p = file; *p; i++) {
			if (*p == '/')			/* find rightmost slash */
				slash = p;
			p += chartorune(&r, p);		/* count runes */
		}
		print("%s:%*s",file, nlen-i+1, "");
	}
	fname = file;
	if ((fd = open(file, OREAD)) < 0) {
		fprint(2, "cannot open: %r\n");
		return;
	}
	filetype(fd);
	close(fd);
}

void
utfconv(void)
{
	Rune r;
	uchar *rb;
	char *p, *e;
	int i;

	if(nbuf < 4)
		return;

	if(memcmp(buf, "\x00\x00\xFE\xFF", 4) == 0){
		if(!mime)
			print("utf-32be ");
		return;
	} else
	if(memcmp(buf, "\xFE\xFF\x00\x00", 4) == 0){
		if(!mime)
			print("utf-32le ");
		return;
	} else
	if(memcmp(buf, "\xEF\xBB\xBF", 3) == 0){
		memmove(buf, buf+3, nbuf-3);
		nbuf -= 3;
		return;
	} else
	if(memcmp(buf, "\xFE\xFF", 2) == 0){
		if(!mime)
			print("utf-16be ");

		nbuf -= 2;
		rb = malloc(nbuf+1);
		memmove(rb, buf+2, nbuf);
		p = (char*)buf;
		e = p+sizeof(buf)-UTFmax-1;
		for(i=0; i<nbuf && p < e; i+=2){
			r = rb[i+1] | rb[i]<<8;
			p += runetochar(p, &r);
		}
		*p = 0;
		free(rb);
		nbuf = p - (char*)buf;
	} else
	if(memcmp(buf, "\xFF\xFE", 2) == 0){
		if(!mime)
			print("utf-16le ");

		nbuf -= 2;
		rb = malloc(nbuf+1);
		memmove(rb, buf+2, nbuf);
		p = (char*)buf;
		e = p+sizeof(buf)-UTFmax-1;
		for(i=0; i<nbuf && p < e; i+=2){
			r = rb[i] | rb[i+1]<<8;
			p += runetochar(p, &r);
		}
		*p = 0;
		free(rb);
		nbuf = p - (char*)buf;
	}
}

void
filetype(int fd)
{
	Rune r;
	int i, f, n;
	char *p, *eob;

	free(mbuf);
	mbuf = dirfstat(fd);
	if(mbuf == nil){
		fprint(2, "cannot stat: %r\n");
		return;
	}
	if(mbuf->mode & DMDIR) {
		print("%s\n", mime ? OCTET : "directory");
		return;
	}
	if(mbuf->type != 'M' && mbuf->type != '|') {
		if(mime)
			print("%s\n", OCTET);
		else
			print("special file #%C/%s\n", mbuf->type, mbuf->name);
		return;
	}
	/* may be reading a pipe on standard input */
	nbuf = readn(fd, buf, sizeof(buf)-1);
	if(nbuf < 0) {
		fprint(2, "cannot read: %r\n");
		return;
	}
	if(nbuf == 0) {
		print("%s\n", mime ? PLAIN : "empty file");
		return;
	}
	buf[nbuf] = 0;

	utfconv();

	/*
	 * build histogram table
	 */
	memset(cfreq, 0, sizeof(cfreq));
	for (i = 0; language[i].name; i++)
		language[i].count = 0;
	eob = (char *)buf+nbuf;
	for(n = 0, p = (char *)buf; p < eob; n++) {
		if (!fullrune(p, eob-p) && eob-p < UTFmax)
			break;
		p += chartorune(&r, p);
		if (r == 0)
			f = Cnull;
		else if (r <= 0x7f) {
			if (!isprint(r) && !isspace(r))
				f = Ceascii;	/* ASCII control char */
			else f = r;
		} else if (r == 0x80) {
			bump_utf_count(r);
			f = Cutf;
		} else if (r < 0xA0)
			f = Cbinary;	/* Invalid Runes */
		else if (r <= 0xff)
			f = Clatin;	/* Latin 1 */
		else {
			bump_utf_count(r);
			f = Cutf;		/* UTF extension */
		}
		cfreq[f]++;			/* ASCII chars peg directly */
	}
	/*
	 * gross classify
	 */
	if (cfreq[Cbinary])
		guess = Fbinary;
	else if (cfreq[Cutf])
		guess = Futf;
	else if (cfreq[Clatin])
		guess = Flatin;
	else if (cfreq[Ceascii])
		guess = Feascii;
	else if (cfreq[Cnull])
		guess = Fbinary;
	else
		guess = Fascii;
	/*
	 * lookup dictionary words
	 */
	memset(wfreq, 0, sizeof(wfreq));
	if(guess == Fascii || guess == Flatin || guess == Futf)
		wordfreq();
	/*
	 * call individual classify routines
	 */
	for(i=0; call[i]; i++)
		if((*call[i])())
			return;

	/*
	 * if all else fails,
	 * print out gross classification
	 */
	if (nbuf < 100 && !mime)
		print(mime ? PLAIN : "short ");
	if (guess == Fascii)
		print("%s\n", mime ? PLAIN : "Ascii");
	else if (guess == Feascii)
		print("%s\n", mime ? PLAIN : "extended ascii");
	else if (guess == Flatin)
		print("%s\n", mime ? PLAIN : "latin ascii");
	else if (guess == Futf && utf_count() < 4)
		print_utf();
	else print("%s\n", mime ? OCTET : "binary");
}

void
bump_utf_count(Rune r)
{
	int low, high, mid;

	high = sizeof(language)/sizeof(language[0])-1;
	for (low = 0; low < high;) {
		mid = (low+high)/2;
		if (r >= language[mid].low) {
			if (r <= language[mid].high) {
				language[mid].count++;
				break;
			} else low = mid+1;
		} else high = mid;
	}
}

int
utf_count(void)
{
	int i, count;

	count = 0;
	for (i = 0; language[i].name; i++)
		if (language[i].count > 0)
			switch (language[i].mode) {
			case Normal:
			case First:
				count++;
				break;
			default:
				break;
			}
	return count;
}

int
chkascii(void)
{
	int i;

	for (i = 'a'; i < 'z'; i++)
		if (cfreq[i])
			return 1;
	for (i = 'A'; i < 'Z'; i++)
		if (cfreq[i])
			return 1;
	return 0;
}

int
find_first(char *name)
{
	int i;

	for (i = 0; language[i].name != 0; i++)
		if (language[i].mode == First
			&& strcmp(language[i].name, name) == 0)
			return i;
	return -1;
}

void
print_utf(void)
{
	int i, printed, j;

	if(mime){
		print("%s\n", PLAIN);
		return;
	}
	if (chkascii()) {
		printed = 1;
		print("Ascii");
	} else
		printed = 0;
	for (i = 0; language[i].name; i++)
		if (language[i].count) {
			switch(language[i].mode) {
			case Multi:
				j = find_first(language[i].name);
				if (j < 0)
					break;
				if (language[j].count > 0)
					break;
				/* Fall through */
			case Normal:
			case First:
				if (printed)
					print(" & ");
				else printed = 1;
				print("%s", language[i].name);
				break;
			case Shared:
			default:
				break;
			}
		}
	if(!printed)
		print("UTF");
	print(" text\n");
}

void
wordfreq(void)
{
	int low, high, mid, r;
	uchar *p, *p2, c;

	p = buf;
	for(;;) {
		while (p < buf+nbuf && !isalpha(*p))
			p++;
		if (p >= buf+nbuf)
			return;
		p2 = p;
		while(p < buf+nbuf && isalpha(*p))
			p++;
		c = *p;
		*p = 0;
		high = sizeof(dict)/sizeof(dict[0]);
		for(low = 0;low < high;) {
			mid = (low+high)/2;
			r = strcmp(dict[mid].word, (char*)p2);
			if(r == 0) {
				wfreq[dict[mid].class]++;
				break;
			}
			if(r < 0)
				low = mid+1;
			else
				high = mid;
		}
		*p++ = c;
	}
}

typedef struct Filemagic Filemagic;
struct Filemagic {
	ulong x;
	ulong mask;
	char *desc;
	char *mime;
};

/*
 * integers in this table must be as seen on a little-endian machine
 * when read from a file.
 */
Filemagic long0tab[] = {
	0xF16DF16D,	0xFFFFFFFF,	"pac1 audio file",	OCTET,
	/* "pac1" */
	0x31636170,	0xFFFFFFFF,	"pac3 audio file",	OCTET,
	/* "pXc2 */
	0x32630070,	0xFFFF00FF,	"pac4 audio file",	OCTET,
	0xBA010000,	0xFFFFFFFF,	"mpeg system stream",	OCTET,
	0x43614c66,	0xFFFFFFFF,	"FLAC audio file",	"audio/flac",
	0x30800CC0,	0xFFFFFFFF,	"inferno .dis executable", OCTET,
	0x04034B50,	0xFFFFFFFF,	"zip archive", "application/zip",
	070707,		0xFFFF,		"cpio archive", "application/x-cpio",
	0x2F7,		0xFFFF,		"tex dvi", "application/dvi",
	0xfaff,		0xfeff,		"mp3 audio",	"audio/mpeg",
	0xf0ff,		0xf6ff,		"aac audio",	"audio/mpeg",
	/* 0xfeedface: this could alternately be a Next Plan 9 boot image */
	0xcefaedfe,	0xFFFFFFFF,	"32-bit power Mach-O executable", OCTET,
	/* 0xfeedfacf */
	0xcffaedfe,	0xFFFFFFFF,	"64-bit power Mach-O executable", OCTET,
	/* 0xcefaedfe */
	0xfeedface,	0xFFFFFFFF,	"386 Mach-O executable", OCTET,
	/* 0xcffaedfe */
	0xfeedfacf,	0xFFFFFFFF,	"amd64 Mach-O executable", OCTET,
	/* 0xcafebabe */
	0xbebafeca,	0xFFFFFFFF,	"Mach-O universal executable", OCTET,
	/*
	 * venti & fossil magic numbers are stored big-endian on disk,
	 * thus the numbers appear reversed in this table.
	 */
	0xad4e5cd1,	0xFFFFFFFF,	"venti arena", OCTET,
	0x2bb19a52,	0xFFFFFFFF,	"paq archive", OCTET,
	0x1a53454e,	0xFFFFFFFF,	"NES ROM", OCTET,
	/* tcpdump pcap file */
	0xa1b2c3d4,	0xFFFFFFFF,	"pcap file",	"application/vnd.tcpdump.pcap",
	0xd4c3b2a1,	0xFFFFFFFF,	"pcap file",	"application/vnd.tcpdump.pcap",
	0xa1b23c4d,	0xFFFFFFFF,	"pcap file",	"application/vnd.tcpdump.pcap",
	0x4d3cb2a1,	0xFFFFFFFF,	"pcap file",	"application/vnd.tcpdump.pcap",
};

int
filemagic(Filemagic *tab, int ntab, ulong x)
{
	int i;

	for(i=0; i<ntab; i++)
		if((x&tab[i].mask) == tab[i].x){
			print("%s\n", mime ? tab[i].mime : tab[i].desc);
			return 1;
		}
	return 0;
}

int
long0(void)
{
	return filemagic(long0tab, nelem(long0tab), LENDIAN(buf));
}

typedef struct Fileoffmag Fileoffmag;
struct Fileoffmag {
	ulong	off;
	Filemagic;
};

/*
 * integers in this table must be as seen on a little-endian machine
 * when read from a file.
 */
Fileoffmag longofftab[] = {
	/*
	 * venti & fossil magic numbers are stored big-endian on disk,
	 * thus the numbers appear reversed in this table.
	 */
	256*1024, 0xe7a5e4a9, 0xFFFFFFFF, "venti arenas partition", OCTET,
	256*1024, 0xc75e5cd1, 0xFFFFFFFF, "venti index section", OCTET,
	128*1024, 0x89ae7637, 0xFFFFFFFF, "fossil write buffer", OCTET,
	4,	  0x31647542, 0xFFFFFFFF, "OS X finder properties", OCTET,
	0x100,	  0x41474553, 0xFFFFFFFF, "SEGA ROM", OCTET,
	0x1fc,	  0xAA550000, 0xFFFF0000, "bootable disk image", OCTET,
};

int
fileoffmagic(Fileoffmag *tab, int ntab)
{
	int i;
	ulong x;
	Fileoffmag *tp;
	uchar buf[sizeof(long)];

	for(i=0; i<ntab; i++) {
		tp = tab + i;
		seek(fd, tp->off, 0);
		if (readn(fd, buf, sizeof buf) != sizeof buf)
			continue;
		x = LENDIAN(buf);
		if((x&tp->mask) == tp->x){
			print("%s\n", mime ? tp->mime : tp->desc);
			return 1;
		}
	}
	return 0;
}

int
longoff(void)
{
	return fileoffmagic(longofftab, nelem(longofftab));
}

int
isexec(void)
{
	Fhdr f;

	seek(fd, 0, 0);		/* reposition to start of file */
	if(crackhdr(fd, &f)) {
		print("%s\n", mime ? OCTET : f.name);
		return 1;
	}
	return 0;
}


/* from tar.c */
enum { NAMSIZ = 100, TBLOCK = 512 };

union	hblock
{
	char	dummy[TBLOCK];
	struct	header
	{
		char	name[NAMSIZ];
		char	mode[8];
		char	uid[8];
		char	gid[8];
		char	size[12];
		char	mtime[12];
		char	chksum[8];
		char	linkflag;
		char	linkname[NAMSIZ];
		/* rest are defined by POSIX's ustar format; see p1003.2b */
		char	magic[6];	/* "ustar" */
		char	version[2];
		char	uname[32];
		char	gname[32];
		char	devmajor[8];
		char	devminor[8];
		char	prefix[155];  /* if non-null, path = prefix "/" name */
	} dbuf;
};

int
checksum(union hblock *hp)
{
	int i;
	char *cp;
	struct header *hdr = &hp->dbuf;

	for (cp = hdr->chksum; cp < &hdr->chksum[sizeof hdr->chksum]; cp++)
		*cp = ' ';
	i = 0;
	for (cp = hp->dummy; cp < &hp->dummy[TBLOCK]; cp++)
		i += *cp & 0xff;
	return i;
}

int
istar(void)
{
	int chksum;
	char tblock[TBLOCK];
	union hblock *hp = (union hblock *)tblock;
	struct header *hdr = &hp->dbuf;

	seek(fd, 0, 0);		/* reposition to start of file */
	if (readn(fd, tblock, sizeof tblock) != sizeof tblock)
		return 0;
	chksum = strtol(hdr->chksum, 0, 8);
	if (hdr->name[0] != '\0' && checksum(hp) == chksum) {
		if (strcmp(hdr->magic, "ustar") == 0)
			print(mime? "application/x-ustar\n": "posix tar archive\n");
		else
			print(mime? "application/x-tar\n": "tar archive\n");
		return 1;
	}
	return 0;
}

/*
 * initial words to classify file
 */
struct	FILE_STRING
{
	char 	*key;
	char	*filetype;
	int	length;
	char	*mime;
} file_string[] =
{
	"\x1f\x9d",		"compressed",			2,	"application/x-compress",
	"\x1f\x8b",		"gzip compressed",		2,	"application/x-gzip",
	"BZh",			"bzip2 compressed",		3,	"application/x-bzip2",
	"!<arch>\n__.SYMDEF",	"archive random library",	16,	OCTET,
	"!<arch>\n",		"archive",			8,	OCTET,
	"070707",		"cpio archive - ascii header",	6,	OCTET,
	"#!/bin/rc",		"rc executable file",		9,	PLAIN,
	"#!/bin/sh",		"sh executable file",		9,	PLAIN,
	"%!",			"postscript",			2,	"application/postscript",
	"\004%!",		"postscript",			3,	"application/postscript",
	"x T post",		"troff output for post",	8,	"application/troff",
	"x T Latin1",		"troff output for Latin1",	10,	"application/troff",
	"x T utf",		"troff output for UTF",		7,	"application/troff",
	"x T 202",		"troff output for 202",		7,	"application/troff",
	"x T aps",		"troff output for aps",		7,	"application/troff",
	"x T ",			"troff output",			4,	"application/troff",
	"GIF",			"GIF image", 			3,	"image/gif",
	"\0PC Research, Inc\0",	"ghostscript fax file",		18,	"application/ghostscript",
	"%PDF",			"PDF",				4,	"application/pdf",
	"<!DOCTYPE",		"HTML file",			9,	"text/html",
	"<!doctype",		"HTML file",			9,	"text/html",
	"<!--",			"HTML file",			4,	"text/html",
	"<html>",		"HTML file",			6,	"text/html",
	"<HTML>",		"HTML file",			6,	"text/html",
	"<?xml",		"HTML file",			5,	"text/html",
	"\111\111\052\000",	"tiff",				4,	"image/tiff",
	"\115\115\000\052",	"tiff",				4,	"image/tiff",
	"\377\330\377\340",	"jpeg",				4,	"image/jpeg",
	"\377\330\377\341",	"jpeg",				4,	"image/jpeg",
	"\377\330\377\333",	"jpeg",				4,	"image/jpeg",
	"\xff\xd8",		"jpeg",				2,	"image/jpeg",
	"BM",			"bmp",				2,	"image/bmp", 
	"\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1",	"microsoft office document",	8,	"application/doc",
	"<MakerFile ",		"FrameMaker file",		11,	"application/framemaker",
	"\033E\033",		"HP PCL printer data",		3,	OCTET,
	"\033&",		"HP PCL printer data",		2,	OCTET,
	"\033%-12345X",		"HPJCL file",		9,	"application/hpjcl",
	"\033Lua",		"Lua bytecode",		4,	OCTET,
	"ID3",			"mp3 audio with id3",	3,	"audio/mpeg",
	"OggS",			"ogg audio",		4,	"audio/ogg",
	".snd",			"sun audio",		4,	"audio/basic",
	"\211PNG",		"PNG image",		4,	"image/png",
	"P1\n",			"ppm",			3,	"image/ppm",
	"P2\n",			"ppm",			3,	"image/ppm",
	"P3\n",			"ppm",			3,	"image/ppm",
	"P4\n",			"ppm",			3,	"image/ppm",
	"P5\n",			"ppm",			3,	"image/ppm",
	"P6\n",			"ppm",			3,	"image/ppm",
	"/* XPM */\n",	"xbm",				10,	"image/xbm",
	".HTML ",		"troff -ms input",	6,	"text/troff",
	".LP",			"troff -ms input",	3,	"text/troff",
	".ND",			"troff -ms input",	3,	"text/troff",
	".PP",			"troff -ms input",	3,	"text/troff",
	".TL",			"troff -ms input",	3,	"text/troff",
	".TR",			"troff -ms input",	3,	"text/troff",
	".TH",			"manual page",		3,	"text/troff",
	".\\\"",		"troff input",		3,	"text/troff",
	".de",			"troff input",		3,	"text/troff",
	".if",			"troff input",		3,	"text/troff",
	".nr",			"troff input",		3,	"text/troff",
	".tr",			"troff input",		3,	"text/troff",
	"vac:",			"venti score",		4,	PLAIN,
	"-----BEGIN CERTIFICATE-----\n",
				"pem certificate",	-1,	PLAIN,
	"-----BEGIN TRUSTED CERTIFICATE-----\n",
				"pem trusted certificate", -1,	PLAIN,
	"-----BEGIN X509 CERTIFICATE-----\n",
				"pem x.509 certificate", -1,	PLAIN,
	"subject=/C=",		"pem certificate with header", -1, PLAIN,
	"process snapshot ",	"process snapshot",	-1,	"application/snapfs",
	"d8:announce",		"torrent file",		11,	"application/x-bittorrent",
	"[playlist]",		"playlist",		10,	"application/x-scpls",
	"#EXTM3U",		"playlist",		7,	"audio/x-mpegurl",
	"BEGIN:VCARD\r\n",	"vCard",		13,	"text/directory;profile=vcard",
	"BEGIN:VCARD\n",	"vCard",		12,	"text/directory;profile=vcard",
	"AT&T",			"DjVu document",	4,	"image/vnd.djvu",
	"Extended module: ",	"XM audio",		17,	"audio/xm",
	"MThd",			"midi audio",		4,	"audio/midi",
	"MUS\x1a",		"mus audio",		4,	"audio/mus",
	"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
	"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
	"\x00\x00\x00\xbb\x11\x22\x00\x44\xff\xff\xff\xff\xff\xff\xff\xff"
	"\xaa\x99\x55\x66", "Xilinx bitstream (not byteswappped)", 52, OCTET,
	"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
	"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
	"\xbb\x00\x00\x00\x44\x00\x22\x11\xff\xff\xff\xff\xff\xff\xff\xff"
	"\x66\x55\x99\xaa", "Xilinx bitstream (byteswappped)", 52, OCTET,
	0,0,0,0
};

int
istring(void)
{
	int i, l;
	struct FILE_STRING *p;

	for(p = file_string; p->key; p++) {
		l = p->length;
		if(l == -1)
			l = strlen(p->key);
		if(nbuf >= l && memcmp(buf, p->key, l) == 0) {
			print("%s\n", mime ? p->mime : p->filetype);
			return 1;
		}
	}
	if(strncmp((char*)buf, "TYPE=", 5) == 0) {	/* td */
		for(i = 5; i < nbuf; i++)
			if(buf[i] == '\n')
				break;
		if(mime)
			print("%s\n", OCTET);
		else
			print("%.*s picture\n", utfnlen((char*)buf+5, i-5), (char*)buf+5);
		return 1;
	}
	return 0;
}

struct offstr
{
	ulong	off;
	struct FILE_STRING;
} offstrs[] = {
	32*1024, "\001CD001\001",	"ISO9660 CD image",	7,	"application/x-iso9660-image",
	32*4, "DICM",	"DICOM medical imaging data",	4,	"application/dicom",
	0, 0, 0, 0, 0
};

int
isoffstr(void)
{
	int n;
	char buf[256];
	struct offstr *p;

	for(p = offstrs; p->key; p++) {
		seek(fd, p->off, 0);
		n = p->length;
		if (n > sizeof buf)
			n = sizeof buf;
		if (readn(fd, buf, n) != n)
			continue;
		if(memcmp(buf, p->key, n) == 0) {
			print("%s\n", mime ? p->mime : p->filetype);
			return 1;
		}
	}
	return 0;
}

int
iff(void)
{
	if (strncmp((char*)buf, "FORM", 4) == 0 &&
	    strncmp((char*)buf+8, "AIFF", 4) == 0) {
		print("%s\n", mime? "audio/x-aiff": "aiff audio");
		return 1;
	}
	if (strncmp((char*)buf, "RIFF", 4) == 0) {
		if (strncmp((char*)buf+8, "WAVE", 4) == 0)
			print("%s\n", mime? "audio/wave": "wave audio");
		else if (strncmp((char*)buf+8, "AVI ", 4) == 0)
			print("%s\n", mime? "video/avi": "avi video");
		else
			print("%s\n", mime? OCTET : "riff file");
		return 1;
	}
	return 0;
}

char*	html_string[] = {
	"blockquote",
	"!DOCTYPE", "![CDATA[", "basefont", "frameset", "noframes", "textarea",
	"caption",
	"button", "center", "iframe", "object", "option", "script",
	"select", "strong",
	"blink", "embed", "frame", "input", "label", "param", "small",
	"style", "table", "tbody", "tfoot", "thead", "title",
	"?xml", "body", "code", "font", "form", "head", "html",
	"link", "menu", "meta", "span",
	"!--", "big", "dir", "div", "img", "pre", "sub", "sup",
	"br", "dd", "dl", "dt", "em", "h1", "h2", "h3", "h4", "h5",
	"h6", "hr", "li", "ol", "td", "th", "tr", "tt", "ul",
	"a", "b", "i", "p", "q", "u",
	0,
};

int
isudiff(void)
{
	char *p;

	p = (char*)buf;
	if((p = strstr(p, "diff")) != nil)
	if((p = strchr(p, '\n')) != nil)
	if(strncmp(++p, "--- ", 4) == 0)
	if((p = strchr(p, '\n')) != nil)
	if(strncmp(++p, "+++ ", 4) == 0)
	if((p = strchr(p, '\n')) != nil)
	if(strncmp(++p, "@@ ", 3) == 0){
		print("%s\n", mime ? "text/plain" : "unified diff output");
		return 1;
	}
	return 0;
}

int
ishtml(void)
{
	int i, n, count;
	uchar *p;

	count = 0;
	p = buf;
	for(;;) {
		while(p < buf+nbuf && *p != '<')
			p++;
		p++;
		if (p >= buf+nbuf)
			break;
		if(*p == '/')
			p++;
		if(p >= buf+nbuf)
			break;
		for(i = 0; html_string[i]; i++){
			n = strlen(html_string[i]);
			if(p + n > buf+nbuf)
				continue;
			if(cistrncmp(html_string[i], (char*)p, n) == 0) {
				p += n;
				if(p < buf+nbuf && strchr("\t\r\n />", *p)){
					if(++count > 2) {
						print("%s\n", mime ? "text/html" : "HTML file");
						return 1;
					}
				}
				break;
			}
		}
	}
	return 0;
}

char*	rfc822_string[] =
{
	"from:",
	"date:",
	"to:",
	"subject:",
	"received:",
	"reply to:",
	"sender:",
	0,
};

int
isrfc822(void)
{

	char *p, *q, *r;
	int i, count;

	count = 0;
	p = (char*)buf;
	for(;;) {
		q = strchr(p, '\n');
		if(q == nil)
			break;
		*q = 0;
		if(p == (char*)buf && strncmp(p, "From ", 5) == 0 && strstr(p, " remote from ")){
			count++;
			*q = '\n';
			p = q+1;
			continue;
		}
		*q = '\n';
		if(*p != '\t' && *p != ' '){
			r = strchr(p, ':');
			if(r == 0 || r > q)
				break;
			for(i = 0; rfc822_string[i]; i++) {
				if(cistrncmp(p, rfc822_string[i], strlen(rfc822_string[i])) == 0){
					count++;
					break;
				}
			}
		}
		p = q+1;
	}
	if(count >= 3){
		print("%s\n", mime ? "message/rfc822" : "email file");
		return 1;
	}
	return 0;
}

int
ismbox(void)
{
	char *p, *q;

	p = (char*)buf;
	q = strchr(p, '\n');
	if(q == nil)
		return 0;
	*q = 0;
	if(strncmp(p, "From ", 5) == 0 && strstr(p, " remote from ") == nil){
		print("%s\n", mime ? PLAIN : "mail box");
		return 1;
	}
	*q = '\n';
	return 0;
}

int
iscint(void)
{
	int type;
	char *name;
	Biobuf b;

	if(Binit(&b, fd, OREAD) == Beof)
		return 0;
	seek(fd, 0, 0);
	type = objtype(&b, &name);
	if(type < 0)
		return 0;
	if(mime)
		print("%s\n", OCTET);
	else
		print("%s intermediate\n", name);
	return 1;
}

int
isc(void)
{
	int n;

	n = wfreq[I1];
	/*
	 * includes
	 */
	if(n >= 2 && wfreq[I2] >= n && wfreq[I3] >= n && cfreq['.'] >= n)
		goto yes;
	if(n >= 1 && wfreq[Alword] >= n && wfreq[I3] >= n && cfreq['.'] >= n)
		goto yes;
	/*
	 * declarations
	 */
	if(wfreq[Cword] >= 5 && cfreq[';'] >= 5)
		goto yes;
	/*
	 * assignments
	 */
	if(cfreq[';'] >= 10 && cfreq['='] >= 10 && wfreq[Cword] >= 1)
		goto yes;
	return 0;

yes:
	if(mime){
		print("%s\n", PLAIN);
		return 1;
	}
	if(wfreq[Alword] > 0)
		print("alef program\n");
	else
		print("c program\n");
	return 1;
}

int
islimbo(void)
{
	/*
	 * includes
	 */
	if(wfreq[Lword] < 4)
		return 0;
	print("%s\n", mime ? PLAIN : "limbo program");
	return 1;
}

int
isas(void)
{
	/*
	 * includes
	 */
	if(wfreq[Aword] < 2)
		return 0;
	print("%s\n", mime ? PLAIN : "as program");
	return 1;
}

int
istga(void)
{
	uchar *p;

	p = buf;
	if(nbuf < 18)
		return 0;
	if((p[12] | p[13]<<8) == 0)	/* width */
		return 0;
	if((p[14] | p[15]<<8) == 0)	/* height */
		return 0;
	if(p[16] != 8 && p[16] != 15 && p[16] != 16 && p[16] != 24 && p[16] != 32)	/* bpp */
		return 0;
	if(((p[2]|(1<<3)) & (~3)) != (1<<3))	/* rle flag */
		return 0;
	if(p[1] == 0){	/* non color-mapped */
		if((p[2]&3) != 2 && (p[2]&3) != 3)	
			return 0;
		if((p[5] | p[6]<<8) != 0)	/* palette length */
			return 0;
	} else
	if(p[1] == 1){	/* color-mapped */
		if((p[2]&3) != 1 || p[7] == 0)	
			return 0;
		if((p[5] | p[6]<<8) == 0)	/* palette length */
			return 0;
	} else
		return 0;
	print("%s\n", mime ? "image/tga" : "targa image");
	return 1;
}

int
ismp3(void)
{
	uchar *p, *e;

	p = buf;
	e = p + nbuf-1;
	while((p < e) && (p = memchr(p, 0xFF, e - p))){
		if((p[1] & 0xFE) == 0xFA){
			print("%s\n", mime ? "audio/mpeg" : "mp3 audio");
			return 1;
		}
		p++;
	}
	return 0;
}

int
ismp4(void)
{
	if(nbuf <= 12)
		return 0;
	if(memcmp(&buf[4], "ftyp", 4) != 0)
		return 0;
	if(memcmp(&buf[8], "isom", 4) == 0){
		print("%s\n", mime ? "video/mp4" : "mp4 video");
		return 1;
	}
	if(memcmp(&buf[8], "M4A ", 4) == 0){
		print("%s\n", mime ? "audio/m4a" : "m4a audio");
		return 1;
	}
	return 0;
}

/*
 * low entropy means encrypted
 */
int
ismung(void)
{
	int i, bucket[8];
	float cs;

	if(nbuf < 64)
		return 0;
	memset(bucket, 0, sizeof(bucket));
	for(i=nbuf-64; i<nbuf; i++)
		bucket[(buf[i]>>5)&07] += 1;

	cs = 0.;
	for(i=0; i<8; i++)
		cs += (bucket[i]-8)*(bucket[i]-8);
	cs /= 8.;
	if(cs <= 24.322) {
		if(buf[0]==0x1f && buf[1]==0x9d)
			print("%s\n", mime ? "application/x-compress" : "compressed");
		else
		if(buf[0]==0x1f && buf[1]==0x8b)
			print("%s\n", mime ? "application/x-gzip" : "gzip compressed");
		else
		if(buf[0]=='B' && buf[1]=='Z' && buf[2]=='h')
			print("%s\n", mime ? "application/x-bzip2" : "bzip2 compressed");
		else
		if(buf[0]==0x78 && buf[1]==0x9c)
			print("%s\n", mime ? "application/x-deflate" : "zlib compressed");
		else
			print("%s\n", mime ? OCTET : "encrypted");
		return 1;
	}
	return 0;
}

/*
 * english by punctuation and frequencies
 */
int
isenglish(void)
{
	int vow, comm, rare, badpun, punct;
	char *p;

	if(guess != Fascii && guess != Feascii)
		return 0;
	badpun = 0;
	punct = 0;
	for(p = (char *)buf; p < (char *)buf+nbuf-1; p++)
		switch(*p) {
		case '.':
		case ',':
		case ')':
		case '%':
		case ';':
		case ':':
		case '?':
			punct++;
			if(p[1] != ' ' && p[1] != '\n')
				badpun++;
		}
	if(badpun*5 > punct)
		return 0;
	if(cfreq['>']+cfreq['<']+cfreq['/'] > cfreq['e'])	/* shell file test */
		return 0;
	if(2*cfreq[';'] > cfreq['e'])
		return 0;

	vow = 0;
	for(p="AEIOU"; *p; p++) {
		vow += cfreq[*p];
		vow += cfreq[tolower(*p)];
	}
	comm = 0;
	for(p="ETAION"; *p; p++) {
		comm += cfreq[*p];
		comm += cfreq[tolower(*p)];
	}
	rare = 0;
	for(p="VJKQXZ"; *p; p++) {
		rare += cfreq[*p];
		rare += cfreq[tolower(*p)];
	}
	if(vow*5 >= nbuf-cfreq[' '] && comm >= 10*rare) {
		print("%s\n", mime ? PLAIN : "English text");
		return 1;
	}
	return 0;
}

/*
 * pick up a number with
 * syntax _*[0-9]+_
 */
#define	P9BITLEN	12
int
p9bitnum(char *s, int *v)
{
	char *es;

	if(s[P9BITLEN-1] != ' ')
		return -1;
	s[P9BITLEN-1] = '\0';
	*v = strtol(s, &es, 10);
	s[P9BITLEN-1] = ' ';
	if(es != &s[P9BITLEN-1])
		return -1;
	return 0;
}

int
depthof(char *s, int *newp)
{
	char *es;
	int d;

	*newp = 0;
	es = s+12;
	while(s<es && *s==' ')
		s++;
	if(s == es)
		return -1;
	if('0'<=*s && *s<='9')
		return 1<<strtol(s, nil, 0);

	*newp = 1;
	d = 0;
	while(s<es && *s!=' '){
		if(strchr("rgbkamx", *s) == nil)
			return -1;
		s++;
		if('0'<=*s && *s<='9')
			d += strtoul(s, &s, 10);
		else
			return -1;
	}

	if(d % 8 == 0 || 8 % d == 0)
		return d;
	else
		return -1;
}

int
isp9bit(void)
{
	int dep, lox, loy, hix, hiy, px, new, cmpr;
	long len;
	char *newlabel;
	uchar *cp;

	cp = buf;
	cmpr = 0;
	if(memcmp(cp, "compressed\n", 11) == 0) {
		cmpr = 1;
		cp = buf + 11;
	}

	if((dep = depthof((char*)cp + 0*P9BITLEN, &new)) < 0)
		return 0;
	newlabel = new ? "" : "old ";
	if(p9bitnum((char*)cp + 1*P9BITLEN, &lox) < 0)
		return 0;
	if(p9bitnum((char*)cp + 2*P9BITLEN, &loy) < 0)
		return 0;
	if(p9bitnum((char*)cp + 3*P9BITLEN, &hix) < 0)
		return 0;
	if(p9bitnum((char*)cp + 4*P9BITLEN, &hiy) < 0)
		return 0;

	hix -= lox;
	hiy -= loy;
	if(hix <= 0 || hiy <= 0)
		return 0;

	if(dep < 8){
		px = 8/dep;		/* pixels per byte */
		/* set l to number of bytes of data per scan line */
		len = (hix+px-1)/px;
	}else
		len = hix*dep/8;
	len *= hiy;			/* col length */
	len += 5 * P9BITLEN;		/* size of initial ascii */

	/*
	 * for compressed images, don't look any further. otherwise:
	 * for image file, length is non-zero and must match calculation above.
	 * for /dev/window and /dev/screen the length is always zero.
	 * for subfont, the subfont header should follow immediately.
	 */
	if (cmpr) {
		print(mime ? "image/p9bit\n" : "Compressed %splan 9 image or subfont, depth %d, size %dx%d\n",
			newlabel, dep, hix, hiy);
		return 1;
	}
	/*
	 * mbuf->length == 0 probably indicates reading a pipe.
	 * Ghostscript sometimes produces a little extra on the end.
	 */
	if (len != 0 && (mbuf->length == 0 || mbuf->length == len ||
	    mbuf->length > len && mbuf->length < len+P9BITLEN)) {
		print(mime ? "image/p9bit\n" : "%splan 9 image, depth %d, size %dx%d\n",
			newlabel, dep, hix, hiy);
		return 1;
	}
	if (p9subfont(buf+len)) {
		print(mime ? "image/p9bit\n" : "%ssubfont file, depth %d, size %dx%d\n",
			newlabel, dep, hix, hiy);
		return 1;
	}
	return 0;
}

int
p9subfont(uchar *p)
{
	int n, h, a;

	/* if image too big, assume it's a subfont */
	if (p+3*P9BITLEN > buf+sizeof(buf))
		return 1;

	if (p9bitnum((char*)p + 0*P9BITLEN, &n) < 0)	/* char count */
		return 0;
	if (p9bitnum((char*)p + 1*P9BITLEN, &h) < 0)	/* height */
		return 0;
	if (p9bitnum((char*)p + 2*P9BITLEN, &a) < 0)	/* ascent */
		return 0;
	if(n > 0 && h > 0 && a >= 0)
		return 1;
	return 0;
}

#define	WHITESPACE(c)		((c) == ' ' || (c) == '\t' || (c) == '\n')

int
isp9font(void)
{
	uchar *cp, *p;
	int i, n;
	char pathname[1024];

	cp = buf;
	if (!getfontnum(cp, &cp))	/* height */
		return 0;
	if (!getfontnum(cp, &cp))	/* ascent */
		return 0;
	for (i = 0; cp=(uchar*)strchr((char*)cp, '\n'); i++) {
		if (!getfontnum(cp, &cp))	/* min */
			break;
		if (!getfontnum(cp, &cp))	/* max */
			return 0;
		getfontnum(cp, &cp);	/* optional offset */
		while (WHITESPACE(*cp))
			cp++;
		for (p = cp; *cp && !WHITESPACE(*cp); cp++)
				;
			/* construct a path name, if needed */
		n = 0;
		if (*p != '/' && slash) {
			n = slash-fname+1;
			if (n < sizeof(pathname))
				memcpy(pathname, fname, n);
			else n = 0;
		}
		if (n+cp-p+4 < sizeof(pathname)) {
			memcpy(pathname+n, p, cp-p);
			n += cp-p;
			pathname[n] = 0;
			if (access(pathname, AEXIST) < 0) {
				strcpy(pathname+n, ".0");
				if (access(pathname, AEXIST) < 0)
					return 0;
			}
		}
	}
	if (i) {
		print("%s\n", mime ? PLAIN : "font file");
		return 1;
	}
	return 0;
}

int
getfontnum(uchar *cp, uchar **rp)
{
	while (WHITESPACE(*cp))		/* extract ulong delimited by whitespace */
		cp++;
	if (*cp < '0' || *cp > '9')
		return 0;
	strtoul((char *)cp, (char **)rp, 0);
	if (!WHITESPACE(**rp)) {
		*rp = cp;
		return 0;
	}
	return 1;
}

int
isrtf(void)
{
	if(strstr((char *)buf, "\\rtf1")){
		print(mime ? "application/rtf\n" : "rich text format\n");
		return 1;
	}
	return 0;
}

int
ismsdos(void)
{
	if (buf[0] == 0x4d && buf[1] == 0x5a){
		print(mime ? "application/x-msdownload\n" : "MSDOS executable\n");
		return 1;
	}
	return 0;
}

int
isicocur(void)
{
	if(buf[0] || buf[1] || buf[3] || buf[9])
		return 0;
	if(buf[4] == 0x00 && buf[5] == 0x00)
		return 0;
	switch(buf[2]){
	case 1:
		print(mime ? "image/x-icon\n" : "Microsoft icon file\n");
		return 1;
	case 2:
		print(mime ? "image/x-icon\n" : "Microsoft cursor file\n");
		return 1;
	}
	return 0;
}

int
iself(void)
{
	static char *cpu[] = {		/* NB: incomplete and arbitary list */
	[1]	"WE32100",
	[2]	"SPARC",
	[3]	"i386",
	[4]	"M68000",
	[5]	"M88000",
	[6]	"i486",
	[7]	"i860",
	[8]	"R3000",
	[9]	"S370",
	[10]	"R4000",
	[15]	"HP-PA",
	[18]	"sparc v8+",
	[19]	"i960",
	[20]	"PPC-32",
	[21]	"PPC-64",
	[40]	"ARM",
	[41]	"Alpha",
	[43]	"sparc v9",
	[50]	"IA-64",
	[62]	"AMD64",
	[75]	"VAX",
	};
	static char *type[] = {
	[1]	"relocatable object",
	[2]	"executable",
	[3]	"shared library",
	[4]	"core dump",
	};

	if (memcmp(buf, "\x7fELF", 4) == 0){
		if (!mime){
			int isdifend = 0;
			int n = (buf[19] << 8) | buf[18];
			char *p = "unknown";
			char *t = "unknown";

			if (n > 0 && n < nelem(cpu) && cpu[n])
				p = cpu[n];
			else {
				/* try the other byte order */
				isdifend = 1;
				n = (buf[18] << 8) | buf[19];
				if (n > 0 && n < nelem(cpu) && cpu[n])
					p = cpu[n];
			}
			if(isdifend)
				n = (buf[16]<< 8) | buf[17];
			else
				n = (buf[17]<< 8) | buf[16];

			if(n>0 && n < nelem(type) && type[n])
				t = type[n];
			print("%s ELF %s\n", p, t);
		}
		else
			print("application/x-elf-executable\n");
		return 1;
	}

	return 0;
}

int
isface(void)
{
	int i, j, ldepth, l;
	char *p;

	ldepth = -1;
	for(j = 0; j < 3; j++){
		for(p = (char*)buf, i=0; i<3; i++){
			if(p[0] != '0' || p[1] != 'x')
				return 0;
			if(buf[2+8] == ',')
				l = 2;
			else if(buf[2+4] == ',')
				l = 1;
			else
				return 0;
			if(ldepth == -1)
				ldepth = l;
			if(l != ldepth)
				return 0;
			strtoul(p, &p, 16);
			if(*p++ != ',')
				return 0;
			while(*p == ' ' || *p == '\t')
				p++;
		}
		if (*p++ != '\n')
			return 0;
	}

	if(mime)
		print("application/x-face\n");
	else
		print("face image depth %d\n", ldepth);
	return 1;
}
