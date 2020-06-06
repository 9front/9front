#include <u.h>
#include <libc.h>
#include <bio.h>
#include <fcall.h>

#include "pci.h"
#include "vga.h"

static Pcidev* pcilist;
static Pcidev* pcitail;

static void
pcicfginit(void)
{
	int fd, i, j, n, bno, dno, fno;
	char buf[1024], *base, *s;
	Pcidev *p;
	Dir *d;

	trace("pcicfginit\n");
	if((fd = open(base = "/dev/pci", OREAD)) < 0)
		if((fd = open(base = "#$/pci", OREAD)) < 0)
			return;
	n = dirreadall(fd, &d);
	close(fd);
	
	for(i=0; i<n; i++) {
		if(strstr(d[i].name, "ctl") == nil)
			continue;

		strncpy(buf, d[i].name, sizeof(buf));
		bno = strtoul(buf, &s, 10);
		dno = strtoul(s+1, &s, 10);
		fno = strtoul(s+1, nil, 10);
	
		p = mallocz(sizeof(*p), 1);
		p->tbdf = MKBUS(BusPCI, bno, dno, fno);
		sprint(buf, "%s/%d.%d.%draw", base, bno, dno, fno);
		if((p->rawfd = open(buf, ORDWR)) < 0){
			free(p);
			continue;
		}
		sprint(buf, "%s/%d.%d.%dctl", base, bno, dno, fno);
		if((fd = open(buf, OREAD)) < 0){
			close(p->rawfd);
			free(p);
			continue;
		}
		if((j = read(fd, buf, sizeof(buf)-1)) <= 0){
			close(p->rawfd);
			close(fd);
			free(p);
			continue;
		}
		buf[j] = 0;
		close(fd);

		p->ccrb = strtol(buf, nil, 16);
		p->ccru = strtol(buf + 3, nil, 16);
		p->vid = strtol(buf + 9, &s, 16);
		p->did = strtol(s + 1, &s, 16);
		p->intl = strtol(s + 1, &s, 10);

		p->rid = pcicfgr8(p, PciRID);

		trace("%d.%d.%d: did=%X vid=%X rid=%X intl=%d ccru=%X\n",
			bno, dno, fno, p->did, p->vid, p->rid, p->intl, p->ccru);

		while(*s == ' '){
			j = strtol(s+1, &s, 10);
			if(j < 0 || j >= nelem(p->mem))
				break;
			p->mem[j].bar = strtoull(s+1, &s, 16);
			p->mem[j].size = strtoul(s+1, &s, 10);
			trace("\tmem[%d] = %llux %d\n", j, p->mem[j].bar, p->mem[j].size);
		}

		if(pcilist != nil)
			pcitail->list = p;
		else
			pcilist = p;
		pcitail = p;
	}
}

static int
pcicfgrw(Pcidev *pcidev, int rno, int data, int len, int read)
{
	uchar buf[4];

	if(read){
		memset(buf, 0, sizeof(buf));
		if(pread(pcidev->rawfd, buf, len, rno) != len)
			return -1;
		switch(len){
		case 1:
			return GBIT8(buf);
		case 2:
			return GBIT16(buf);
		case 4:
			return GBIT32(buf);
		default:
			abort();
		}
	} else {
		switch(len){
		case 1:
			PBIT8(buf, data);
			break;
		case 2:
			PBIT16(buf, data);
			break;
		case 4:
			PBIT32(buf, data);
			break;
		default:
			abort();
		}
		if(pwrite(pcidev->rawfd, buf, len, rno) != len)
			return -1;
	}
	return 0;
}

int
pcicfgr8(Pcidev* pcidev, int rno)
{
	return pcicfgrw(pcidev, rno, 0, 1, 1);
}

void
pcicfgw8(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw(pcidev, rno, data, 1, 0);
}

int
pcicfgr16(Pcidev* pcidev, int rno)
{
	return pcicfgrw(pcidev, rno, 0, 2, 1);
}

void
pcicfgw16(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw(pcidev, rno, data, 2, 0);
}

int
pcicfgr32(Pcidev* pcidev, int rno)
{
	return pcicfgrw(pcidev, rno, 0, 4, 1);
}

void
pcicfgw32(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw(pcidev, rno, data, 4, 0);
}

Pcidev*
pcimatch(Pcidev* prev, int vid, int did)
{
	if(pcilist == nil)
		pcicfginit();

	if(prev == nil)
		prev = pcilist;
	else
		prev = prev->list;

	while(prev != nil) {
		if(prev->vid == vid && (did == 0 || prev->did == did))
			break;
		prev = prev->list;
	}
	return prev;
}

