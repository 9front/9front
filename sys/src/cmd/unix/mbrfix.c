#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long long vlong;


enum {
	Sectsz = 0x200,
	Psectsz = 11,
	Pclustsc = 13,
	Presvd = 14,
	Pnumfat = 16,
	Pfatsz = 22,
	Pfatsz32 = 36,
	Pvolid = 67,
};

int
readn(int f, void *av, int n)
{
	char *a;
	int m, t;
	a = av;
	t = 0;
	while(t < n){
		m = read(f, a+t, n-t);
		if(m <= 0){
			if(t == 0)
				return m;
			break;
		}
		t += m;
	}
	return t;
}

void
sysfatal(char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
	fprintf(stderr, "\n");
	exit(1);
}

void
readsect(int fd, uint n, void *data)
{
	loff_t off;

	off = (loff_t) n * Sectsz;
	if(llseek(fd, off, SEEK_SET) != off)
		sysfatal("seek to sector 0x%x failed", n);
	if(readn(fd, data, Sectsz) != Sectsz)
		sysfatal("short read: %m");
}

void
writesect(int fd, uint n, void *data)
{
	loff_t off;

	off = (loff_t) n * Sectsz;
	if(llseek(fd, off, SEEK_SET) != off)
		sysfatal("seek to sector 0x%x failed", n);
	if(write(fd, data, Sectsz) != Sectsz)
		sysfatal("short write: %m");
}

uint
getulong(uchar *s)
{
	return s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24);
}

void
putulong(uchar *s, uint n)
{
	*s++ = n & 0xff;
	*s++ = (n >> 8) & 0xff;
	*s++ = (n >> 16) & 0xff;
	*s++ = (n >> 24) & 0xff;
}


uint
getushort(uchar *s)
{
	return s[0] | (s[1] << 8);
}

int
checksig(uchar *s)
{
	return s[0x1fe] == 0x55 && s[0x1ff] == 0xaa;
}

void
fixpbs(uchar *pbs, uchar *pbs9, uint lba)
{
	uint a;
	uint fatsz, resvd, numfat;

	numfat = pbs[Pnumfat];
	fatsz = getushort(&pbs[Pfatsz]);
	if(fatsz == 0)
		fatsz = getulong(&pbs[Pfatsz32]);
	resvd = getushort(&pbs[Presvd]);

	a = pbs9[1] + 2;
	memcpy(pbs, pbs9, 3);
	memcpy(pbs+a, pbs9+a, Sectsz-a-2);
	a = lba + numfat * fatsz + resvd;
	printf("Xroot=%x\n", a);
	putulong(&pbs[Pvolid], a);
}


int
main(int argc, char *argv[])
{
	int dev, fd, i;
	uchar mbr9[Sectsz], pbs9[Sectsz];
	uchar mbr[Sectsz], pbs[Sectsz];
	uint lba;
	int part, want;
	char *mbrfn, *pbsfn, *devfn;

	if(argc < 4)
		sysfatal("usage: <device> <mbrfile> <pbsfile> [part]");
	devfn = argv[1];
	mbrfn = argv[2];
	pbsfn = argv[3];
	want = argc >= 5 ? atoi(argv[4]) : -1;
	part = -1;

	dev = open(devfn, O_RDWR);
	if(dev < 0)
		sysfatal("%s: %m", devfn);

	if((fd = open(mbrfn, O_RDONLY)) < 0)
		sysfatal("%s: %m", mbrfn);
	if(readn(fd, mbr9, Sectsz) < 3)
		sysfatal("%s: too short", mbrfn);
	close(fd);

	fd = open(pbsfn, O_RDONLY);
	if(fd < 0)
		sysfatal("%s: %m", pbsfn);
	if(readn(fd, pbs9, Sectsz) < 3)
		sysfatal("%s: too short", pbsfn);
	if(pbs9[0] != 0xeb)
		sysfatal("first byte of pbs not a short jump");
	close(fd);

	readsect(dev, 0, mbr);
	if(!checksig(mbr))
		sysfatal("sector 0 is missing signature");
	for(i=0; i<4; i++){
		if(mbr[0x1be + i*16] == 0x80 && (part == -1 || i == want))
			part = i;
	}
	if(part == -1)
		sysfatal("no bootable partitions found");
	if(want != -1 && part != want)
		sysfatal("partition %d is not bootable", want);

	lba = getulong(&mbr[0x1be + part*16 + 8]);
	if(lba == 0)
		sysfatal("partition %d has zero LBA", part);

	readsect(dev, lba, pbs);
	if(!checksig(pbs))
		sysfatal("partition %d (LBA=0x%x) is missing signaure", part, lba);
	if(getushort(&pbs[Psectsz]) != 512)
		sysfatal("sector size not 512");

	printf("using partition %d, LBA=0x%x\n", part, lba);
	memcpy(mbr, mbr9, 446);
	fixpbs(pbs, pbs9, lba);
	
	writesect(dev, 0, mbr);
	writesect(dev, lba, pbs);
	
	close(dev);
	return 0;
}
