#include <u.h>
#include <libc.h>
#include <bio.h>
#include "tapefs.h"

/*
 * File system for cpio tapes (read-only)
 */

union hblock {
	char tbuf[Maxbuf];
} dblock;

typedef  void HdrReader(Fileinf *);

Biobuf	*tape;

static void
addrfatal(char *fmt, va_list arg)
{
	char buf[1024];

	vseprint(buf, buf+sizeof(buf), fmt, arg);
	fprint(2, "%s: %#llx: %s\n", argv0, Bseek(tape, 0, 1), buf);
	exits(buf);
}

static int
egetc(void)
{
	int c;

	if((c = Bgetc(tape)) == Beof)
		sysfatal("unexpected eof");
	if(c < 0)
		sysfatal("read error: %r");
	return c;
}

static ushort
rd16le()
{
	ushort x;

	return x = egetc(), x |= egetc()<<8;
}

static ulong
rd3211()
{
	ulong x;

	return x = egetc()<<16, x |= egetc()<<24, x |= egetc(), x |= egetc()<<8;
}

/* sysvr3 and sysvr4 skip records with names longer than 256. pwb 1.0,
32V, sysiii, sysvr1, and sysvr2 overrun their 256 byte buffer */
static void
rdpwb(Fileinf *f, ushort (*rd16)(void), ulong (*rd32)(void))
{
	int namesz, n;
	static char buf[256];

	rd16();	/* dev */
	rd16();	/* ino */
	f->mode = rd16();
	f->uid = rd16();
	f->gid = rd16();
	rd16();	/* nlink */
	rd16();	/* rdev */
	f->mdate = rd32();
	namesz = rd16();
	f->size = rd32();

	/* namesz include the trailing nul */
	if(namesz == 0)
		sysfatal("name too small");
	if(namesz > sizeof(buf))
		sysfatal("name too big");

	if((n = Bread(tape, buf, namesz)) < 0)
		sysfatal("read error: %r");
	if(n < namesz)
		sysfatal("unexpected eof");

	if(buf[n-1] != '\0')
		sysfatal("no nul after file name");
	if((n = strlen(buf)) != namesz-1)
		sysfatal("mismatched name length: saw %d; expected %d", n, namesz-1);
	f->name = buf;

	/* skip padding */
	if(Bseek(tape, 0, 1) & 1)
		egetc();
}

static void
rdpwb11(Fileinf *f)
{
	rdpwb(f, rd16le, rd3211);
}

static vlong
rdasc(int n)
{
	vlong x;
	int y;

	for(x = 0; n > 0; n--) {
		if((y = egetc() - '0') & ~7)
			sysfatal("not octal");
		x = x<<3 | y;
	}
	return x;
}

/* sysvr3 and sysvr4 skip records with names longer than 256. sysiii,
sysvr1, and sysvr2 overrun their 256 byte buffer */
static void
rdsysiii(Fileinf *f)
{
	int namesz, n;
	static char buf[256];

	rdasc(6);	/* dev */
	rdasc(6);	/* ino */
	f->mode = rdasc(6);
	f->uid = rdasc(6);
	f->gid = rdasc(6);
	rdasc(6);	/* nlink */
	rdasc(6);	/* rdev */
	f->mdate = rdasc(11);
	namesz = rdasc(6);
	f->size = rdasc(11);

	/* namesz includes the trailing nul */
	if(namesz == 0)
		sysfatal("name too small");
	if(namesz > sizeof (buf))
		sysfatal("name too big");

	if((n = Bread(tape, buf, namesz)) < 0)
		sysfatal("read error: %r");
	if(n < namesz)
		sysfatal("unexpected eof");

	if(buf[n-1] != '\0')
		sysfatal("no nul after file name");
	if((n = strlen(buf)) != namesz-1)
		sysfatal("mismatched name length: saw %d; expected %d", n, namesz-1);
	f->name = buf;
}

static HdrReader *
rdmagic(void)
{
	uchar buf[6];

	buf[0] = egetc();
	buf[1] = egetc();
	if(buf[0] == 0xc7 && buf[1] == 0x71)
		return rdpwb11;

	buf[2] = egetc();
	buf[3] = egetc();
	buf[4] = egetc();
	buf[5] = egetc();
	if(memcmp(buf, "070707", 6) == 0)
		return rdsysiii;

	sysfatal("Out of phase--get MERT help");
	return nil;
}

void
populate(char *name)
{
	HdrReader *rdhdr, *prevhdr;
	Fileinf f;

	/* the tape buffer may not be the ideal size for scanning the
	record headers */
	if((tape = Bopen(name, OREAD)) == nil)
		sysfatal("Can't open argument file");

	extern void (*_sysfatal)(char *, va_list);
	_sysfatal = addrfatal;

	prevhdr = nil;
	replete = 1;
	for(;;) {
		/* sysiii and sysv implementations don't allow
		multiple header types within a single tape, so we
		won't either */
		rdhdr = rdmagic();
		if(prevhdr != nil && rdhdr != prevhdr)
			sysfatal("mixed headers");
		rdhdr(&f);

		while(f.name[0] == '/')
			f.name++;
		if(f.name[0] == '\0')
			sysfatal("nameless record");
		if(strcmp(f.name, "TRAILER!!!") == 0)
			break;
		switch(f.mode & 0170000) {
		case 0040000:
			f.mode = DMDIR | f.mode&0777;
			break;
		case 0100000:	/* normal file */
		case 0120000:	/* symlink */
			f.mode &= 0777;
			break;
		default:	/* sockets, pipes, devices */
			f.mode = 0;
			break;
		}
		f.addr = Bseek(tape, 0, 1);
		poppath(f, 1);

		Bseek(tape, f.size, 1);

		/* skip padding */
		if(rdhdr == rdpwb11 && (Bseek(tape, 0, 1) & 1))
			egetc();
	}
}

void
dotrunc(Ram *r)
{
	USED(r);
}

void
docreate(Ram *r)
{
	USED(r);
}

char *
doread(Ram *r, vlong off, long cnt)
{
	Bseek(tape, r->addr+off, 0);
	if (cnt>sizeof(dblock.tbuf))
		sysfatal("read too big");
	Bread(tape, dblock.tbuf, cnt);
	return dblock.tbuf;
}

void
popdir(Ram *r)
{
	USED(r);
}

void
dowrite(Ram *r, char *buf, long off, long cnt)
{
	USED(r); USED(buf); USED(off); USED(cnt);
}

int
dopermw(Ram *r)
{
	USED(r);
	return 0;
}
