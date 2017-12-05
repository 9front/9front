/*
 * edisk - edit gpt disk partition table
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <disk.h>
#include "edit.h"
#include <mp.h>
#include <libsec.h>

#define TB (1024LL*GB)
#define GB (1024*1024*1024)
#define MB (1024*1024)
#define KB (1024)

typedef struct Header Header;
typedef struct Entry Entry;

typedef struct Type Type;
typedef struct Flag Flag;
typedef struct Gptpart Gptpart;

struct Header
{
	uchar	sig[8];
	uchar	rev[4];
	uchar	hdrsiz[4];
	uchar	hdrcrc[4];
	uchar	zero[4];
	uchar	selflba[8];
	uchar	backlba[8];
	uchar	firstlba[8];
	uchar	lastlba[8];
	uchar	devid[16];
	uchar	tablba[8];
	uchar	entrycount[4];
	uchar	entrysize[4];
	uchar	tabcrc[4];
};

struct Entry
{
	uchar	typeid[16];
	uchar	partid[16];
	uchar	firstlba[8];
	uchar	lastlba[8];
	uchar	attr[8];
	uchar	name[72];
};

enum {
	Headersiz = 92,
	Entrysiz = 16+16+8+8+8+72,
};


struct Type {
	uchar	uuid[16];
	char	*name;
	char	*desc;
};

struct Flag {
	uvlong	f;
	char	c;
	char	*desc;
};

struct Gptpart {
	Part;
	Type	*type;	/* nil when not in use */
	uvlong	attr;
	uchar	uuid[16];
	Rune	label[72+1];
	char	namebuf[8];
};

static uchar	*pmbr;
static Header	*phdr;
static Header	*bhdr;

static vlong	partoff;
static vlong	partend;

static Gptpart	*parts;
static int	nparts;

static uchar	devid[16];
static uchar	zeros[16];

/* RFC4122, but in little endian format */
#define UU(a,b,c,d,e) { \
	(a)&255,((a)>>8)&255,((a)>>16)&255,((a)>>24)&255, \
	(b)&255,((b)>>8)&255, \
	(c)&255,((c)>>8)&255, \
	((d)>>8)&255,(d)&255, \
	((e)>>40)&255, ((e)>>32)&255, ((e)>>24)&255, ((e)>>16)&255, ((e)>>8)&255, (e)&255}

static Type	types[256] = {
{UU(0x00000000,0x0000,0x0000,0x0000,0x000000000000ULL), "", "Unused entry"},
{UU(0x024DEE41,0x33E7,0x11D3,0x9D69,0x0008C781F39FULL), "mbr", "MBR partition"},
{UU(0xC12A7328,0xF81F,0x11D2,0xBA4B,0x00A0C93EC93BULL), "esp", "EFI System Partition"},
{UU(0x21686148,0x6449,0x6E6F,0x744E,0x656564454649ULL), "bios", "BIOS boot partition"},
{UU(0xD3BFE2DE,0x3DAF,0x11DF,0xBA40,0xE3A556D89593ULL), "iffs", "Intel Fast Flash"},
{UU(0xF4019732,0x066E,0x4E12,0x8273,0x346C5641494FULL), "sony", "Sony boot"},
{UU(0xBFBFAFE7,0xA34F,0x448A,0x9A5B,0x6213EB736C22ULL), "lenovo", "Lenovo boot"},
{UU(0xE3C9E316,0x0B5C,0x4DB8,0x817D,0xF92DF00215AEULL), "msr", "Microsoft Reserved Partition"},
{UU(0xEBD0A0A2,0xB9E5,0x4433,0x87C0,0x68B6B72699C7ULL), "dos", "Microsoft Basic data"},
{UU(0x5808C8AA,0x7E8F,0x42E0,0x85D2,0xE1E90434CFB3ULL), "ldmm", "Logical Disk Manager metadata"},
{UU(0xAF9B60A0,0x1431,0x4F62,0xBC68,0x3311714A69ADULL), "ldmd", "Logical Disk Manager data"},
{UU(0xDE94BBA4,0x06D1,0x4D40,0xA16A,0xBFD50179D6ACULL), "recovery", "Windows Recovery Environment"},
{UU(0x37AFFC90,0xEF7D,0x4E96,0x91C3,0x2D7AE055B174ULL), "gpfs", "IBM General Parallel File System"},
{UU(0xE75CAF8F,0xF680,0x4CEE,0xAFA3,0xB001E56EFC2DULL), "storagespaces", "Storage Spaces"},
{UU(0x75894C1E,0x3AEB,0x11D3,0xB7C1,0x7B03A0000000ULL), "hpuxdata", "HP-UX Data"},
{UU(0xE2A1E728,0x32E3,0x11D6,0xA682,0x7B03A0000000ULL), "hpuxserv", "HP-UX Service"},
{UU(0x0FC63DAF,0x8483,0x4772,0x8E79,0x3D69D8477DE4ULL), "linuxdata", "Linux Data"},
{UU(0xA19D880F,0x05FC,0x4D3B,0xA006,0x743F0F84911EULL), "linuxraid", "Linux RAID"},
{UU(0x0657FD6D,0xA4AB,0x43C4,0x84E5,0x0933C84B4F4FULL), "linuxswap", "Linux Swap"},
{UU(0xE6D6D379,0xF507,0x44C2,0xA23C,0x238F2A3DF928ULL), "linuxlvm", "Linux Logical Volume Manager"},
{UU(0x933AC7E1,0x2EB4,0x4F13,0xB844,0x0E14E2AEF915ULL), "linuxhome", "Linux /home"},
{UU(0x3B8F8425,0x20E0,0x4F3B,0x907F,0x1A25A76F98E8ULL), "linuxsrv", "Linux /srv"},
{UU(0x7FFEC5C9,0x2D00,0x49B7,0x8941,0x3EA10A5586B7ULL), "linuxcrypt", "Linux Plain dm-crypt"},
{UU(0xCA7D7CCB,0x63ED,0x4C53,0x861C,0x1742536059CCULL), "luks", "LUKS"},
{UU(0x8DA63339,0x0007,0x60C0,0xC436,0x083AC8230908ULL), "linuxreserved", "Linux Reserved"},
{UU(0x83BD6B9D,0x7F41,0x11DC,0xBE0B,0x001560B84F0FULL), "fbsdboot", "FreeBSD Boot"},
{UU(0x516E7CB4,0x6ECF,0x11D6,0x8FF8,0x00022D09712BULL), "fbsddata", "FreeBSD Data"},
{UU(0x516E7CB5,0x6ECF,0x11D6,0x8FF8,0x00022D09712BULL), "fbsdswap", "FreeBSD Swap"},
{UU(0x516E7CB6,0x6ECF,0x11D6,0x8FF8,0x00022D09712BULL), "fbsdufs", "FreeBSD Unix File System"},
{UU(0x516E7CB8,0x6ECF,0x11D6,0x8FF8,0x00022D09712BULL), "fbsdvvm", "FreeBSD Vinum volume manager"},
{UU(0x516E7CBA,0x6ECF,0x11D6,0x8FF8,0x00022D09712BULL), "fbsdzfs", "FreeBSD ZFS"},
{UU(0x48465300,0x0000,0x11AA,0xAA11,0x00306543ECACULL), "applehfs", "Apple HFS+"},
{UU(0x55465300,0x0000,0x11AA,0xAA11,0x00306543ECACULL), "appleufs", "Apple UFS"},
{UU(0x6A898CC3,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "applezfs", "Apple ZFS"},
{UU(0x52414944,0x0000,0x11AA,0xAA11,0x00306543ECACULL), "appleraid", "Apple RAID"},
{UU(0x52414944,0x5F4F,0x11AA,0xAA11,0x00306543ECACULL), "appleraidoff", "Apple RAID, offline"},
{UU(0x426F6F74,0x0000,0x11AA,0xAA11,0x00306543ECACULL), "appleboot", "Apple Boot"},
{UU(0x4C616265,0x6C00,0x11AA,0xAA11,0x00306543ECACULL), "applelabel", "Apple Label"},
{UU(0x5265636F,0x7665,0x11AA,0xAA11,0x00306543ECACULL), "appletv", "Apple TV Recovery"},
{UU(0x53746F72,0x6167,0x11AA,0xAA11,0x00306543ECACULL), "applecs", "Apple Core Storage"},
{UU(0x6A82CB45,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solarisboot", "Solaris Boot"},
{UU(0x6A85CF4D,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solarisroot", "Solaris Root"},
{UU(0x6A87C46F,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solarisswap", "Solaris Swap"},
{UU(0x6A8B642B,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solarisbakup", "Solaris Backup"},
{UU(0x6A898CC3,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solarisusr", "Solaris /usr"},
{UU(0x6A8EF2E9,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solarisvar", "Solaris /var"},
{UU(0x6A90BA39,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solarishome", "Solaris /home"},
{UU(0x6A9283A5,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solarisalt", "Solaris Alternate sector"},
{UU(0x6A945A3B,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solaris", "Solaris Reserved"},
{UU(0x6A9630D1,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solaris", "Solaris Reserved"},
{UU(0x6A980767,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solaris", "Solaris Reserved"},
{UU(0x6A96237F,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solaris", "Solaris Reserved"},
{UU(0x6A8D2AC7,0x1DD2,0x11B2,0x99A6,0x080020736631ULL), "solaris", "Solaris Reserved"},
{UU(0x49F48D32,0xB10E,0x11DC,0xB99B,0x0019D1879648ULL), "nbsdswap", "NetBSD Swap"},
{UU(0x49F48D5A,0xB10E,0x11DC,0xB99B,0x0019D1879648ULL), "nbsdffs", "NetBSD FFS"},
{UU(0x49F48D82,0xB10E,0x11DC,0xB99B,0x0019D1879648ULL), "nbsdlfs", "NetBSD LFS"},
{UU(0x49F48DAA,0xB10E,0x11DC,0xB99B,0x0019D1879648ULL), "nbsdraid", "NetBSD RAID"},
{UU(0x2DB519C4,0xB10F,0x11DC,0xB99B,0x0019D1879648ULL), "nbsdcat", "NetBSD Concatenated"},
{UU(0x2DB519EC,0xB10F,0x11DC,0xB99B,0x0019D1879648ULL), "nbsdcrypt", "NetBSD Encrypted"},
{UU(0xFE3A2A5D,0x4F32,0x41A7,0xB725,0xACCC3285A309ULL), "chromeoskern", "ChromeOS kernel"},
{UU(0x3CB8E202,0x3B7E,0x47DD,0x8A3C,0x7FF2A13CFCECULL), "chromeosroot", "ChromeOS rootfs"},
{UU(0x2E0A753D,0x9E48,0x43B0,0x8337,0xB15192CB1B5EULL), "chromeos", "ChromeOS future use"},
{UU(0x42465331,0x3BA3,0x10F1,0x802A,0x4861696B7521ULL), "haikubfs", "Haiku BFS"},
{UU(0x85D5E45E,0x237C,0x11E1,0xB4B3,0xE89A8F7FC3A7ULL), "midbsdboot", "MidnightBSD Boot"},
{UU(0x85D5E45A,0x237C,0x11E1,0xB4B3,0xE89A8F7FC3A7ULL), "midbsddata", "MidnightBSD Data"},
{UU(0x85D5E45B,0x237C,0x11E1,0xB4B3,0xE89A8F7FC3A7ULL), "midbsdswap", "MidnightBSD Swap"},
{UU(0x0394EF8B,0x237E,0x11E1,0xB4B3,0xE89A8F7FC3A7ULL), "midbsdufs", "MidnightBSD Unix File System"},
{UU(0x85D5E45C,0x237C,0x11E1,0xB4B3,0xE89A8F7FC3A7ULL), "midbsdvvm", "MidnightBSD Vinum volume manager"},
{UU(0x85D5E45D,0x237C,0x11E1,0xB4B3,0xE89A8F7FC3A7ULL), "midbsdzfs", "MidnightBSD ZFS"},
{UU(0x45B0969E,0x9B03,0x4F30,0xB4C6,0xB4B80CEFF106ULL), "cephjournal", "Ceph Journal"},
{UU(0x45B0969E,0x9B03,0x4F30,0xB4C6,0x5EC00CEFF106ULL), "cephcrypt", "Ceph dm-crypt Encrypted Journal"},
{UU(0x4FBD7E29,0x9D25,0x41B8,0xAFD0,0x062C0CEFF05DULL), "cephosd", "Ceph OSD"},
{UU(0x4FBD7E29,0x9D25,0x41B8,0xAFD0,0x5EC00CEFF05DULL), "cephcryptosd", "Ceph dm-crypt OSD"},
{UU(0x824CC7A0,0x36A8,0x11E3,0x890A,0x952519AD3F61ULL), "openbsd", "OpenBSD Data"},
{UU(0xCEF5A9AD,0x73BC,0x4601,0x89F3,0xCDEEEEE321A1ULL), "qnx6", "QNX6 Power-safe file system"},
{UU(0xC91818F9,0x8025,0x47AF,0x89D2,0xF030D7000C2CULL), "plan9", "Plan 9"},
{UU(0x9D275380,0x40AD,0x11DB,0xBF97,0x000C2911D1B8ULL), "vmwareesxcore", "VMware ESX vmkcore (coredump)"},
{UU(0xAA31E02A,0x400F,0x11DB,0x9590,0x000C2911D1B8ULL), "vmwareesxvmfs", "VMware ESX VMFS filesystem"},
{UU(0x9198EFFC,0x31C0,0x11DB,0x8F78,0x000C2911D1B8ULL), "vmwareesxrsv", "VMware ESX reserved"},
{UU(0x2568845D,0x2332,0x4675,0xBC39,0x8FA5A4748D15ULL), "androidiabootloader", "Android-IA Bootloader"},
{UU(0x114EAFFE,0x1552,0x4022,0xB26E,0x9B053604CF84ULL), "androidiabootloader2", "Android-IA Bootloader 2"},
{UU(0x49A4D17F,0x93A3,0x45C1,0xA0DE,0xF50B2EBE2599ULL), "androidiaboot", "Android-IA Boot"},
{UU(0x4177C722,0x9E92,0x4AAB,0x8644,0x43502BFD5506ULL), "androidiarecovery", "Android-IA Recovery"},
{UU(0xEF32A33B,0xA409,0x486C,0x9141,0x9FFB711F6266ULL), "androidiamisc", "Android-IA Misc"},
{UU(0x20AC26BE,0x20B7,0x11E3,0x84C5,0x6CFDB94711E9ULL), "androidiametadata", "Android-IA Metadata"},
{UU(0x38F428E6,0xD326,0x425D,0x9140,0x6E0EA133647CULL), "androidiasystem", "Android-IA System"},
{UU(0xA893EF21,0xE428,0x470A,0x9E55,0x0668FD91A2D9ULL), "androidiacache", "Android-IA Cache"},
{UU(0xDC76DDA9,0x5AC1,0x491C,0xAF42,0xA82591580C0DULL), "androidiadata", "Android-IA Data"},
{UU(0xEBC597D0,0x2053,0x4B15,0x8B64,0xE0AAC75F4DB1ULL), "androidiapersistent", "Android-IA Persistent"},
{UU(0x8F68CC74,0xC5E5,0x48DA,0xBE91,0xA0C8C15E9C80ULL), "androidiafactory", "Android-IA Factory"},
{UU(0x767941D0,0x2085,0x11E3,0xAD3B,0x6CFDB94711E9ULL), "androidiafastboot", "Android-IA Fastboot"},
{UU(0xAC6D7924,0xEB71,0x4DF8,0xB48D,0xE267B27148FFULL), "androidiaoem", "Android-IA OEM"},
{UU(0x7412F7D5,0xA156,0x4B13,0x81DC,0x867174929325ULL), "onieboot", "ONIE boot"},
{UU(0xD4E6E2CD,0x4469,0x46F3,0xB5CB,0x1BFF57AFC149ULL), "onieconfig", "ONIE configuration"},
{UU(0x9E1A2D38,0xC612,0x4316,0xAA26,0x8B49521E5A8BULL), "powerpcprep", "PowerPC PReP boot"},
{UU(0xBC13C2FF,0x59E6,0x4262,0xA352,0xB275FD6F7172ULL), "freedesktopos", "Freedesktop shared boot loader configuration"},
{UU(0x734E5AFE,0xF61A,0x11E6,0xBC64,0x92361F002671ULL), "ataritos", "Atari TOS"},
};

static Flag	flags[] = {
	{ 0x0000000000000001ULL, 'S', "system" },
	{ 0x0000000000000002ULL, 'E', "efi-hidden" },
	{ 0x0000000000000004ULL, 'A', "active" },
	{ 0x2000000000000000ULL, 'R', "read-only" },
	{ 0x4000000000000000ULL, 'H', "hidden" },
	{ 0x8000000000000000ULL, 'M', "nomount" },
	{ 0, 0, nil }
};

static void initcrc32(void);
static u32int sumcrc32(u32int, uchar *, ulong);

static u32int getle32(void*);
static void putle32(void*, u32int);
static u64int getle64(void *);
static void putle64(void *, u64int);

static void uugen(uchar uuid[16]);
static Type* gettype(uchar uuid[16], char *name);
static int uufmt(Fmt*);
#pragma	varargck	type	"U"	uchar*

static int attrfmt(Fmt*);
#pragma varargck	type	"A"	uvlong

static void rdpart(Edit*);
static void autopart(Edit*);
static void blankpart(Edit*);
static void cmdnamectl(Edit*);

static int blank;
static int dowrite;
static int file;
static int rdonly;
static int doauto;
static int printflag;
static int written;

static void 	cmdsum(Edit*, Part*, vlong, vlong);
static char 	*cmdadd(Edit*, char*, vlong, vlong);
static char 	*cmddel(Edit*, Part*);
static char 	*cmdext(Edit*, int, char**);
static char 	*cmdhelp(Edit*);
static char 	*cmdokname(Edit*, char*);
static char 	*cmdwrite(Edit*);
static void	cmdprintctl(Edit*, int);

Edit edit = {
	.add =		cmdadd,
	.del =		cmddel,
	.ext =		cmdext,
	.help =		cmdhelp,
	.okname =	cmdokname,
	.sum =		cmdsum,
	.write =	cmdwrite,
	.printctl =	cmdprintctl,
	.unit =		"sector",
};

void
usage(void)
{
	fprint(2, "usage: disk/edisk [-abfprw] [-s sectorsize] /dev/sdC0/data\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	vlong secsize;

	fmtinstall('U', uufmt);
	fmtinstall('A', attrfmt);

	initcrc32();

	secsize = 0;
	ARGBEGIN{
	case 'a':
		doauto++;
		break;
	case 'b':
		blank++;
		break;
	case 'f':
		file++;
		break;
	case 'p':
		printflag++;
		break;
	case 'r':
		rdonly++;
		break;
	case 's':
		secsize = atoi(ARGF());
		break;
	case 'v':
		break;
	case 'w':
		dowrite++;
		break;
	}ARGEND;

	if(argc != 1)
		usage();

	edit.disk = opendisk(argv[0], rdonly, file);
	if(edit.disk == nil) {
		fprint(2, "cannot open disk: %r\n");
		exits("opendisk");
	}

	if(secsize != 0) {
		edit.disk->secsize = secsize;
		edit.disk->secs = edit.disk->size / secsize;
	}
	edit.unitsz = edit.disk->secsize;
	edit.end = edit.disk->secs;

	if(blank)
		blankpart(&edit);
	else
		rdpart(&edit);

	if(doauto)
		autopart(&edit);

	if(dowrite)
		runcmd(&edit, "w");

	if(printflag)
		runcmd(&edit, "P");

	if(dowrite || printflag)
		exits(0);

	runcmd(&edit, "p");
	for(;;) {
		fprint(2, ">>> ");
		runcmd(&edit, getline(&edit));
	}
}


typedef struct Block Block;
struct Block
{
	Block	*link;
	Disk	*disk;
	uchar	*save;	/* saved backup data */
	vlong	addr;
	uchar	data[];
};

static Block *blocks;

static void*
getblock(Disk *disk, vlong addr)
{
	Block *b;

	if(addr < 0 || addr >= disk->secs)
		abort();

	for(b = blocks; b != nil; b = b->link){
		if(b->addr == addr && b->disk == disk)
			return b->data;
	}
	b = malloc(sizeof(Block) + 2*disk->secsize);
	if(pread(disk->fd, b->data, disk->secsize, disk->secsize*addr) != disk->secsize){
		sysfatal("getblock %llud: %r", addr);
		return nil;
	}
	b->save = &b->data[disk->secsize];
	memmove(b->save, b->data, disk->secsize);

	b->addr = addr;
	b->link = blocks;
	b->disk = disk;
	blocks = b;
	return b->data;
}

static void
flushdisk(Disk *disk)
{
	Block *b, *r;

	if(disk->wfd < 0)
		return;

	for(b = blocks; b != nil; b = b->link){
		if(b->disk != disk || memcmp(b->data, b->save, disk->secsize) == 0)
			continue;
		if(pwrite(disk->wfd, b->data, disk->secsize, b->addr*disk->secsize) != disk->secsize){
			fprint(2, "error writing lba %llud: %r\n", b->addr);
			goto Recover;
		}
	}
	return;

Recover:
	for(r = blocks; r != b; r = r->link){
		if(r->disk != disk || memcmp(r->data, r->save, disk->secsize) == 0)
			continue;
		pwrite(disk->wfd, r->save, disk->secsize, r->addr*disk->secsize);
	}
	exits("recovered");
}


static u32int crc32tab[256];

static void
initcrc32(void)
{
	u32int c;
	int n, k;

	for(n = 0; n < 256; n++){
		c = n;
		for(k = 0; k < 8; k++)
			if((c & 1) != 0)
				c = 0xedb88320 ^ c >> 1;
			else
				c >>= 1;
		crc32tab[n] = c;
	}
}
static u32int
sumcrc32(u32int c, uchar *buf, ulong len)
{
	c = ~c;
	while(len-- != 0)
		c = crc32tab[(*buf++ ^ c) & 0xff] ^ c >> 8;
	return ~c;
}


static u32int
getle32(void* v)
{
	uchar *p;

	p = v;
	return (p[3]<<24)|(p[2]<<16)|(p[1]<<8)|p[0];
}

static void
putle32(void* v, u32int i)
{
	uchar *p;

	p = v;
	p[0] = i;
	p[1] = i>>8;
	p[2] = i>>16;
	p[3] = i>>24;
}

static u64int
getle64(void *v)
{
	return ((u64int)getle32((uchar*)v + 4) << 32) | getle32(v);
}

static void
putle64(void *v, u64int i)
{
	putle32(v, i);
	putle32((uchar*)v + 4, i >> 32);
}


static void
uugen(uchar uu[16])
{
	genrandom(uu, 16);
	uu[7] = (uu[7] & ~0xF0) | 0x40;
	uu[8] = (uu[8] & ~0xC0) | 0x80;
}

static int
uufmt(Fmt *fmt)
{
	uchar *uu = va_arg(fmt->args, uchar*);
	return fmtprint(fmt,
		"%.2uX%.2uX%.2uX%.2uX-"
		"%.2uX%.2uX-"
		"%.2uX%.2uX-"
		"%.2uX%.2uX-%.2uX%.2uX%.2uX%.2uX%.2uX%.2uX",
/* Data1 */	uu[3], uu[2], uu[1], uu[0], 
/* Data2 */	uu[5], uu[4],
/* Data3 */	uu[7], uu[6],
/* Data4 */	uu[8], uu[9], uu[10], uu[11], uu[12], uu[13], uu[14], uu[15]);
}


static int
attrfmt(Fmt *fmt)
{
	uvlong a = va_arg(fmt->args, uvlong);
	char s[64+1], *p;
	Flag *f;

	p = s;
	for(f=flags; f->c != '\0'; f++){
		if(a & f->f)
			*p = f->c;
		else
			*p = '-';
		p++;
	}
	*p = '\0';
	return fmtprint(fmt, "%s", s);
}


static Header*
readhdr(Disk *disk, vlong lba)
{
	Header *hdr;
	u32int crc;
	int siz;

	if(lba < 0)
		lba += disk->secs;

	hdr = getblock(disk, lba);
	if(memcmp(hdr->sig, "EFI PART", 8) != 0)
		return nil;
	if(getle64(hdr->selflba) != lba)
		return nil;
	siz = getle32(hdr->hdrsiz);
	if(siz < Headersiz || siz > disk->secsize)
		return nil;
	crc = getle32(hdr->hdrcrc);
	putle32(hdr->hdrcrc, 0);
	putle32(hdr->hdrcrc, sumcrc32(0, (uchar*)hdr, siz));
	if(getle32(hdr->hdrcrc) != crc){
		putle32(hdr->hdrcrc, crc);
		return nil;
	}

	return hdr;
}

static void
partname(Edit *, Gptpart *p)
{
	snprint(p->namebuf, sizeof(p->namebuf), "p%d", (int)(p - parts)+1);
	p->name = p->namebuf;
}

static char*
readent(Edit *edit, Entry *ent, Gptpart *p)
{
	int i;

	memset(p, 0, sizeof(*p));
	if(memcmp(ent->typeid, zeros, 16) == 0)
		return nil;

	p->type = gettype(ent->typeid, nil);
	memmove(p->uuid, ent->partid, 16);
	p->start = getle64(ent->firstlba);
	p->end = getle64(ent->lastlba)+1;
	p->attr = getle64(ent->attr);
	for(i=0; i<nelem(p->label)-1; i++)
		p->label[i] = ent->name[i*2] | (Rune)ent->name[i*2+1]<<8;
	p->label[i] = 0;
	partname(edit, p);

	return addpart(edit, p);
}

static Entry*
getent(Disk *disk, vlong tablba, int entsize, int i)
{
	int ent2blk;
	uchar *blkp;

	ent2blk = disk->secsize / entsize;
	blkp = getblock(disk, tablba + (i/ent2blk));
	blkp += entsize * (i%ent2blk);
	return (Entry*)blkp;
}

static int
readtab(Edit *edit, Header *hdr)
{
	int entries, entsize, i;
	vlong tablba;
	u32int crc;
	Entry *ent;
	char *err;

	entries = getle32(hdr->entrycount);
	entsize = getle32(hdr->entrysize);
	if(entsize < Entrysiz || entsize > edit->disk->secsize)
		return -1;

	crc = 0;
	tablba = getle64(hdr->tablba);
	for(i=0; i<entries; i++){
		ent = getent(edit->disk, tablba, entsize, i);
		crc = sumcrc32(crc, (uchar*)ent, entsize);
	}
	if(getle32(hdr->tabcrc) != crc)
		return -1;

	nparts = entries;
	parts = emalloc(nparts*sizeof(parts[0]));

	partoff = getle64(hdr->firstlba);
	partend = getle64(hdr->lastlba)+1;

	edit->dot = partoff;
	edit->end = partend;

	for(i=0; i<nparts; i++){
		ent = getent(edit->disk, tablba, entsize, i);
		if((err = readent(edit, ent, &parts[i])) != nil)
			fprint(2, "readtab: %s\n", err);
	}

	return 0;
}

static char*
checkhdr(Header *a, Header *b)
{
	if(memcmp(a->sig, b->sig, sizeof(a->sig)) != 0)
		return "signature";
	if(memcmp(a->rev, b->rev, sizeof(a->rev)) != 0)
		return "revision";
	if(memcmp(a->hdrsiz, b->hdrsiz, sizeof(a->hdrsiz)) != 0)
		return "header size";
	if(memcmp(a->selflba, b->backlba, sizeof(a->selflba)) != 0
	|| memcmp(a->backlba, b->selflba, sizeof(a->backlba)) != 0)
		return "backup lba/self lba";
	if(memcmp(a->firstlba, b->firstlba, sizeof(a->firstlba)) != 0)
		return "first lba";
	if(memcmp(a->lastlba, b->lastlba, sizeof(a->lastlba)) != 0)
		return "last lba";
	if(memcmp(a->devid, b->devid, sizeof(a->devid)) != 0)
		return "device guid";
	if(memcmp(a->entrycount, b->entrycount, sizeof(a->entrycount)) != 0)
		return "entry count";
	if(memcmp(a->entrysize, b->entrysize, sizeof(a->entrysize)) != 0)
		return "entry size";
	if(memcmp(a->tabcrc, b->tabcrc, sizeof(a->tabcrc)) != 0)
		return "table checksum";
	return nil;
}

static Header*
getbakhdr(Edit *edit, Header *bhdr)
{
	vlong lba, blba, tlba;
	Header *hdr;
	int siz;

	siz = getle32(bhdr->hdrsiz);
	lba = getle64(bhdr->backlba);

	if(!blank){
		char *mismatch;

		mismatch = "data";
		hdr = readhdr(edit->disk, lba);
		if(hdr != nil && (mismatch = checkhdr(bhdr, hdr)) == nil)
			return hdr;
		fprint(2, "backup header at lba %lld has mismatching %s, restoring.\n",
			lba, mismatch);
	}

	hdr = getblock(edit->disk, lba);
	memmove(hdr, bhdr, siz);
	putle64(hdr->selflba, lba);
	blba = getle64(bhdr->selflba);
	putle64(hdr->backlba, blba);
	if(lba <= blba)
		tlba = lba+1;
	else
		tlba = partend;
	putle64(hdr->tablba, tlba);
	edit->changed = 1;

	return hdr;
}

typedef struct Tentry Tentry;
struct Tentry {
	uchar	active;			/* active flag */
	uchar	starth;			/* starting head */
	uchar	starts;			/* starting sector */
	uchar	startc;			/* starting cylinder */
	uchar	type;			/* partition type */
	uchar	endh;			/* ending head */
	uchar	ends;			/* ending sector */
	uchar	endc;			/* ending cylinder */
	uchar	lba[4];			/* starting LBA */
	uchar	size[4];		/* size in sectors */
};

enum {
	NTentry = 4,
	Tentrysiz = 16,
};

static uchar*
readmbr(Disk *disk)
{
	int dosparts, protected;
	uchar *mbr, *magic;
	Tentry *t;
	int i;

	mbr = getblock(disk, 0);
	magic = &mbr[disk->secsize - 2];
	if(magic[0] != 0x55 || magic[1] != 0xAA)
		sysfatal("did not find master boot record");

	dosparts = protected = 0;
	for(i=0; i<NTentry; i++){
		t = (Tentry*)&mbr[disk->secsize - 2 - (i+1)*Tentrysiz];
		switch(t->type){
		case 0xEE:
			protected = 1;
		case 0xEF:
		case 0x00:
			continue;
		}
		dosparts++;
	}

	if(dosparts && protected && !(printflag || rdonly))
		sysfatal("potential hybrid MBR/GPT detected, not editing");

	if(dosparts && !protected)
		sysfatal("dos partition table in use and no protective partition found");

	return mbr;
}

static void
rdpart(Edit *edit)
{
	pmbr = readmbr(edit->disk);
	if((phdr = readhdr(edit->disk, 1)) != nil && readtab(edit, phdr) == 0){
		memmove(devid, phdr->devid, 16);
		bhdr = getbakhdr(edit, phdr);
		return;
	}
	if((bhdr = readhdr(edit->disk, -1)) != nil && readtab(edit, bhdr) == 0){
		memmove(devid, bhdr->devid, 16);
		phdr = getbakhdr(edit, bhdr);
		return;
	}
	sysfatal("did not find partition table");
}

static Header*
inithdr(Disk *disk)
{
	vlong tabsize, baklba;
	Header *hdr;

	tabsize = (Entrysiz*nparts + disk->secsize-1) / disk->secsize;
	if(tabsize < 1)
		tabsize = 1;

	baklba = disk->secs-1;
	partend = baklba - tabsize;
	partoff = 2 + tabsize;

	if(partoff >= partend)
		sysfatal("disk too small for partition table");

	hdr = getblock(disk, 1);
	memset(hdr, 0, Headersiz);

	memmove(hdr->sig, "EFI PART", 8);
	putle32(hdr->rev, 0x10000);
	putle32(hdr->hdrsiz, Headersiz);
	putle32(hdr->hdrcrc, 0);
	putle64(hdr->selflba, 1);
	putle64(hdr->backlba, baklba);
	putle64(hdr->firstlba, partoff);
	putle64(hdr->lastlba, partend-1);
	memmove(hdr->devid, devid, 16);
	putle64(hdr->tablba, 2);
	putle32(hdr->entrycount, nparts);
	putle32(hdr->entrysize, Entrysiz);
	putle32(hdr->tabcrc, 0);

	return hdr;
}

static uchar*
initmbr(Disk *disk)
{
	uchar *mbr, *magic;
	u32int size;
	Tentry *t;

	mbr = getblock(disk, 0);

	magic = &mbr[disk->secsize - 2];
	magic[0] = 0x55;
	magic[1] = 0xAA;

	t = (Tentry*)&mbr[disk->secsize - 2 - NTentry*Tentrysiz];
	memset(t, 0, NTentry * Tentrysiz);

	t->type = 0xEE;
	t->active = 0;

	size = (disk->secs - 1) > 0xFFFFFFFF ? 0xFFFFFFFF : (disk->secs - 1);
	putle32(t->lba, 1);
	putle32(t->size, size);

	t->starth = 0;
	t->startc = 0;
	t->starts = 1;
	t->endh = disk->h-1;
	t->ends = (disk->s & 0x3F) | (((disk->c-1)>>2) & 0xC0);
	t->endc = disk->c-1;

	return mbr;
}

static void
blankpart(Edit *edit)
{
	nparts = 128;
	parts = emalloc(nparts*sizeof(parts[0]));

	uugen(devid);
	pmbr = initmbr(edit->disk);
	phdr = inithdr(edit->disk);
	bhdr = getbakhdr(edit, phdr);

	edit->dot = partoff;
	edit->end = partend;

	edit->changed = 1;
}

static void
writeent(Entry *ent, Gptpart *p)
{
	int i;

	if(p->type == nil)
		return;
	memmove(ent->typeid, p->type->uuid, 16);
	memmove(ent->partid, p->uuid, 16);
	putle64(ent->firstlba, p->start);
	putle64(ent->lastlba, p->end-1);
	putle64(ent->attr, p->attr);
	for(i=0; i<nelem(ent->name)/2; i++){
		ent->name[i*2] = p->label[i] & 0xFF;
		ent->name[i*2+1] = p->label[i] >> 8;
	}
}

static void
writetab(Edit *edit, Header *hdr)
{
	int hdrsize, entsize, i;
	vlong tablba;
	u32int crc;
	Entry *ent;

	crc = 0;
	entsize = getle32(hdr->entrysize);
	tablba = getle64(hdr->tablba);
	for(i=0; i<nparts; i++){
		ent = getent(edit->disk, tablba, entsize, i);
		memset(ent, 0, entsize);
		writeent(ent, &parts[i]);
		crc = sumcrc32(crc, (uchar*)ent, entsize);
	}

	hdrsize = getle32(hdr->hdrsiz);
	putle32(hdr->tabcrc, crc);
	putle32(hdr->hdrcrc, 0);
	putle32(hdr->hdrcrc, sumcrc32(0, (uchar*)hdr, hdrsize));
}

static char*
cmdwrite(Edit *edit)
{
	writetab(edit, phdr);
	writetab(edit, bhdr);
	flushdisk(edit->disk);
	cmdprintctl(edit, edit->disk->ctlfd);
	return nil;
}

static char*
newpart(Edit *edit, Gptpart *p, vlong start, vlong end, Type *type, uvlong attr)
{
	if(end <= partoff || start >= partend)
		return "partition overlaps partition table";

	if(start < partoff)
		start = partoff;

	memset(p, 0, sizeof(*p));
	p->type = type;
	p->attr = attr;
	p->start = start;
	p->end = end;
	uugen(p->uuid);
	runesnprint(p->label, nelem(p->label), "%s", p->type->desc);
	partname(edit, p);
	return addpart(edit, p);
}

static void
autopart1(Edit *edit, Type *type, uvlong attr, vlong maxsize)
{
	vlong start, bigstart, bigsize;
	Gptpart *p;
	int i;

	maxsize /= edit->disk->secsize;

	bigsize = 0;
	bigstart = 0;
	start = partoff;
	for(i=0; i<edit->npart; i++){
		p = (Gptpart*)edit->part[i];
		if(p->type == type)
			return;
		if(p->start > start && (p->start - start) > bigsize){
			bigsize = p->start - start;
			bigstart = start;
		}
		start = p->end;
	}
	if(partend > start && (partend - start) > bigsize){
		bigsize = partend - start;
		bigstart = start;
	}
	if(bigsize < 1) {
		fprint(2, "couldn't find space for plan 9 partition\n");
		return;
	}
	if(maxsize && bigsize > maxsize)
		bigsize = maxsize;
	for(i=0; i<nparts; i++){
		p = &parts[i];
		if(p->type == nil){
			newpart(edit, p, bigstart, bigstart+bigsize, type, attr);
			return;
		}
	}
	fprint(2, "couldn't find free slot for %s partition\n", type->name);
}

static void
autopart(Edit *edit)
{
	autopart1(edit, gettype(nil, "esp"), 4, 550*MB);
	autopart1(edit, gettype(nil, "plan9"), 0, 0);
}

typedef struct Name Name;
struct Name {
	char *name;
	Name *link;
};

static Name *namelist;

static void
plan9print(Gptpart *p)
{
	int i, ok;
	char *name, *vname;
	Name *n;
	char *sep;

	vname = p->type->name;
	if(vname==nil || strcmp(vname, "")==0) {
		p->ctlname = "";
		return;
	}

	/* avoid names like plan90 */
	i = strlen(vname) - 1;
	if(vname[i] >= '0' && vname[i] <= '9')
		sep = ".";
	else
		sep = "";

	i = 0;

	name = emalloc(strlen(vname)+10);
	sprint(name, "%s", vname);
	do {
		ok = 1;
		for(n=namelist; n; n=n->link) {
			if(strcmp(name, n->name) == 0) {
				i++;
				sprint(name, "%s%s%d", vname, sep, i);
				ok = 0;
			}
		}
	} while(ok == 0);

	p->ctlname = name;

	n = emalloc(sizeof(*n));
	n->name = name;
	n->link = namelist;
	namelist = n;
}

static void
freenamelist(void)
{
	Name *n, *next;

	for(n=namelist; n; n=next) {
		next = n->link;
		free(n->name);
		free(n);
	}
	namelist = nil;
}

static void
cmdprintctl(Edit *edit, int ctlfd)
{
	int i;

	freenamelist();
	for(i=0; i<edit->npart; i++)
		plan9print((Gptpart*)edit->part[i]);
	ctldiff(edit, ctlfd);
}

static char*
cmdokname(Edit*, char *name)
{
	if(name[0] != 'p' || atoi(name+1) <= 0)
		return "name must be pN";
	return nil;
}

static void
cmdsum(Edit *edit, Part *vp, vlong a, vlong b)
{
	char *name, *type, *unit;
	Rune *label;
	Gptpart *p;
	uvlong attr;
	vlong s, d;

	if((p = (Gptpart*)vp) == nil){
		if(a < partoff)
			a = partoff;
		if(a >= b)
			return;
		name = "empty";
		type = "";
		attr = 0;
		label = L"";
	} else {
		name = p->name;
		type = p->type->name;
		attr = p->attr;
		label = p->label;
	}

	s = (b - a)*edit->disk->secsize;
	if(s >= 1*TB){
		unit = "TB";
		d = TB;
	}else if(s >= 1*GB){
		unit = "GB";
		d = GB;
	}else if(s >= 1*MB){
		unit = "MB";
		d = MB;
	}else if(s >= 1*KB){
		unit = "KB";
		d = KB;
	}else{
		unit = "B ";
		d = 1;
	}

	print("%A %-6s %*llud %*llud (%lld.%.2d %s) %8s \"%S\"\n",
		attr, name, edit->disk->width, a, edit->disk->width, b,
		s/d, (int)(((s%d)*100)/d), unit, type, label);
}

static char*
cmdadd(Edit *edit, char *name, vlong start, vlong end)
{
	int slot;

	slot = atoi(name+1)-1;
	if(slot < 0 || slot >= nparts)
		return "partition number out of range";
	return newpart(edit, &parts[slot], start, end, gettype(nil, "plan9"), 0);
}

static char*
cmddel(Edit *edit, Part *p)
{
	memset((Gptpart*)p, 0, sizeof(Gptpart));
	return delpart(edit, p);
}

static char *help = 
	"t name [type] - set partition type\n"
	"f name [+-flags] - set partition attributes\n"
	"l name [label] - set partition label\n";

static char*
cmdhelp(Edit*)
{
	print("%s\n", help);
	return nil;
}

static char*
cmdflag(Edit *edit, int na, char **a)
{
	Gptpart *p;
	char *s, op;
	Flag *f;

	if(na < 2)
		return "args";

	if((p = (Gptpart*)findpart(edit, a[1])) == nil)
		return "unknown partition";

	if(na == 2){
		for(;;){
			fprint(2, "set attibutes [? for list]: ");
			s = getline(edit);
			if(s[0] != '?')
				break;
			for(f = flags; f->c != '\0'; f++)
				fprint(2, "%#.16llux %c - %s\n", f->f, f->c, f->desc);
		}
	} else {
		s = a[2];
	}

	op = '+';
	for(; *s != '\0'; s++){
		switch(*s){
		case '+':
		case '-':
			op = *s;
		case ' ':
			continue;
		}
		for(f = flags; f->c != '\0'; f++)
			if(f->c == *s)
				break;
		if(f->c == '\0')
			return "unknown flag";
		switch(op){
		case '+':
			p->attr |= f->f;
			break;
		case '-':
			p->attr &= ~f->f;
			break;
		}
		p->changed = 1;
		edit->changed = 1;
	}
	return nil;
}

static Type*
gettype(uchar uuid[16], char *name)
{
	Type *t;

	if(name != nil){
		for(t = types; t->name != nil; t++)
			if(strcmp(name, t->name) == 0)
				return t;
		uugen(uuid);
	} else {
		for(t = types; t->name != nil; t++)
			if(memcmp(t->uuid, uuid, 16) == 0)
				return t;
	}
	if(t >= &types[nelem(types)-1])
		sysfatal("too many partition types");
	memmove(t->uuid, uuid, 16);
	t->name = smprint("type%.2uX%.2uX%.2uX%.2uX", uuid[3], uuid[2], uuid[1], uuid[0]);
	t->desc = name != nil ? estrdup(name) : "";
	return t;
}

static char*
cmdtype(Edit *edit, int nf, char **f)
{
	uchar uuid[16];
	Gptpart *p;
	char *q;
	Type *t;

	if(nf < 2)
		return "args";

	if((p = (Gptpart*)findpart(edit, f[1])) == nil)
		return "unknown partition";

	if(nf == 2) {
		for(;;) {
			fprint(2, "new partition type [? for list]: ");
			q = getline(edit);
			if(q[0] != '?')
				break;
			for(t = types+1; t->name != nil; t++)
				fprint(2, "%U %-15s %s\n", t->uuid, t->name, t->desc);
		}
	} else
		q = f[2];

	if(q[0] == '\0' || (t = gettype(uuid, q)) == p->type)
		return nil;

	p->type = t;
	memset(p->label, 0, sizeof(p->label));
	runesnprint(p->label, nelem(p->label), "%s", t->desc);
	p->changed = 1;
	edit->changed = 1;
	return nil;
}

static char*
cmdlabel(Edit *edit, int nf, char **f)
{
	Gptpart *p;
	char *q;

	if(nf < 2)
		return "args";

	if((p = (Gptpart*)findpart(edit, f[1])) == nil)
		return "unknown partition";

	if(nf == 2) {
		fprint(2, "new label: ");
		q = getline(edit);
	} else
		q = f[2];

	memset(p->label, 0, sizeof(p->label));
	runesnprint(p->label, nelem(p->label), "%s", q);
	p->changed = 1;
	edit->changed = 1;
	return nil;
}

static char*
cmdext(Edit *edit, int nf, char **f)
{
	switch(f[0][0]) {
	case 't':
		return cmdtype(edit, nf, f);
	case 'f':
		return cmdflag(edit, nf, f);
	case 'l':
		return cmdlabel(edit, nf, f);
	default:
		return "unknown command";
	}
}
