#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static uchar hdr[8192];
static int fd;

extern int bootmodn;
extern char **bootmod;

static int
putmmap(uchar *p0)
{
	u32int *p;
	Region *r;
	
	p = (u32int *) p0;
	for(r = mmap; r != nil; r = r->next){
		if(r->type != REGMEM) continue;
		if(gavail(p) < 20) sysfatal("out of guest memory");
		p[0] = 20;
		p[1] = r->start;
		p[2] = r->end - r->start;
		p[3] = 1;
	}
	return (uchar *) p - p0;
}

static int
putcmdline(uchar *p0)
{
	int i;
	char *p, *e;
	extern int cmdlinen;
	extern char **cmdlinev;
	
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
	
	USED(modp);
	rset(RPC, entry);
	rset(RAX, 0x2badb002);
	rset(RBX, bssend);
	return 1;
}

void
loadkernel(char *fn)
{
	fd = open(fn, OREAD);
	if(fd < 0) sysfatal("open: %r");
	if(readn(fd, hdr, sizeof(hdr)) <= 0)
		sysfatal("readn: %r");
	if(!trymultiboot())
		sysfatal("%s: unknown format", fn);
	close(fd);
}
