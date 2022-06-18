#include <u.h>
#include <libc.h>
#include <flate.h>

int infd, outfd;
ulong dcrc;
ulong *tab;
uchar buf[65536];

enum {
	IH_TYPE_SCRIPT		= 6,
};

void
put(uchar *p, u32int v)
{
	*p++ = v >> 24;
	*p++ = v >> 16;
	*p++ = v >> 8;
	*p = v;
}

void
usage(void)
{
	fprint(2, "usage: %s a.out\n", argv0);
	exits("usage");
}

void
block(int n)
{
	int rc;

	rc = readn(infd, buf, n);
	if(rc < 0) sysfatal("read: %r");
	if(rc < n) sysfatal("input file truncated");
	if(write(outfd, buf, n) < 0) sysfatal("write: %r");
	dcrc = blockcrc(tab, dcrc, buf, n);
}

void
copy(int n)
{
	int i;

	for(i = sizeof(buf) - 1; i < n; i += sizeof(buf))
		block(sizeof(buf));
	i = n & sizeof(buf) - 1;
	if(i > 0)
		block(i);
}

void
main(int argc, char **argv)
{
	uchar header[64];
	char *ofile;
	Dir *dir;

	ofile = nil;
	ARGBEGIN {
	case 'o': ofile = strdup(EARGF(usage())); break;
	default: usage();
	} ARGEND;

	if(argc == 0)
		infd = 0;
	else {
		if(argc != 1) usage();
		infd = open(argv[0], OREAD);
		if(infd < 0) sysfatal("infd: %r");
	}
	dir = dirfstat(infd);
	if(dir == nil) sysfatal("stat: %r");
	if(dir->length > 0xFFFFFFFF-8) sysfatal("file too big");
	if(ofile == nil) ofile = smprint("%s.u", dir->name);
	outfd = create(ofile, OWRITE|OTRUNC, 0666);
	if(outfd < 0) sysfatal("create: %r");
	
	tab = mkcrctab(0xEDB88320);	
	seek(outfd, sizeof(header), 0);
	put(buf+0, dir->length);
	put(buf+4, 0);
	dcrc = blockcrc(tab, 0, buf, 8);
	if(write(outfd, buf, 8) != 8) sysfatal("write: %r");
	copy(dir->length);

	memset(header, 0, sizeof(header));
	put(&header[0], 0x27051956); /* magic */
	put(&header[8], time(0)); /* time */
	put(&header[12], 8+dir->length); /* image size */
	put(&header[16], 0); /* load address */
	put(&header[20], 0); /* entry point */
	put(&header[24], dcrc); /* data crc */
	header[28] = 0;
	header[29] = 0;
	header[30] = IH_TYPE_SCRIPT;
	header[31] = 0; /* compressed = no */
	
	strncpy((char*)&header[32], dir->name, sizeof(header)-32);
	put(&header[4], blockcrc(tab, 0, header, sizeof(header)));
	
	seek(outfd, 0, 0);
	if(write(outfd, header, sizeof(header)) < sizeof(header)) sysfatal("write: %r");
	
	exits(nil);
}
