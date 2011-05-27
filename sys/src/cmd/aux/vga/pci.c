#include <u.h>
#include <libc.h>
#include <bio.h>

#include "pci.h"
#include "vga.h"

/*
 * PCI support code.
 * There really should be a driver for this, it's not terribly safe
 * without locks or restrictions on what can be poked (e.g. Axil NX801).
 */
enum {					/* configuration mechanism #1 */
	PciADDR		= 0xCF8,	/* CONFIG_ADDRESS */
	PciDATA		= 0xCFC,	/* CONFIG_DATA */

					/* configuration mechanism #2 */
	PciCSE		= 0xCF8,	/* configuration space enable */
	PciFORWARD	= 0xCFA,	/* which bus */

	MaxFNO		= 7,
	MaxUBN		= 255,
};

static int pcicfgmode = -1;
static int pcimaxdno;
static Pcidev* pciroot;
static Pcidev* pcilist;
static Pcidev* pcitail;


static void
pcicfginit(void)
{
	Dir *d;
	int fd, i, j, n, bno, dno, fno;
	char buf[1024], *s;
	Pcidev *p;

	pcicfgmode = 0x666;
	trace("pcicfginit\n");
	fd = open("#$/pci", OREAD);
	if(fd < 0)
		return;
	if((n = dirreadall(fd, &d)) < 0)
		return;
	close(fd);
	
	for(i=0; i<n; i++) {
		int nl = strlen(d[i].name);
		if(d[i].name[nl-3] == 'r' && d[i].name[nl-2] == 'a'  && d[i].name[nl-1] == 'w' ) {
			trace("pci device %s\n",d[i].name);
			sprint(buf, "#$/pci/%s", d[i].name);
			if((fd = open(buf, OREAD)) < 0)
				return;
			if((read(fd, buf, 0x30)) <= 0)
				return;
			close(fd);
			
			bno = strtoul(d[i].name, &s, 10);
			dno = strtoul(s+1, &s, 10);
			fno = strtoul(s+1, nil, 10);
			trace("\t-> %d %d %d\n",bno, dno, fno);
			p = mallocz(sizeof(*p), 1);
			p->tbdf = MKBUS(BusPCI, bno, dno, fno);
			p->vid = *(ulong*)(buf);
			p->did = *(ushort*)(buf+2);
			p->rid  = *(uchar*)(buf+PciRID);
			p->intl = *(uchar*)(buf+PciINTL);
			p->ccru = *(ushort*)(buf+PciCCRu);
			
			int rno = PciBAR0 - 4;
			for(j = 0; j < nelem(p->mem); j++){
				rno += 4;
				p->mem[j].bar = pcicfgr32(p, rno);
				pcicfgw32(p, rno, -1);
				ulong v = pcicfgr32(p, rno);
				pcicfgw32(p, rno, p->mem[i].bar);
				p->mem[j].size = -(v & ~0xF);
				trace("\t->mem[%d] = %p %d\n", j, p->mem[j].bar, p->mem[j].size);
			}
				
	
			if(pcilist != nil)
				pcitail->list = p;
			else
				pcilist = p;
			pcitail = p;


			trace("\t-> did=%X vid=%X rid=%X intl=%d ccru=%X\n",p->did, p->vid, p->rid,p->intl, p->ccru);
			
		}
	}
}

static int
pcicfgrw8(int tbdf, int rno, int data, int read)
{
	int o, type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	switch(pcicfgmode){

	case 1:
		o = rno & 0x03;
		rno &= ~0x03;
		outportl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = inportb(PciDATA+o);
		else
			outportb(PciDATA+o, data);
		outportl(PciADDR, 0);
		break;

	case 2:
		outportb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outportb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = inportb((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outportb((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outportb(PciCSE, 0);
		break;
	}

	return x;
}

int
pcicfgr8(Pcidev* pcidev, int rno)
{
	return pcicfgrw8(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw8(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw8(pcidev->tbdf, rno, data, 0);
}

static int
pcicfgrw16(int tbdf, int rno, int data, int read)
{
	int o, type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	switch(pcicfgmode){

	case 1:
		o = rno & 0x02;
		rno &= ~0x03;
		outportl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = inportw(PciDATA+o);
		else
			outportw(PciDATA+o, data);
		outportl(PciADDR, 0);
		break;

	case 2:
		outportb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outportb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = inportw((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outportw((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outportb(PciCSE, 0);
		break;
	}

	return x;
}

int
pcicfgr16(Pcidev* pcidev, int rno)
{
	return pcicfgrw16(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw16(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw16(pcidev->tbdf, rno, data, 0);
}

static int
pcicfgrw32(int tbdf, int rno, int data, int read)
{
	int type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	switch(pcicfgmode){

	case 1:
		rno &= ~0x03;
		outportl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = inportl(PciDATA);
		else
			outportl(PciDATA, data);
		outportl(PciADDR, 0);
		break;

	case 2:
		outportb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outportb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = inportl((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outportl((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outportb(PciCSE, 0);
		break;
	}

	return x;
}

int
pcicfgr32(Pcidev* pcidev, int rno)
{
	return pcicfgrw32(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw32(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw32(pcidev->tbdf, rno, data, 0);
}

Pcidev*
pcimatch(Pcidev* prev, int vid, int did)
{
	if(pcicfgmode == -1)
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

void
pcihinv(Pcidev* p)
{
	int i;
	Pcidev *t;

	if(pcicfgmode == -1)
		pcicfginit();


	if(p == nil) {
		p = pciroot;
		Bprint(&stdout, "bus dev type vid  did intl memory\n");
	}
	for(t = p; t != nil; t = t->link) {
		Bprint(&stdout, "%d  %2d/%d %.4ux %.4ux %.4ux %2d  ",
			BUSBNO(t->tbdf), BUSDNO(t->tbdf), BUSFNO(t->tbdf),
			t->ccru, t->vid, t->did, t->intl);

		for(i = 0; i < nelem(p->mem); i++) {
			if(t->mem[i].size == 0)
				continue;
			Bprint(&stdout, "%d:%.8lux %d ", i,
				t->mem[i].bar, t->mem[i].size);
		}
		Bprint(&stdout, "\n");
	}
	while(p != nil) {
		if(p->bridge != nil)
			pcihinv(p->bridge);
		p = p->link;
	}
}
