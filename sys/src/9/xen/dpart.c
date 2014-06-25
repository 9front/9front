#include <u.h>
#include <libc.h>
#include <disk.h>

typedef void Fs;
#include "/sys/src/boot/pc/dosfs.h"

enum {
	Npart = 32
};

#define	GSHORT(p)		(((p)[1]<<8)|(p)[0])
#define	GLONG(p)			((GSHORT(p+2)<<16)|GSHORT(p))

int
readdisk(Disk *d, void *buf, vlong off, int len)
{
	if(seek(d->fd, off, 0) == -1
	|| read(d->fd, buf, len) != len)
		return -1;
	return 0;
}

void
addpart(Disk *d, char *name, ulong s, ulong e)
{
	print("%s: part %s %lud %lud\n", d->prefix, name, s, e);
	fprint(d->ctlfd, "part %s %lud %lud\n", name, s, e);
}

int
isdos(int t)
{
	return t==FAT12 || t==FAT16 || t==FATHUGE || t==FAT32 || t==FAT32X;
}

int
isextend(int t)
{
	return t==EXTEND || t==EXTHUGE || t==LEXTEND;
}



/* build a cdboot partition if there is an embedded boot floppy image */
int
cdpart(Disk *d)
{
	uchar buf[2048];
	ulong a, n;
	uchar *p;

	if(readdisk(d, buf, 17*2048, 2048) == -1
	|| strcmp((char*)buf+1, "CD001\x01EL TORITO SPECIFICATION") != 0)
		return 0;

	p = buf + 0x47;
	a = p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
	
	if(readdisk(d, buf, a*2048, 2048) == -1
	|| memcmp((char*)buf, "\x01\x00\x00\x00", 4) != 0
	|| memcmp((char*)buf+30, "\x55\xAA", 2) != 0
	|| buf[0x20] != 0x88)
		return 0;

	p = buf+0x28;
	a = p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24);

	switch(buf[0x21]) {
	case 1: n = 1200*1024; break;
	case 2: n = 1440*1024; break;
	case 3: n = 2880*1024; break;
	default: return 0;
	}

	a = a * (uvlong)2048 / d->secsize;
	n /= d->secsize;
	addpart(d, "cdboot", a, a+n);
	return 1;
}

int
p9part(Disk *d, char *name, ulong pstart)
{
	char partbuf[512];
	char *field[4], *line[Npart+1], *name2;
	ulong start, end;
	int i, n;

	name2 = smprint("%s%s", d->prefix, name);
	d = opendisk(name2, 1, 0);
	if(!d) {
		fprint(2, "%s: %r\n", name2);
		free(name2);
		return 0;
	}
	free(name2);

	if(readdisk(d, partbuf, 512, sizeof partbuf) == -1)
		return 0;
	partbuf[sizeof partbuf - 1] = '\0';
	if(strncmp(partbuf, "part ", 5) != 0
	|| (n = getfields(partbuf, line, Npart+1, 0, "\n")) == 0)
		return 0;
	for(i = 0; i < n; i++) {
		if(strncmp(line[i], "part ", 5) != 0)
			break;
		if(getfields(line[i], field, 4, 0, " ") != 4)
			break;
		start = strtoul(field[2], 0, 0);
		end = strtoul(field[3], 0, 0);
		if(start >= end)
			break;
		addpart(d, field[1], pstart+start, pstart+end);
	}
	return 0;
}

int
mbrpart(Disk *d)
{
	uchar mbrbuf[512];
	char name[10];
	Dospart *dp;
	ulong taboffset, start, end;
	ulong firstxpart, nxtxpart;
	int i, nplan9, havedos;

#define readmbr()	\
	if(readdisk(d, mbrbuf, (uvlong)taboffset*512, sizeof mbrbuf) == -1	\
	|| mbrbuf[0x1FE] != 0x55 || mbrbuf[0x1FF] != 0xAA)	\
		return 0

	if(d->secsize > 512)
		return 0;
	dp = (Dospart*)&mbrbuf[0x1BE];
	taboffset = 0;

	if(1) {
		/* get the MBR (allowing for DMDDO) */
		readmbr();
		for(i = 0; i < 4; i++) {
			if(dp[i].type == DMDDO) {
				taboffset = 63;
				readmbr();
				i = -1;		/* start over */
			}
		}
	}

	/*
	 * Read the partitions, first from the MBR and then
	 * from successive extended partition tables.
	 */
	nplan9 = 0;
	havedos = 0;
	firstxpart = 0;
	for(;;) {
		readmbr();
		nxtxpart = 0;
		for(i = 0; i < 4; i++) {
			/* partition offsets are relative to taboffset */
			start = taboffset+GLONG(dp[i].start);
			end = start+GLONG(dp[i].len);
			if(dp[i].type == PLAN9) {
				if(nplan9 == 0)
					strcpy(name, "plan9");
				else
					sprint(name, "plan9.%d", nplan9);
				addpart(d, name, start, end);
				p9part(d, name, start);
				nplan9++;
			}

			if(!havedos && isdos(dp[i].type)) {
				havedos = 1;
				addpart(d, "dos", start, end);
			}

			/* nxtxpart is relative to firstxpart (or 0), not taboffset */
			if(isextend(dp[i].type))
				nxtxpart = start-taboffset+firstxpart;
		}
		if(!nxtxpart)
			break;
		if(!firstxpart)
			firstxpart = nxtxpart;
		taboffset = nxtxpart;
	}
	return nplan9 + havedos;
}

void
partall(void)
{
	Disk *d;
	Dir *ent;
	char *name;
	int fd, i, n;

	fd = open("#S", OREAD);
	if(fd == -1) {
		fprint(2, "No disk\n");
		return;
	}

	while((n = dirread(fd, &ent)) > 0) {
		for(i = 0; i < n; i++) {
			if(ent[i].mode & DMDIR) {
				name = smprint("#S/%s/data", ent[i].name);
				d = opendisk(name, 1, 0);
				if(!d) {
					fprint(2, "%s: %r\n", name);
					continue;
				}
				// XXX not safe yet: if(!mbrpart(d) && !cdpart(d) && !p9part(d, "data", 0))
				if(!mbrpart(d) && !cdpart(d))
					fprint(2, "%s: no partitions\n", name);
				close(d->fd);
			}
		}
	}
	close(fd);				
}


void
main(int argc, char **argv)
{
	USED(argc, argv);

	fmtinstall('r', errfmt);

	bind("#c", "/dev", MBEFORE);
	open("/dev/cons", OREAD);
	open("/dev/cons", OWRITE);
	open("/dev/cons", OWRITE);

	partall();

	close(0);
	close(1);
	close(2);

	exec("/boot/boot2", argv);
	
	exits(0);
}
