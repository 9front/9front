#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

#include <ip.h>		/* parseether() */
#include <libsec.h>	/* genrandom() */

typedef struct VIODev VIODev;
typedef struct VIOQueue VIOQueue;
typedef struct VIOBuf VIOBuf;
typedef struct VIONetDev VIONetDev;
typedef struct VIOBlkDev VIOBlkDev;

enum {
	BUFCHAIN = 1,
	BUFWR = 2,
	
	USEDNOIRQ = 1,
	
	DRIVEROK = 4, /* devstat */
};

struct VIOBuf {
	u32int flags;
	VIOQueue *qu;
	void *p;
	u64int addr;
	u32int len;
	u32int idx;
	VIOBuf *next, *head;
	u32int rptr, wptr;
};

struct VIOQueue {
	QLock;
	Rendez;
	VIODev *d;
	u8int (*desc)[16], *avail, *used;
	u16int size;
	u32int addr;
	u16int availidx, usedidx;
	void (*notify)(VIOQueue*);
	int livebuf;
	Rendez livebufrend;
};

struct VIONetDev {
	int readfd, writefd;
	u8int mac[6];
	enum {
		VNETPROMISC = 1,
		VNETALLMULTI = 2,
		VNETALLUNI = 4,
		VNETNOMULTI = 8,
		VNETNOUNI = 16,
		VNETNOBCAST = 32,
		
		VNETHEADER = 1<<31,
	} flags;
	u64int macbloom, multibloom;
};

struct VIOBlkDev {
	int fd;
	uvlong size;
};

struct VIODev {
	PCIDev *pci;
	u32int devfeat, guestfeat;
	u16int qsel;
	u8int devstat, isrstat;
	VIOQueue *qu;
	int nqu, allocqu;
	u32int (*io)(int, u16int, u32int, int, VIODev *);
	void (*reset)(VIODev *);
	union {
		VIONetDev net;
		VIOBlkDev blk;
	};
};

static void
vioirq_(void *arg)
{
	VIODev *d;
	int val;
	
	d = ((void**)arg)[0];
	val = (uintptr)((void**)arg)[1];
	if(val != 0)
		d->isrstat |= val;
	else
		d->isrstat = 0;
	pciirq(d->pci, d->isrstat);
	free(arg);
}

static void
vioirq(VIODev *d, int val)
{
	void **v;
	
	assert(d != nil);
	v = emalloc(sizeof(void*)*2);
	v[0] = d;
	v[1] = (void *) val;
	sendnotif(vioirq_, v);
}

static void *
checkdesc(VIOQueue *q, int i)
{
	if(i >= q->size){
		vmerror("virtio device %#x: invalid next pointer %d in queue (size %d), ignoring descriptor", q->d->pci->bdf, i, q->size);
		return nil;
	}
	return q->desc[i];
}

VIOBuf *
viogetbuf(VIOQueue *q, int wait)
{
	u16int gidx;
	VIOBuf *b, *rb, **bp;
	void *dp;
	
	qlock(q);
waitloop:
	while((q->d->devstat & DRIVEROK) == 0 || q->desc == nil || (gidx = GET16(q->avail, 2), gidx == q->availidx)){
		if(!wait){
			qunlock(q);
			return nil;
		}
		rsleep(q);
	}
	dp = checkdesc(q, GET16(q->avail, 4 + 2 * (q->availidx % q->size)));
	rb = nil;
	bp = &rb;
	for(;;){
		b = emalloc(sizeof(VIOBuf));
		b->qu = q;
		b->idx = (u8int(*)[16])dp - q->desc;
		b->addr = GET64(dp, 0);
		b->len = GET32(dp, 8);
		b->flags = GET16(dp, 12);
		b->p = gptr(b->addr, b->len);
		if(b->p == nil){
			vmerror("virtio device %#x: invalid buffer pointer %#p in queue, ignoring descriptor", q->d->pci->bdf, (void*)b->addr);
			free(b);
			break;
		}
		*bp = b;
		b->head = rb;
		bp = &b->next;
		if((b->flags & BUFCHAIN) == 0) break;
		dp = checkdesc(q, GET16(dp, 14));
		if(dp == nil) break;
	}
	q->availidx++;
	if(rb == nil) goto waitloop;
	q->livebuf++;
	qunlock(q);
	return rb;
}

void
vioputbuf(VIOBuf *b)
{
	VIOBuf *bn;
	VIOQueue *q;
	u8int *p;
	
	if(b == nil) return;
	q = b->qu;
	qlock(q);
	if((q->d->devstat & DRIVEROK) == 0){
		qunlock(q);
		goto end;
	}
	if(q->used == nil)
		vmerror("virtio device %#x: address was set to an invalid value while holding buffer", q->d->pci->bdf);
	else{
		p = q->used + 4 + 8 * (q->usedidx % q->size);
		PUT32(p, 4, b->wptr);
		PUT32(p, 0, b->idx);
		PUT16(q->used, 2, ++q->usedidx);
	}
	if(--q->livebuf <= 0)
		rwakeup(&q->livebufrend);
	qunlock(q);
	if(q->avail != nil && (GET16(q->avail, 0) & USEDNOIRQ) == 0)
		vioirq(q->d, 1);
end:
	while(b != nil){
		bn = b->next;
		free(b);
		b = bn;
	}
}

ulong
vioqread(VIOBuf *b, void *v, ulong n)
{
	VIOBuf *c;
	u32int p;
	int rc;
	ulong m;
	
	p = b->rptr;
	c = b;
	rc = 0;
	for(;;){
		if(rc >= n) return rc;
		for(;;){
			if(c == nil) return rc;
			if((c->flags & BUFWR) == 0){
				if(p < c->len) break;
				p -= c->len;
			}
			c = c->next;
		}
		m = c->len - p;
		if(m > n - rc) m = n - rc;
		memmove(v, (u8int*)c->p + p, m);
		p += m, rc += m;
		v = (u8int*)v + m;
		b->rptr += m;
	}
}

ulong
vioqwrite(VIOBuf *b, void *v, ulong n)
{
	VIOBuf *c;
	u32int p;
	int rc;
	ulong m;
	
	p = b->wptr;
	c = b;
	rc = 0;
	for(;;){
		if(rc >= n) return rc;
		for(;;){
			if(c == nil) return rc;
			if((c->flags & BUFWR) != 0){
				if(p < c->len) break;
				p -= c->len;
			}
			c = c->next;
		}
		m = c->len - p;
		if(m > n - rc) m = n - rc;
		memmove((u8int*)c->p + p, v, m);
		p += m, rc += m;
		v = (u8int*)v + m;
		b->wptr += m;
	}
}

ulong
vioqrem(VIOBuf *b, int wr)
{
	VIOBuf *c;
	u32int p;
	ulong rc;
	
	p = wr ? b->wptr : b->rptr;
	for(c = b;; c = c->next){
		if(c == nil) return 0;
		if(((c->flags & BUFWR) != 0) == wr){
			if(p < c->len) break;
			p -= c->len;
		}
	}
	rc = c->len - p;
	for(c = c->next; c != nil; c = c->next)
		if(((c->flags & BUFWR) != 0) == wr)
			rc += c->len;
	return rc;
}

static void
vioqaddrset(VIOQueue *q, u64int addr)
{
	void *p;
	int sz1, sz;

	addr <<= 12;
	sz1 = -(-(18 * q->size + 4) & -4096);
	sz = sz1 + (-(-(8 * q->size + 6) & -4096));
	p = gptr(addr, sz);
	if(p == nil)
		vmerror("virtio device %#x: attempt to set queue to invalid address %#p", q->d->pci->bdf, (void *) addr);
	qlock(q);
	q->addr = addr;
	if(p == nil){
		q->desc = nil;
		q->avail = nil;
		q->used = nil;
	}else{
		q->desc = p;
		q->avail = (u8int*)p + 16 * q->size;
		q->used = (u8int*)p + sz1;
		rwakeupall(q);
	}
	qunlock(q);
}

static void
viodevstatset(VIODev *v, u32int val)
{
	int i;

	v->devstat = val;
	if(val == 0){
		if(v->reset != nil)
			v->reset(v);
		v->guestfeat = 0;
		vioirq(v, 0);
		for(i = 0; i < v->nqu; i++){
			qlock(&v->qu[i]);
			while(v->qu[i].livebuf > 0)
				rsleep(&v->qu[i].livebufrend);
			qunlock(&v->qu[i]);
		}
	}else{
		for(i = 0; i < v->nqu; i++)
			v->qu[i].notify(&v->qu[i]);
	}
}

u32int
vioio(int isin, u16int port, u32int val, int sz, void *vp)
{
	VIODev *v;
	int rc;
	static char whinebuf[32];
	
	v = vp;
	switch(isin << 16 | port){
	case 0x4: v->guestfeat = val; return 0;
	case 0x8: if(v->qsel < v->nqu) vioqaddrset(&v->qu[v->qsel], val); return 0;
	case 0xe: v->qsel = val; return 0;
	case 0x10: if(val < v->nqu) v->qu[val].notify(&v->qu[val]); return 0;
	case 0x12: viodevstatset(v, val); return 0;
	case 0x10000: return v->devfeat;
	case 0x10004: return v->guestfeat;
	case 0x10008: return v->qsel >= v->nqu ? 0 : v->qu[v->qsel].addr;
	case 0x1000c: return v->qsel >= v->nqu ? 0 : v->qu[v->qsel].size;
	case 0x1000e: return v->qsel;
	case 0x10010: return 0;
	case 0x10012: return v->devstat;
	case 0x10013: rc = v->isrstat; vioirq(v, 0); return rc;
	}
	if(port >= 20 && v->io != nil)
		return v->io(isin, port - 20, val, sz, v);
	snprint(whinebuf, sizeof(whinebuf), "virtio device %6x", v->pci->bdf);
	return iowhine(isin, port, val, sz, whinebuf);
}

VIODev *
mkviodev(u16int devid, u32int pciclass, u32int subid, int queues)
{
	VIODev *d;
	
	d = emalloc(sizeof(VIODev));
	d->pci = mkpcidev(allocbdf(), devid << 16 | 0x1AF4, pciclass << 8, 1);
	d->pci->subid = subid << 16;
	mkpcibar(d->pci, BARIO, 0, 256, vioio, d);
	d->qu = emalloc(queues * sizeof(VIOQueue));
	d->allocqu = queues;
	return d;
}

static void
viowakeup(VIOQueue *q)
{
	qlock(q);
	rwakeupall(q);
	qunlock(q);
}

VIOQueue *
mkvioqueue(VIODev *d, int sz, void (*fn)(VIOQueue*))
{
	VIOQueue *q;

	assert(sz > 0 && sz <= 32768 && (sz & sz - 1) == 0 && fn != nil && d->nqu < d->allocqu);
	q = d->qu + d->nqu++;
	q->Rendez.l = q;
	q->livebufrend.l = q;
	q->size = sz;
	q->d = d;
	q->notify = fn;
	return q;
}

int
bloomhash(u8int *mac)
{
	int x;

	x = mac[0];
	x ^= mac[0] >> 6 ^ mac[1] << 2;
	x ^= mac[1] >> 4 ^ mac[2] << 4;
	x ^= mac[2] >> 2;
	x ^= mac[3];
	x ^= mac[3] >> 6 ^ mac[4] << 2;
	x ^= mac[4] >> 4 ^ mac[5] << 4;
	x ^= mac[5] >> 2;
	return x & 63;
}

int
viomacok(VIODev *d, u8int *mac)
{
	static u8int bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if((d->net.flags & VNETPROMISC) != 0) return 1;
	if((mac[0] & 1) == 0){
		if((d->net.flags & (VNETNOUNI|VNETALLUNI)) != 0)
			return (d->net.flags & VNETNOUNI) == 0;
		if(memcmp(mac, d->net.mac, 6) == 0) return 1;
		if(d->net.macbloom == 0) return 0;
		return d->net.macbloom >> bloomhash(mac) & 1;
	}else if(memcmp(mac, bcast, 6) == 0)
		return (d->net.flags & VNETNOBCAST) == 0;
	else{
		if((d->net.flags & (VNETNOMULTI|VNETALLMULTI)) != 0)
			return (d->net.flags & VNETNOMULTI) == 0;
		if(d->net.multibloom == 0) return 0;
		return d->net.multibloom >> bloomhash(mac) & 1;
	}
}

void
vionetrproc(void *vp)
{
	VIODev *v;
	VIOQueue *q;
	VIOBuf *vb;
	uchar rxhead[10];
	uchar rxbuf[1600];
	int rc;
	
	threadsetname("vionetrproc");
	v = vp;
	q = &v->qu[0];
	memset(rxhead, 0, sizeof(rxhead));
	for(;;){
		rc = read(v->net.readfd, rxbuf, sizeof(rxbuf));
		if(rc == 0){
			vmerror("read(vionetrproc): eof");
			threadexits("read: eof");
		}
		if(rc < 0){
			vmerror("read(vionetrproc): %r");
			threadexits("read: %r");
		}
		if(rc < 14){
			vmerror("vionetrproc: short packet received (len=%d)", rc);
			continue;
		}
		if(!viomacok(v, rxbuf))
			continue;
		vb = viogetbuf(q, 1);
		if(vb == nil){
			vmerror("viogetbuf: %r");
			continue;
		}
		vioqwrite(vb, rxhead, sizeof(rxhead));
		vioqwrite(vb, rxbuf, rc);
		vioputbuf(vb);
	}
}

void
vionetwproc(void *vp)
{
	VIODev *v;
	VIOQueue *q;
	VIOBuf *vb;
	uchar txhead[10];
	uchar txbuf[1610];
	int rc, len;
	uvlong ns;
	
	threadsetname("vionetwproc");
	v = vp;
	q = &v->qu[1];
	for(;;){
		vb = viogetbuf(q, 1);
		if(vb == nil){
			vmerror("viogetbuf: %r");
			threadexits("viogetbuf: %r");
		}
		vioqread(vb, txhead, sizeof(txhead));
		len = vioqread(vb, txbuf+10, sizeof(txbuf)-10);
		if(len == sizeof(txbuf)-10){
			vmerror("virtio net: ignoring excessively long packet");
			vioputbuf(vb);
			continue;
		}
		if(len < 14){
			/* openbsd ends up sending lots of zero length packets sometimes */
			if(len != 0)
				vmerror("virtio net: ignoring short packet (length=%d)", len);
			vioputbuf(vb);
			continue;
		}else if(len < 60){ /* openbsd doesn't seem to know about ethernet minimum packet lengths either */
			memset(txbuf + 10 + len, 0, 60 - len);
			len = 60;
		}
		if((v->net.flags & VNETHEADER) != 0){
			txbuf[0] = len  >> 8;
			txbuf[1] = len;
			ns = nsec();
			txbuf[2] = ns >> 56;
			txbuf[3] = ns >> 48;
			txbuf[4] = ns >> 40;
			txbuf[5] = ns >> 32;
			txbuf[6] = ns >> 24;
			txbuf[7] = ns >> 16;
			txbuf[8] = ns >> 8;
			txbuf[9] = ns;
			rc = write(v->net.writefd, txbuf, len + 10);
		}else
			rc = write(v->net.writefd, txbuf + 10, len);
		vioputbuf(vb);
		if(rc < 0){
			vmerror("write(vionetwproc): %r");
			continue;
		}
		if(rc < len){
			vmerror("write(vionetwproc): incomplete write (%d < %d)", rc, len);
			continue;
		}
	}
}

u32int
vionetio(int isin, u16int port, u32int val, int sz, VIODev *v)
{
	switch(isin << 16 | port){
	case 0x10000: case 0x10001: case 0x10002: case 0x10003:
		return GET32(v->net.mac, 0) >> (port & 3) * 8;
	case 0x10004: case 0x10005: case 0x10006: case 0x10007:
		return (GET16(v->net.mac, 4) | 1 << 16) >> (port & 3) * 8;
	}
	return iowhine(isin, port, val, sz, "virtio net");
}

int
vionettables(VIODev *d, VIOBuf *b)
{
	u8int buf[4];
	u8int mac[6];
	u64int bloom[2];
	int i, l;
	
	bloom[0] = 0;
	bloom[1] = 0;
	for(i = 0; i < 2; i++){
		if(vioqread(b, buf, 4) < 4)
			return 1;
		l = GET32(buf, 0);
		while(l--){
			if(vioqread(b, mac, 6) < 6)
				return 1;
			bloom[i] |= 1ULL<<bloomhash(mac);
		}
	}
	d->net.macbloom = bloom[0];
	d->net.multibloom = bloom[1];
	return 0;
}

void
vionetcmd(VIOQueue *q)
{
	VIODev *d;
	VIOBuf *b;
	u8int cmd[2], buf[6];
	u8int ack;
	int fl;

	d = q->d;
	for(; b = viogetbuf(q, 0), b != nil; vioputbuf(b)){
		if(vioqread(b, cmd, 2) < 2){
			ack = 1;
			vioqwrite(b, &ack, 1);
			continue;
		}
		ack = 0;
		switch(cmd[0] << 8 | cmd[1]){
		case 0x0000: fl = VNETPROMISC; goto flag;
		case 0x0001: fl = VNETALLMULTI; goto flag;
		case 0x0002: fl = VNETALLUNI; goto flag;
		case 0x0003: fl = VNETNOMULTI; goto flag;
		case 0x0004: fl = VNETNOUNI; goto flag;
		case 0x0005: fl = VNETNOBCAST; goto flag;
		flag:
			if(vioqread(b, buf, 1) < 1) ack = 1;
			else if(buf[0] == 1) d->net.flags |= fl;
			else if(buf[0] == 0) d->net.flags &= ~fl;
			else ack = 1;
			break;
		case 0x0100: /* MAC_TABLE_SET */
			ack = vionettables(d, b);
			break;
		case 0x0101: /* MAC_ADDR_SET */
			if(vioqread(b, buf, 6) < 6) ack = 1;
			else memmove(d->net.mac, buf, 6);
			break;
		default:
			ack = 1;
		}
		vioqwrite(b, &ack, 1);
	}
}

void
vionetreset(VIODev *d)
{
	d->net.flags &= VNETHEADER;
	d->net.macbloom = 0;
	d->net.multibloom = 0;
}

int
mkvionet(char *net)
{
	int fd, cfd;
	VIODev *d;
	char *ea;
	int flags;
	enum { VNETFILE = 1 };

	ea = nil;
	flags = 0;
	for(;;){
		if(strncmp(net, "hdr!", 4) == 0){
			net += 4;
			flags |= VNETHEADER;
		}else if(strncmp(net, "file!", 5) == 0){
			net += 5;
			flags |= VNETFILE;
		}else if(strncmp(net, "ea:", 3) == 0){
			net = strchr(ea = net+3, '!');
			if(net++ == nil){
				werrstr("missing: !");
				return -1;
			}
		}else
			break;
	}
	if((flags & VNETFILE) != 0){
		flags &= ~VNETFILE;
		fd = open(net, ORDWR);
		if(fd < 0) return -1;
	}else{
		fd = dial(netmkaddr("-1", net, nil), nil, nil, &cfd);
		if(fd < 0) return -1;
		if(cfd >= 0) {
			write(cfd, "promiscuous", 11);
			write(cfd, "bridge", 6);
		}
	}
	
	d = mkviodev(0x1000, 0x020000, 1, 3);
	mkvioqueue(d, 1024, viowakeup);
	mkvioqueue(d, 1024, viowakeup);
	mkvioqueue(d, 32, vionetcmd);
	if(ea == nil || parseether(d->net.mac, ea)){
		genrandom(d->net.mac, 6);
		d->net.mac[0] = d->net.mac[0] & ~1 | 2;
	}
	d->net.flags = flags;
	d->devfeat = 1<<5|1<<16|1<<17|1<<18|1<<20;
	d->io = vionetio;
	d->reset = vionetreset;
	d->net.readfd = d->net.writefd = fd;
	proccreate(vionetrproc, d, 8192);
	proccreate(vionetwproc, d, 8192);
	return 0;
}

u32int
vioblkio(int isin, u16int port, u32int val, int sz, VIODev *v)
{
	switch(isin << 16 | port){
	case 0x10000: case 0x10001: case 0x10002: case 0x10003:
		return (u32int)v->blk.size >> (port & 3) * 8;
	case 0x10004: case 0x10005: case 0x10006: case 0x10007:
		return (u32int)(v->blk.size >> 32) >> (port & 3) * 8;
	}
	return iowhine(isin, port, val, sz, "virtio blk");
}

void
vioblkproc(void *vp)
{
	VIODev *v;
	VIOQueue *q;
	VIOBuf *b;
	u8int cmd[16];
	u8int ack;
	char buf[8192];
	uvlong addr;
	int rc, n, m;
	
	threadsetname("vioblkproc");
	v = vp;
	q = &v->qu[0];
	for(;;){
		b = viogetbuf(q, 1);
		if(b == nil){
			vmerror("vioblkproc: viogetbuf: %r");
			threadexits("vioblkproc: viogetbuf: %r");
		}
		ack = 0;
		if(vioqread(b, cmd, sizeof(cmd)) < sizeof(cmd)) goto nope;
		addr = GET64(cmd, 8);
		switch(GET32(cmd, 0)){
		case 0:
			n = vioqrem(b, 1) - 1;
			if(n < 0 || addr * 512 + n > v->blk.size * 512){
				ack = 1;
				break;
			}
			seek(v->blk.fd, addr << 9, 0);
			for(; n > 0; n -= rc){
				rc = sizeof(buf);
				if(n < rc) rc = n;
				rc = read(v->blk.fd, buf, rc);
				if(rc < 0) vmerror("read(vioblkproc): %r");
				if(rc <= 0){
					ack = 1;
					break;
				}
				vioqwrite(b, buf, rc);
			}
			break;
		case 1:
			n = vioqrem(b, 0);
			if(addr * 512 + n > v->blk.size * 512){
				ack = 1;
				break;
			}
			seek(v->blk.fd, addr << 9, 0);
			for(; n > 0; n -= rc){
				m = vioqread(b, buf, sizeof(buf));
				rc = write(v->blk.fd, buf, m);
				if(rc < 0) vmerror("write(vioblkproc): %r");
				if(rc < m){
					ack = 1;
					break;
				}
			}
			break;
		default:
		nope:
			ack = 2;
		}
		vioqwrite(b, &ack, 1);
		vioputbuf(b);
	}
}

int
mkvioblk(char *fn)
{
	int fd;
	VIODev *d;
	
	fd = open(fn, ORDWR);
	if(fd < 0) return -1;
	d = mkviodev(0x1000, 0x018000, 2, 1);
	mkvioqueue(d, 32, viowakeup);
	d->io = vioblkio;
	d->blk.fd = fd;
	d->blk.size = seek(fd, 0, 2) >> 9;
	proccreate(vioblkproc, d, 16384);
	return 0;
}
