#include <u.h>
#include "fns.h"

enum {
	Sectsz = 0x200,
	Dirsz = 0x20,
	Maxpath = 64,
};

typedef struct Extend Extend;
typedef struct Dir Dir;
typedef struct Pbs Pbs;

struct Extend
{
	int drive;
	ulong lba;
	ulong len;
	uchar *rp;
	uchar *ep;
	uchar buf[Sectsz];
};

struct Dir
{
	char name[11];
	uchar attr;
	uchar reserved;
	uchar ctime;
	uchar ctime[2];
	uchar cdate[2];
	uchar adate[2];
	uchar starthi[2];
	uchar mtime[2];
	uchar mdate[2];
	uchar startlo[2];
	uchar len[4];
};

struct Pbs
{
	uchar magic[3];
	uchar version[8];
	uchar sectsize[2];
	uchar clustsize;
	uchar nreserv[2];
	uchar nfats;
	uchar rootsize[2];
	uchar volsize[2];
	uchar mediadesc;
	uchar fatsize[2];
	uchar trksize[2];
	uchar nheads[2];
	uchar nhidden[4];
	uchar bigvolsize[4];
	uchar driveno;
	uchar reserved0;
	uchar bootsig;
	uchar volid[4];
	uchar label[11];
	uchar type[8];
};

int readsect(ulong drive, ulong lba, void *buf);

int
read(void *f, void *data, int len)
{
	Extend *ex = f;

	if(ex->len > 0 && ex->rp >= ex->ep)
		if(readsect(ex->drive, ex->lba++, ex->rp = ex->buf))
			return -1;
	if(ex->len < len)
		len = ex->len;
	if(len > (ex->ep - ex->rp))
		len = ex->ep - ex->rp;
	memmove(data, ex->rp, len);
	ex->rp += len;
	ex->len -= len;
	return len;
}

void
close(void *f)
{
	Extend *ex = f;

	ex->drive = 0;
	ex->lba = 0;
	ex->len = 0;
	ex->rp = ex->ep = ex->buf + Sectsz;
}

static ulong
rootlba(Extend *fat)
{
	ulong lba;
	Pbs *p = (Pbs*)fat->buf;

	lba = fat->lba;
	lba += *((ushort*)p->nreserv);
	lba += *((ushort*)p->fatsize) * p->nfats;
	return lba;
}

static ulong
dirlba(Extend *fat, Dir *d)
{
	ulong clust;
	ulong dirs;
	ulong lba;
	Pbs *p = (Pbs*)fat->buf;

	lba = rootlba(fat);
	dirs = *((ushort*)p->rootsize);
	lba += (dirs * Dirsz + Sectsz-1) / Sectsz;
	clust = *((ushort*)d->starthi)<<16 | *((ushort*)d->startlo);
	lba += (clust - 2) * p->clustsize;
	return lba;
}

static int
dirname(Dir *d, char buf[Maxpath])
{
	char c, *x;

	if(d->attr == 0x0F || *d->name <= 0)
		return -1;
	memmove(buf, d->name, 8);
	x = buf+8;
	while(x > buf && x[-1] == ' ')
		x--;
	if(d->name[8] != ' '){
		*x++ = '.';
		memmove(x, d->name+8, 3);
		x += 3;
	}
	while(x > buf && x[-1] == ' ')
		x--;
	*x = 0;
	x = buf;
	while(c = *x){
		if(c >= 'A' && c <= 'Z'){
			c -= 'A';
			c += 'a';
		}
		*x++ = c;
	}
	return x - buf;
}

static int
fatwalk(Extend *ex, Extend *fat, char *path)
{
	char name[Maxpath], *end;
	Pbs *pbs = (Pbs*)fat->buf;
	int i, j;
	Dir d;

	close(ex);
	ex->drive = fat->drive;
	ex->lba = rootlba(fat);
	ex->len = *((ushort*)pbs->rootsize) * Dirsz;
	for(;;){
		if(readn(ex, &d, Dirsz) != Dirsz)
			break;
		if((i = dirname(&d, name)) <= 0)
			continue;
		while(*path == '/')
			path++;
		if((end = strchr(path, '/')) == 0)
			end = path + strlen(path);
		j = end - path;
		if(i == j && memcmp(name, path, j) == 0){
			ex->rp = ex->ep;
			ex->lba = dirlba(fat, &d);
			ex->len = *((ulong*)d.len);
			if(*end == 0)
				return 0;
			else if(d.attr & 0x10){
				ex->len = pbs->clustsize * Sectsz;
				path = end;
				continue;
			}
			break;
		}
	}
	close(ex);
	return -1;
}

static int
findfat(Extend *fat, int drive)
{
	struct {
		uchar status;
		uchar bchs[3];
		uchar typ;
		uchar echs[3];
		uchar lba[4];
		uchar len[4];
	} *p;
	int i;

	if(readsect(drive, 0, fat->buf))
		return -1;
	if(fat->buf[0x1fe] != 0x55 || fat->buf[0x1ff] != 0xAA)
		return -1;
	p = (void*)&fat->buf[0x1be];
	for(i=0; i<4; i++){
		if(p[i].status != 0x80)
			continue;
		close(fat);
		fat->drive = drive;
		fat->lba = *((ulong*)p[i].lba);
		if(readsect(drive, fat->lba, fat->buf))
			continue;
		return 0;
	}
	return -1;
}

void
start(void *sp)
{
	char path[Maxpath], *kern;
	int drive;
	Extend fat, ex;
	void *f;

	/* drive passed in DL */
	drive = ((ushort*)sp)[5] & 0xFF;

	if(findfat(&fat, drive)){
		print("no fat\r\n");
		halt();
	}
	if(fatwalk(f = &ex, &fat, "plan9.ini")){
		print("no config\r\n");
		f = 0;
	}
	for(;;){
		kern = configure(f, path); f = 0;
		if(fatwalk(&ex, &fat, kern)){
			print("not found\r\n");
			continue;
		}
		print(bootkern(&ex));
		print(crnl);
	}
}

