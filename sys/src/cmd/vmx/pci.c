#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

PCIDev *pcidevs;
PCIBar membars, iobars;

PCIDev *
mkpcidev(u32int bdf, u32int viddid, u32int clrev, int needirq)
{
	PCIDev *d;
	int n;
	
	d = emalloc(sizeof(PCIDev));
	d->bdf = bdf;
	d->viddid = viddid;
	d->clrev = clrev;
	d->next = pcidevs;
	d->irqno = needirq ? 0 : 0xff;
	for(n = 0; n < nelem(d->bar); n++){
		d->bar[n].d = d;
		d->bar[n].busnext = &d->bar[n];
		d->bar[n].busprev = &d->bar[n];
	}
	d->capalloc = 64;
	pcidevs = d;
	return d;
}

u32int
allocbdf(void)
{
	static int dev = 1;
	
	return BDF(0, dev++, 0);
}

u32int
roundpow2(u32int l)
{
	l = -l;
	l &= (int)l >> 16;
	l &= (int)l >> 8;
	l &= (int)l >> 4;
	l &= (int)l >> 2;
	l &= (int)l >> 1;
	return -l;
}

PCIBar *
mkpcibar(PCIDev *d, u8int t, u32int a, u32int l, void *fn, void *aux)
{
	PCIBar *b;

	assert((t & 1) == 0 || (t & 2) == 0);
	assert((t & 1) != 0 || (t & 6) == 0);
	if((t & 1) != 0 && l < 4) l = 4;
	if((t & 1) == 0 && l < 4096) l = 4096;
	if((l & l-1) != 0)
		l = roundpow2(l);
	for(b = d->bar; b < d->bar + nelem(d->bar); b++)
		if(b->length == 0)
			break;
	if(b == d->bar + nelem(d->bar))
		sysfatal("pci bdf %6ux: too many bars", d->bdf);
	b->addr = a;
	b->type = t;
	b->length = l;
	b->busnext = b;
	b->busprev = b;
	b->d = d;
	if((b->type & 1) != 0)
		b->io = fn;
	b->aux = aux;
	return b;
}

static void
updatebar(PCIBar *b)
{
	b->busnext->busprev = b->busprev;
	b->busprev->busnext = b->busnext;
	b->busnext = b;
	b->busprev = b;
	if(b->length == 0) return;
	if((b->type & 1) == 0){
		if((b->d->ctrl & 2) == 0) return;
		b->busnext = &membars;
		b->busprev = membars.busprev;
		b->busnext->busprev = b;
		b->busprev->busnext = b;
	}else{
		if((b->d->ctrl & 1) == 0 || b->addr == 0 || b->io == nil) return;
		b->busnext = &iobars;
		b->busprev = iobars.busprev;
		b->busnext->busprev = b;
		b->busprev->busnext = b;
	}
}

static void
pciirqupdate(void)
{
	PCIDev *d;
	int irqs, act, i;
	
	irqs = 0;
	act = 0;
	for(d = pcidevs; d != nil; d = d->next){
		if(d->irqno < 16){
			irqs |= 1<<d->irqno;
			act |= d->irqactive<<d->irqno;
		}
	}
	for(i = 0; i < 16; i++)
		if((irqs & 1<<i) != 0)
			irqline(i, ~act>>i & 1);
}

PCICap *
mkpcicap(PCIDev *d, u8int length, u32int (*readf)(PCICap *, u8int), void (*writef)(PCICap *, u8int, u32int, u32int))
{
	PCICap *c, **p;

	assert(readf != nil);
	if(d->capalloc + length > 256)
		sysfatal("mkpcicap (dev %#ux): out of configuration space", d->bdf);
	c = emalloc(sizeof(PCICap));
	c->dev = d;
	c->read = readf;
	c->write = writef;
	c->length = length;
	
	c->addr = d->capalloc;
	d->capalloc += length;
	for(p = &d->cap; *p != nil; p = &(*p)->next)
		;
	*p = c;
	return c;
}

static PCIDev *
findpcidev(u32int bdf)
{
	PCIDev *d;

	for(d = pcidevs; d != nil; d = d->next)
		if(d->bdf == bdf)
			return d;
	return nil;
}

static PCICap *
findpcicap(PCIDev *d, u8int addr)
{
	PCICap *c;
	
	for(c = d->cap; c != nil; c = c->next)
		if((uint)(addr - c->addr) < c->length)
			return c;
	return nil;
}

static u32int
pciread(PCIDev *d, int addr)
{
	u32int val;
	PCICap *c;
	int n;

	switch(addr){
	case 0x00: return d->viddid;
	case 0x04: return 0xa00000 | (d->cap != nil ? 1<<20 : 0) | d->ctrl;
	case 0x08: return d->clrev;
	case 0x0c: return 0; /* BIST, Header Type, Latency Timer, Cache Size */
	case 0x10: case 0x14: case 0x18: case 0x1c: case 0x20: case 0x24:
		n = addr - 0x10 >> 2;
		return d->bar[n].addr | d->bar[n].type;
	case 0x28: return 0; /* Cardbus */
	case 0x2c: return d->subid; /* Subsystem ID */
	case 0x30: return 0; /* Expansion ROM */
	case 0x34: return d->cap != nil ? d->cap->addr : 0; /* Capabilities */
	case 0x38: return 0; /* Reserved */
	case 0x3c: return 1 << 8 | d->irqno; /* Max_Lat, Min_Gnt, IRQ Pin, IRQ Line */
	}
	c = findpcicap(d, addr);
	if(c != nil){
		val = c->read(c, addr - c->addr);
		if(addr == c->addr){
			val &= ~0xff00;
			if(c->next != nil)
				val |= c->next->addr << 8;
		}
		return val;
	}
	vmdebug("pcidev %.6ux: ignoring read from addr %#ux", d->bdf, addr);
	return 0;
}

static void
pciwrite(PCIDev *d, int addr, u32int val, u32int mask)
{
	int n;
	PCICap *c;
	
	switch(addr){
	case 0x04:
		d->ctrl = (d->ctrl & ~mask | val & mask) & 0x21f;
		for(n = 0; n < nelem(d->bar); n++)
			updatebar(&d->bar[n]);
		return;
	case 0x10: case 0x14: case 0x18: case 0x1c: case 0x20: case 0x24:
		n = addr - 0x10 >> 2;
		val &= (d->bar[n].type & 1) != 0 ? ~15 : ~3;
		d->bar[n].addr = (d->bar[n].addr & ~mask | val & mask) & ~(d->bar[n].length - 1);
		updatebar(&d->bar[n]);
		return;
	case 0x30: return;
	case 0x3c: d->irqno = (d->irqno & ~mask | val & mask) & 0xff; pciirqupdate(); return;
	}
	c = findpcicap(d, addr);
	if(c != nil && c->write != nil){
		c->write(c, addr - c->addr, val, mask);
		return;
	}
	vmdebug("pcidev %.6ux: ignoring write to addr %#ux, val %#ux", d->bdf, addr, val);
}

u32int
pciio(int isin, u16int port, u32int val, int sz, void *)
{
	static u32int cfgaddr;
	u32int mask;
	PCIDev *d;

	switch(isin << 16 | port){
	case 0x0cf8: cfgaddr = val; return 0;
	case 0x10cf8: return cfgaddr;
	case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
		val <<= 8 * (port & 3);
		mask = -1UL >> 32 - 8 * sz << 8 * (port & 3);
		if((cfgaddr & 1<<31) != 0 && (d = findpcidev(cfgaddr & 0xffff00), d != nil))
			pciwrite(d, cfgaddr & 0xfc, val, mask);
		return 0;
	case 0x10cfc: case 0x10cfd: case 0x10cfe: case 0x10cff:
		if((cfgaddr & 1<<31) == 0 || (d = findpcidev(cfgaddr & 0xffff00), d == nil))
			return -1;
		return pciread(d, cfgaddr & 0xfc) >> 8 * (port & 3);
	}
	return iowhine(isin, port, val, sz, "pci");
}

void
pciirq(PCIDev *d, int status)
{
	d->irqactive = status != 0;
	pciirqupdate();
}

void
pciinit(void)
{
	iobars.busnext = &iobars;
	iobars.busprev = &iobars;
	membars.busprev = &membars;
	membars.busnext = &membars;
	mkpcidev(BDF(0,0,0), 0x01008086, 0x06000000, 0);
}

void
pcibusmap(void)
{
	u16int iop;
	u16int irqs, uirqs;
	PCIDev *d;
	PCIBar *b;
	int irq;
	int i;
	
	iop = 0x1000;
	irqs = 1<<5|1<<7|1<<9|1<<10|1<<11;
	uirqs = 0;
	irq = 0;
	for(d = pcidevs; d != nil; d = d->next){
		d->ctrl |= 3;
		for(b = d->bar; b < d->bar + nelem(d->bar); b++){
			if(b->length == 0 || b->addr != 0)
				continue;
			if((b->type & 1) == 0){
				vmerror("pci device %.6ux: memory bars unsupported", d->bdf);
				continue;
			}
			if(iop + b->length >= 0x10000){
				vmerror("pci device %.6ux: not enough I/O address space for BAR%d (len=%d)", d->bdf, (int)(b - d->bar), b->length);
				continue;
			}
			b->addr = iop;
			iop += b->length;
			updatebar(b);
		}
		if(d->irqno == 0){
			do
				irq = irq + 1 & 15;
			while((irqs & 1<<irq) == 0);
			d->irqno = irq;
			uirqs |= 1<<irq;
		}
	}
	elcr(uirqs);
	for(i = 0; i < 16; i++)
		if((uirqs & 1<<i) != 0)
			irqline(i, 1);
}
