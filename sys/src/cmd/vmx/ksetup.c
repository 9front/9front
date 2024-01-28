#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <libsec.h>
#include "dat.h"
#include "fns.h"
#include "x86.h"

static uchar hdr[8192];
static int fd;

extern int bootmodn;
extern char **bootmod;
extern int cmdlinen;
extern char **cmdlinev;
extern VgaMode *curmode, textmode;
extern uintptr fbaddr, fbsz;

static int elf64;

static int
biostype(Region *r)
{
	return r->type >> 8 & 0xff;
}

static void *
pack(void *v, char *fmt, ...)
{
	uchar *p;
	va_list va;
	
	p = v;
	va_start(va, fmt);
	for(; *fmt != 0; fmt++)
		switch(*fmt){
		case '.': p++; break;
		case 's': PUT16(p, 0, va_arg(va, int)); p += 2; break;
		case 'i': PUT32(p, 0, va_arg(va, u32int)); p += 4; break;
		case 'v': PUT64(p, 0, va_arg(va, u64int)); p += 8; break;
		case 'z': if(elf64) {PUT64(p, 0, va_arg(va, uintptr)); p += 8;} else {PUT32(p, 0, va_arg(va, uintptr)); p += 4;} break;
		default: sysfatal("pack: unknown fmt character %c", *fmt);
		}
	va_end(va);
	return p;
}

static int
putmmap(uchar *p0)
{
	uchar *p;
	Region *r;
	int t;
	
	p = p0;
	for(r = mmap; r != nil; r = r->next){
		t = biostype(r);
		if(t == 0) continue;
		if(gavail(p) < 24) sysfatal("out of guest memory");
		p = pack(p, "ivvi", 20, (uvlong) r->start, (uvlong)(r->end - r->start), t);
	}
	return (uchar *) p - p0;
}

static int
putcmdline(uchar *p0)
{
	int i;
	char *p, *e;
	
	if(cmdlinen == 0) return 0;
	p = (char*)p0;
	e = gend(p0);
	if(p >= e) return 0;
	for(i = 0; i < cmdlinen; i++){
		p = strecpy(p, e, cmdlinev[i]);
		if(i != cmdlinen - 1) *p++ = ' ';
	}
	return p - (char*)p0 + 1;
}

static int
putmods(uchar *p0)
{
	int i, fd, rc;
	u32int *p;
	uchar *q;
	char dummy;

	if(bootmodn == 0) return 0;
	p = (u32int*)p0;
	q = (uchar*)(p + 4 * bootmodn);
	for(i = 0; i < bootmodn; i++){
		q = gptr(-(-gpa(q) & -BY2PG), 1);
		if(q == nil) sysfatal("out of guest memory");
		fd = open(bootmod[i], OREAD);
		if(fd == -1) sysfatal("module open: %r");
		p[0] = gpa(q);
		rc = readn(fd, q, gavail(q));
		if(rc < 0) sysfatal("module read: %r");
		if(read(fd, &dummy, 1) == 1) sysfatal("out of guest memory");
		close(fd);
		q += rc;
		p[1] = gpa(q);
		p[2] = 0;
		p[3] = 0;
		p += 4;
	}
	bootmodn = ((uchar*)p - p0) / 16;
	return q - p0;
}

static int
trymultiboot(void)
{
	u32int *p, flags;
	u32int header, load, loadend, bssend, entry;
	u32int filestart;
	uchar *gp;
	uchar *modp;
	int len;
	int rc;

	for(p = (u32int*)hdr; p < (u32int*)hdr + sizeof(hdr)/4; p++)
		if(*p == 0x1badb002)
			break;
	if(p == (u32int*)hdr + sizeof(hdr)/4)
		return 0;
	if((u32int)(p[0] + p[1] + p[2]) != 0)
		sysfatal("invalid multiboot checksum");
	flags = p[1];
	if((flags & 1<<16) == 0)
		sysfatal("no size info in multiboot header");
	header = p[3];
	load = p[4];
	loadend = p[5];
	bssend = p[6];
	entry = p[7];
	filestart = (uchar*)p - hdr - (header - load);
	gp = gptr(load, bssend != 0 ? bssend - load : loadend != 0 ? loadend - load : BY2PG);
	if(gp == nil)
		sysfatal("kernel image out of bounds");
	seek(fd, filestart, 0);
	if(loadend == 0){
		rc = readn(fd, gp, gavail(gp));
		if(rc <= 0) sysfatal("readn: %r");
		loadend = load + rc;
	}else{
		rc = readn(fd, gp, loadend - load);
		if(rc < 0) sysfatal("readn: %r");
		if(rc < loadend - load) sysfatal("short kernel image");
	}
	if(bssend == 0) bssend = loadend;
	bssend = -(-bssend & -BY2PG);
	p = gptr(bssend, 128);
	if(p == nil) sysfatal("no space for multiboot structure");
	memset(p, 0, 128);
	p[0] = 1<<0;
	p[1] = gavail(gptr(0, 0)) >> 10;
	if(p[1] > 640) p[1] = 640;
	p[2] = gavail(gptr(1048576, 0)) >> 10;	
	modp = gptr(bssend + 128, 1);
	if(modp == nil) sysfatal("out of guest memory");
	len = putmmap(modp);
	if(len != 0){
		p[0] |= 1<<6;
		p[11] = len;
		p[12] = gpa(modp);
		modp += len;
	}
	len = putcmdline(modp);
	if(len != 0){
		p[0] |= 1<<2;
		p[4] = gpa(modp);
		modp += len + 7 & -8;
	}
	len = putmods(modp);
	if(len != 0){
		p[0] |= 1<<3;
		p[5] = bootmodn;
		p[6] = gpa(modp);
		modp += len + 7 & -8;
	}

	if(curmode != nil && curmode != &textmode){
		int i, o, n;
		u16int r, g, b;

		o = 0;
		r = g = b = 0;
		for(i = 0; i < 4; i++){
			n = curmode->chan >> 8*i & 0xf;
			if(n == 0) continue;
			switch(curmode->chan >> 4 + 8*i & 0xf){
			case CRed:	r = o | n<<8; break;
			case CGreen:	g = o | n<<8; break;
			case CBlue:	b = o | n<<8; break;
			}
			o += n;
		}
		p[0] |= 1<<12;
		pack(&p[22], "viiiisss", (u64int)fbaddr,
			curmode->hbytes, curmode->w, curmode->h,
			chantodepth(curmode->chan) | 1<<8, r, g, b);
	}

	USED(modp);
	rset(RPC, entry);
	rset(RAX, 0x2badb002);
	rset(RBX, bssend);
	return 1;
}

typedef struct ELFHeader ELFHeader;
struct ELFHeader {
	uintptr entry, phoff, shoff;
	u32int flags;
	u16int ehsize;
	u16int phentsize, phnum;
	u16int shentsize, shnum, shstrndx;
};
typedef struct ELFPHeader ELFPHeader;
struct ELFPHeader {
	u32int type, flags;
	uintptr offset, vaddr, paddr, filesz, memsz, align;
};
enum {
	PT_NULL, PT_LOAD, PT_DYNAMIC, PT_INTERP, PT_NOTE,
	PT_SHLIB, PT_PHDR, PT_TLS,
	PT_GNU_EH_FRAME = 0x6474e550,
	PT_GNU_RELRO = 0x6474e552,
	PT_OPENBSD_RANDOMIZE = 0x65a3dbe6,
};
typedef struct ELFSHeader ELFSHeader;
struct ELFSHeader {
	u32int iname, type;
	uintptr flags, addr, offset, size;
	u32int link, info;
	uintptr addralign, entsize;
	char *name;
};
enum {
	SHT_SYMTAB = 2,
	SHT_STRTAB = 3,
	SHF_ALLOC = 2,
};
typedef struct ELFSymbol ELFSymbol;
struct ELFSymbol {
	u32int iname;
	uintptr addr, size;
	u8int info, other;
	u16int shndx;
	char *name;
};
static ELFHeader eh;
static ELFPHeader *ph;
static ELFSHeader *sh;
static ELFSymbol *sym;
static int nsym;
static uintptr elfmax;

static u64int
elff(uchar **p, uchar *e, int sz)
{
	u64int rc;

	if(sz == -1)
		sz = elf64 ? 8 : 4;
	if(*p + sz > e){
		fprint(2, "out of bounds: %#p > %#p", *p + sz, e);
		return 0;
	}
	switch(sz){
	case 1: rc = GET8(*p, 0); break;
	case 2: rc = GET16(*p, 0); break;
	case 3: case 4: rc = GET32(*p, 0); break;
	default: rc = GET64(*p, 0); break;
	}
	*p += sz;
	return rc;
}

static void
elfheader(ELFHeader *eh, uchar *p, uchar *e)
{
	eh->entry = elff(&p, e, -1);
	eh->phoff = elff(&p, e, -1);
	eh->shoff = elff(&p, e, -1);
	eh->flags = elff(&p, e, 4);
	eh->ehsize = elff(&p, e, 2);
	eh->phentsize = elff(&p, e, 2);
	eh->phnum = elff(&p, e, 2);
	eh->shentsize = elff(&p, e, 2);
	eh->shnum = elff(&p, e, 2);
	eh->shstrndx = elff(&p, e, 2);
}

static void
elfpheader(ELFPHeader *ph, uchar *p, uchar *e)
{
	ph->type = elff(&p, e, 4);
	if(elf64) ph->flags = elff(&p, e, 4);
	ph->offset = elff(&p, e, -1);
	ph->vaddr = elff(&p, e, -1);
	ph->paddr = elff(&p, e, -1);
	ph->filesz = elff(&p, e, -1);
	ph->memsz = elff(&p, e, -1);
	if(!elf64) ph->flags = elff(&p, e, 4);
	ph->align = elff(&p, e, -1);
}

static void
elfsheader(ELFSHeader *sh, uchar *p, uchar *e)
{
	sh->iname = elff(&p, e, 4);
	sh->type = elff(&p, e, 4);
	sh->flags = elff(&p, e, -1);
	sh->addr = elff(&p, e, -1);
	sh->offset = elff(&p, e, -1);
	sh->size = elff(&p, e, -1);
	sh->link = elff(&p, e, 4);
	sh->info = elff(&p, e, 4);
	sh->addralign = elff(&p, e, -1);
	sh->entsize = elff(&p, e, -1);
}

static void
elfsymbol(ELFSymbol *s, uchar *p, uchar *e)
{
	s->iname = elff(&p, e, 4);
	if(elf64){
		s->info = elff(&p, e, 1);
		s->other = elff(&p, e, 1);
		s->shndx = elff(&p, e, 2);
		s->addr = elff(&p, e, -1);
		s->size = elff(&p, e, -1);
	}else{
		s->addr = elff(&p, e, -1);
		s->size = elff(&p, e, -1);
		s->info = elff(&p, e, 1);
		s->other = elff(&p, e, 1);
		s->shndx = elff(&p, e, 2);
	}
}

static void
epreadn(void *buf, ulong sz, vlong off, char *fn)
{
	seek(fd, off, 0);
	werrstr("eof");
	if(readn(fd, buf, sz) < sz)
		sysfatal("%s: read: %r", fn);
}

static int
elfheaders(void)
{
	uchar *buf;
	int i;
	ELFSHeader *s;

	if(GET32(hdr, 0) != 0x464c457f) return 0;
	if(hdr[5] != 1 || hdr[6] != 1 || hdr[0x14] != 1) return 0;
	switch(hdr[4]){
	case 1: elf64 = 0; break;
	case 2: elf64 = 1; if(sizeof(uintptr) == 4) sysfatal("64-bit binaries not supported on 32-bit host"); break;
	default: return 0;
	}
	elfheader(&eh, hdr + 0x18, hdr + sizeof(hdr));
	buf = emalloc(eh.phentsize > eh.shentsize ? eh.phentsize : eh.shentsize);
	ph = emalloc(sizeof(ELFPHeader) * eh.phnum);
	for(i = 0; i < eh.phnum; i++){
		epreadn(buf, eh.phentsize, eh.phoff + i * eh.phentsize, "elfheaders");
		elfpheader(&ph[i], buf, buf + eh.phentsize);
	}
	sh = emalloc(sizeof(ELFSHeader) * eh.shnum);
	for(i = 0; i < eh.shnum; i++){
		epreadn(buf, eh.shentsize, eh.shoff + i * eh.shentsize, "elfheaders");
		elfsheader(&sh[i], buf, buf + eh.shentsize);
	}
	free(buf);
	
	if(eh.shstrndx != 0 && eh.shstrndx < eh.shnum){
		s = &sh[eh.shstrndx];
		if(s->type != SHT_STRTAB)
			sysfatal("elfheaders: section string table is not a string table");
		buf = emalloc(s->size + 1);
		epreadn(buf, s->size, s->offset, "elfheaders");
		for(i = 0; i < eh.shnum; i++){
			if(sh[i].iname < s->size)
				sh[i].name = (char *) &buf[sh[i].iname];
		}
	}
	
	return 1;
}

static void
elfdata(void)
{
	int i;
	void *v;

	for(i = 0; i < eh.phnum; i++){
		switch(ph[i].type){
		case PT_NULL:
		case PT_GNU_RELRO:
		case PT_NOTE:
			continue;
		case PT_DYNAMIC:
		case PT_INTERP:
			sysfatal("elf: dynamically linked");
		default:
			sysfatal("elf: unknown program header type %#ux", (int)ph[i].type);
		case PT_LOAD:
		case PT_PHDR:
		case PT_OPENBSD_RANDOMIZE:
			break;
		}
		v = gptr(ph[i].paddr, ph[i].memsz);
		if(v == nil)
			sysfatal("invalid address %#p (length=%#p) in elf", (void*)ph[i].paddr, (void*)ph[i].memsz);
		if(ph[i].type == PT_OPENBSD_RANDOMIZE)
			genrandom(v, ph[i].memsz);
		else{
			if(ph[i].filesz > ph[i].memsz)
				sysfatal("elf: header entry shorter in memory than in the file (%#p < %#p)", (void*)ph[i].memsz, (void*)ph[i].filesz);
			if(ph[i].filesz != 0)
				epreadn(v, ph[i].filesz, ph[i].offset, "elfdata");
			if(ph[i].filesz < ph[i].memsz)
				memset((uchar*)v + ph[i].filesz, 0, ph[i].memsz - ph[i].filesz);
		}
		if(ph[i].paddr + ph[i].memsz > elfmax)
			elfmax = ph[i].paddr + ph[i].memsz;
	}
}

static int
elfsymbols(void)
{
	ELFSHeader *s, *sy, *st;
	char *str;
	uchar *buf, *p;
	int i;
	
	sy = nil;
	st = nil;
	for(s = sh; s < sh + eh.shnum; s++){
		if(s->type == SHT_SYMTAB && s->name != nil && strcmp(s->name, ".symtab") == 0) 
			sy = s;
		if(s->type == SHT_STRTAB && s->name != nil && strcmp(s->name, ".strtab") == 0)
			st = s;
	}
	if(sy == nil || st == nil)
		return 0;
	if(sy->entsize == 0 || (sy->size % sy->entsize) != 0)
		sysfatal("symbol section: invalid headers");
	str = emalloc(st->size);
	epreadn(str, st->size, st->offset, "elfsymbols");
	buf = emalloc(sy->size);
	epreadn(buf, sy->size, sy->offset, "elfsymbols");
	nsym = sy->size / sy->entsize;
	sym = emalloc(sizeof(ELFSymbol) * nsym);
	for(i = 0; i < nsym; i++){
		p = buf + i * sy->entsize;
		elfsymbol(sym + i, p, p + sy->entsize);
		if(sym[i].iname < st->size)
			sym[i].name = &str[sym[i].iname];
	}
	free(buf);
	return 1;
}

static ELFSymbol *
elfsym(char *n)
{
	ELFSymbol *s;
	
	for(s = sym; s < sym + nsym; s++)
		if(s->name != nil && strcmp(s->name, n) == 0)
			return s;
	return nil;
}

static void *
symaddr(ELFSymbol *s)
{
	ELFPHeader *p;

	if(s == nil) return nil;
	for(p = ph; p < ph + eh.phnum; p++)
		if(s->addr >= p->vaddr && s->addr < p->vaddr + p->memsz)
			return gptr(p->paddr + (s->addr - p->vaddr), s->size);
	return nil;
}

static uchar *obsdarg, *obsdarg0, *obsdargnext;
static int obsdarglen;
static int obsdconsdev = 12 << 8, obsddbcons = -1, obsdbootdev;

enum {
	BOOTARG_MEMMAP,
	BOOTARG_DISKINFO,
	BOOTARG_APMINFO,
	BOOTARG_CKSUMLEN,
	BOOTARG_PCIINFO,
	BOOTARG_CONSDEV,
	BOOTARG_SMPINFO,
	BOOTARG_BOOTMAC,
	BOOTARG_DDB,
	BOOTARG_BOOTDUID,
	BOOTARG_BOOTSR,
	BOOTARG_EFIINFO,
	BOOTARG_END = -1,
};

static void
obsdelfload(void)
{
	void *v, *w, *hdrfix;
	int shentsize;
	int saddr;
	ELFSHeader *s;
	uintptr off;
	
	saddr = elf64 ? 8 : 4;
	elfmax = -(-elfmax & -saddr);
	v = gptr(elfmax, eh.ehsize);
	if(v == nil)
space:		sysfatal("out of space for kernel");
	epreadn(v, eh.ehsize, 0, "obsdelfload");
	elfmax += -(-eh.ehsize & -saddr);
	hdrfix = (uchar*)v + (elf64 ? 0x20 : 0x1c);
	
	shentsize = 40 + 24*elf64;
	v = gptr(elfmax, shentsize * eh.shnum);
	if(v == nil) goto space;
	off = shentsize * eh.shnum;
	elfmax += off;
	off += -(-eh.ehsize & -saddr);
	for(s = sh; s < sh + eh.shnum; s++)
		if(s->type == SHT_SYMTAB || s->type == SHT_STRTAB ||
		s->name != nil && (strcmp(s->name, ".debug_line") == 0 || strcmp(s->name, ".SUNW_ctf") == 0)){
			w = gptr(elfmax, s->size);
			if(w == nil) goto space;
			epreadn(w, s->size, s->offset, "obsdelfload");
			v = pack(v, "iizzzziizz",
				s->iname, s->type, s->flags | SHF_ALLOC, (uintptr)0,
				off, s->size, s->link, s->info, s->addralign, s->entsize);
			elfmax += -(-s->size & -saddr);
			off += -(-s->size & -saddr);
		}else{
			memset(v, 0, shentsize);
			v = (uchar*)v + shentsize;
		}
	pack(hdrfix, "zz......sss", (uintptr)0, -(-eh.ehsize & -saddr), 0, 0, shentsize);
}

#define obsdpack(...) (obsdarg = pack(obsdarg, __VA_ARGS__))

static void
obsdstart(int type)
{
	obsdarg0 = obsdarg;
	PUT32(obsdarg, 0, type);
	PUT32(obsdarg, 8, 0); /* next */
	obsdarg += 12;
}

static void
obsdend(void)
{
	if(obsdarg == obsdarg0 + 12) obsdarg += 4;
	PUT32(obsdarg0, 4, obsdarg - obsdarg0); /* size */
	obsdarglen += obsdarg - obsdarg0;
	PUT32(obsdargnext, 0, gpa(obsdarg0));
	obsdargnext = obsdarg0 + 8;
	obsdarg0 = nil;	
}

static void
obsdfb(void)
{
	int i, s, p;
	u32int r, g, b, a, m;

	if(curmode == nil || curmode == &textmode) return;
	p = r = g = b = a = 0;
	for(i = 0; i < 4; i++){
		s = curmode->chan >> 8 * i & 0xf;
		if(s == 0) continue;
		m = (1<<s)-1 << p;
		p += s;
		switch(curmode->chan >> 4 + 8 * i & 0xf){
		case CRed: r |= m; break;
		case CGreen: g |= m; break;
		case CBlue: b |= m; break;
		case CAlpha: case CIgnore: a |= m; break;
		default: return;
		}
	}
	obsdstart(BOOTARG_EFIINFO);
	obsdpack("vvvviiiiiii", 0ULL, 0ULL, (uvlong)fbaddr, (uvlong)fbsz, curmode->h, curmode->w, curmode->w, r, g, b, a);
	obsdend();
}

static void
obsdargs(void)
{
	Region *r;
	int t;

	obsdstart(BOOTARG_MEMMAP);
	for(r = mmap; r != nil; r = r->next){
		t = biostype(r);
		if(t == 0) continue;
		obsdpack("vvi", (uvlong)r->start, (uvlong)(r->end - r->start), t);
	}
	obsdpack("vvi", 0ULL, 0ULL, 0);
	obsdend();
	obsdstart(BOOTARG_CONSDEV); obsdpack("iiii", obsdconsdev, -1, -1, 0); obsdend();
	if(obsddbcons != -1){
		obsdstart(BOOTARG_DDB); obsdpack("i", obsddbcons); obsdend();
	}
	obsdfb();
	obsdstart(BOOTARG_END); obsdend();
}

static int
obsdcmdline(int argc, char **argv)
{
	char *p;
	char *q, *r;
	int howto;
	
	howto = 0;
	while(argc-- > 0){
		p = *argv++;
		if(*p == '-'){
			while(*++p != 0)
				switch(*p){
				case 'a': howto |= 0x0001; break; /* RB_ASKNAME */
				case 's': howto |= 0x0002; break; /* RB_SINGLE */
				case 'd': howto |= 0x0040; break; /* RB_DDB */
				case 'c': howto |= 0x0400; break; /* RB_CONFIG */
				default: goto usage;
				}
			continue;
		}
		q = strchr(p, '=');
		if(q == nil) goto usage;
		*q++ = 0;
		if(strcmp(p, "device") == 0){
			obsdbootdev = 0;
			switch(*q){
			case 'w': break;
			case 'f': obsdbootdev = 2; break;
			case 's': obsdbootdev = 4; break;
			case 'c': obsdbootdev = 6; break;
			case 'r': obsdbootdev = 17; break;
			case 'v': obsdbootdev = 14; if(*++q != 'n') goto nodev; break;
			default: nodev: sysfatal("invalid device");
			}
			if(*++q != 'd') goto nodev;
			obsdbootdev |= strtoul(++q, &r, 10) << 16;
			if(r == q || (obsdbootdev & 0xfff00000) != 0) goto nodev;
			if(*r < 'a' || *r > 'p') goto nodev;
			obsdbootdev |= *r - 'a' << 8;
			if(*++r != 0) goto nodev;
			obsdbootdev |= 0xa0000000;
		}else if(strcmp(p, "tty") == 0){
			if(strcmp(q, "com0") == 0)
				obsdconsdev = 8 << 8;
			else if(strcmp(q, "com1") == 0)
				obsdconsdev = 8 << 8 | 1;
			else if(strcmp(q, "pc0") == 0)
				obsdconsdev = 12 << 8;
			else
				sysfatal("tty must be one of com0, com1, pc0");
		}else if(strcmp(p, "db_console") == 0){
			if(strcmp(q, "on") == 0)
				obsddbcons = 1;
			else if(strcmp(q, "off") == 0)
				obsddbcons = 0;
			else
				sysfatal("db_console must be one of on, off");
		}else goto usage;
	}
	return howto;
usage:
	fprint(2, "openbsd cmdline usage: kernel [-asdc] [var=value ...]\nsupported vars: device tty db_console\n");
	threadexitsall("usage");
}

static int
obsdload(void)
{
	int sp;
	int howto;
	uchar *v;
	
	obsdelfload();
	sp = 0xfffc;
	sp -= 36;
	v = gptr(sp, 36);
	howto = obsdcmdline(cmdlinen, cmdlinev);
	assert(v != nil);
	PUT32(v, 4, howto); /* howto */
	PUT32(v, 8, obsdbootdev); /* bootdev */
	PUT32(v, 12, 0xa); /* bootapiver */
	PUT32(v, 16, elfmax); /* esym */
	PUT32(v, 20, 0); /* extmem */
	PUT32(v, 24, 0); /* cnvmem */
	PUT32(v, 32, 0); /* bootargv */
	obsdarg = gptr(0x10000, 4096);
	assert(obsdarg != nil);
	obsdargnext = &v[32]; /* bootargv */
	obsdargs();
	assert(obsdarg0 == nil);
	PUT32(v, 28, obsdarglen); /* bootargc */
	rset(RSP, sp);
	rset(RPC, (u32int)eh.entry & 0x0fffffff);
	return 1;
}

static int
tryelf(void)
{
	char *s, *t;

	if(!elfheaders()) return 0;
	elfdata();
	if(!elfsymbols()) return 0;
	s = symaddr(elfsym("ostype"));
	if(s != nil && strcmp(s, "OpenBSD") == 0)
		return obsdload();
	/* from 6.9 up, bsd.rd has just these syms */
	s = symaddr(elfsym("rd_root_image"));
	t = symaddr(elfsym("rd_root_size"));
	if(s != nil && t != nil)
		return obsdload();
	return 0;
}

static void
linuxbootmod(char *fn, void *zp, u32int kend)
{
	u32int addr;
	uintptr memend;
	int fd;
	vlong sz;
	void *v;
	int rc;
	
	fd = open(fn, OREAD);
	if(fd < 0) sysfatal("linux: initrd: open: %r");
	sz = seek(fd, 0, 2);
	if(sz < 0) sysfatal("linux: initrd: seek: %r");
	if(sz == 0) sysfatal("linux: empty initrd");
	addr = GET32(zp, 0x22c);
	memend = (1<<20) + gavail(gptr(1<<20, 0));
	if(addr >= memend) addr = memend - 1;
	if((addr - (sz - 1) & -4) < kend) sysfatal("linux: no room for initrd");
	addr = addr - (sz - 1) & -4;
	v = gptr(addr, sz);
	if(v == nil) sysfatal("linux: initrd: gptr failed");
	seek(fd, 0, 0);
	rc = readn(fd, v, sz);
	if(rc < 0) sysfatal("linux: initrd: read: %r");
	if(rc < sz) sysfatal("linux: initrd: short read");
	close(fd);
	PUT32(zp, 0x218, addr);
	PUT32(zp, 0x21C, sz);
}

static void
linuxscreeninfo(void *zp)
{
	extern VgaMode *curmode, textmode;
	extern uintptr fbaddr, fbsz;
	uintptr extmem;
	int i, p, s;
	
	extmem = gavail(gptr(1<<20, 0)) >> 10;
	if(extmem >= 65535) extmem = 65535;
	PUT16(zp, 0x02, extmem);
	
	if(curmode == nil) return;
	if(curmode == &textmode){
		PUT8(zp, 0x06, 3); /* mode 3 */
		PUT8(zp, 0x07, 80); /* 80 cols */
		PUT8(zp, 0x0e, 25); /* 25 rows */
		PUT8(zp, 0x0f, 0x22); /* VGA */
		PUT16(zp, 0x10, 16); /* characters are 16 pixels high */
	}else{
		PUT8(zp, 0x0f, 0x23); /* VESA linear framebuffer */
		PUT16(zp, 0x12, curmode->w);
		PUT16(zp, 0x14, curmode->h);
		PUT16(zp, 0x16, chantodepth(curmode->chan));
		PUT32(zp, 0x18, fbaddr);
		PUT32(zp, 0x1C, fbsz);
		PUT16(zp, 0x24, curmode->hbytes);
		for(i = 0, p = 0; i < 4; i++){
			s = curmode->chan >> 8 * i & 15;
			if(s == 0) continue;
			switch(curmode->chan >> 8 * i + 4 & 15){
			case CRed: PUT16(zp, 0x26, s | p << 8); break;
			case CGreen: PUT16(zp, 0x28, s | p << 8); break;
			case CBlue: PUT16(zp, 0x2a, s | p << 8); break;
			case CAlpha: case CIgnore:  PUT16(zp, 0x2c, s | p << 8); ; break;
			}
			p += s;
		}
		PUT16(zp, 0x34, 1<<0|1<<1|1<<3|1<<4|1<<5|1<<6|1<<7); /* attributes */
	}
}

static void
linuxgdt(void *v)
{
	u32int base;
	
	base = gpa(v);
	rset("gdtrbase", base);
	v = pack(v, "vvvv", 0, 0,
		GDTBASE(0) | GDTLIM(-1) | GDTRX | GDTG | GDTP | GDT32,
		GDTBASE(0) | GDTLIM(-1) | GDTRW | GDTG | GDTP | GDT32
	);
	rset("gdtrlimit", gpa(v) - base - 1);
	rset("cs", 0x10);
	rset("ds", 0x18);
	rset("es", 0x18);
	rset("ss", 0x18);
}

static void
linuxe820(uchar *zp)
{
	Region *r;
	uchar *v;
	int t;
	int n;
	
	v = zp + 0x2d0;
	n = 1;
	for(r = mmap; r != nil; r = r->next){
		t = biostype(r);
		if(t == 0) continue;
		v = pack(v, "vvi", r->start, r->end - r->start, t);
		n++;
	}
	PUT8(zp, 0x1e8, n);
}

static int
trylinux(void)
{
	char buf[1024];
	u8int loadflags;
	u16int version;
	uchar *zp;
	void *v;
	u32int ncmdline, cmdlinemax, syssize, setupsects;
	
	seek(fd, 0, 0);
	if(readn(fd, buf, sizeof(buf)) < 1024) return 0;
	if(GET16(buf, 0x1FE) != 0xAA55 || GET32(buf, 0x202) != 0x53726448) return 0;
	version = GET16(buf, 0x206);
	if(version < 0x206){
		vmerror("linux: kernel too old (boot protocol version %d.%.2d, needs to be 2.06 or newer)", version >> 8, version & 0xff);
		return 0;
	}
	loadflags = GET8(buf, 0x211);
	if((loadflags & 1) == 0){
		vmerror("linux: zImage is not supported");
		return 0;
	}
	zp = gptr(0x1000, 0x1000);
	if(zp == nil) sysfatal("linux: gptr for zeropage failed");
	rset(RSI, 0x1000);
	memset(zp, 0, 0x1000);
	memmove(zp + 0x1f1, buf + 0x1f1, 0x202 + GET8(buf, 0x201) - 0x1f1);
	setupsects = GET8(zp, 0x1F1);
	if(setupsects == 0) setupsects = 4;
	syssize = GET32(zp, 0x1F4);
	cmdlinemax = GET32(zp, 0x238);
	
	v = gptr(1<<20, syssize << 4);
	if(v == nil) sysfatal("linux: not enough room for kernel");
	epreadn(v, syssize << 4, (setupsects + 1) * 512, "trylinux");
	
	v = gptr(0x20000, 1);
	if(v == nil) sysfatal("linux: gptr for cmdline failed");
	ncmdline = putcmdline(v);
	if(ncmdline == 0)
		*(uchar*)v = 0;
	else
		if(ncmdline - 1 > cmdlinemax) sysfatal("linux: cmdline too long (%d > %d)", ncmdline, cmdlinemax);
	PUT32(zp, 0x228, 0x20000);
	
	switch(bootmodn){
	case 0: break;
	default:
		vmerror("linux: ignoring extra boot modules (only one supported)");
		/* wet floor */
	case 1:
		linuxbootmod(*bootmod, zp, (1<<20) + (syssize << 4));
	}
	
	linuxscreeninfo(zp);
	v = gptr(0x3000, 256);
	if(v == nil) sysfatal("linux: gptr for gdt failed");
	linuxgdt(v);
	
	linuxe820(zp);
	
	PUT16(zp, 0x1FA, 0xffff);
	PUT8(zp, 0x210, 0xFF); /* bootloader ID */
	PUT8(zp, 0x211, loadflags | 0x80); /* kernel can use heap */

	PUT32(zp, 0x224, 0xfe00); /* kernel can use full segment */
	rset(RPC, GET32(zp, 0x214));
	rset(RBP, 0);
	rset(RDI, 0);
	rset(RBX, 0);
	return 1;
}


void
loadkernel(char *fn)
{
	fd = open(fn, OREAD);
	if(fd < 0) sysfatal("open: %r");
	if(readn(fd, hdr, sizeof(hdr)) <= 0)
		sysfatal("readn: %r");
	if(trymultiboot())
		goto done;
	if(tryelf())
		goto done;
	if(trylinux())
		goto done;
	sysfatal("%s: unknown format", fn);
done:
	close(fd);
}
