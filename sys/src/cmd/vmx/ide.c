#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

typedef struct IDE IDE;
typedef struct IDEIO IDEIO;

struct IDEIO {
	QLock;
	Rendez;
	u8int rbuf[8192];
	u8int wbuf[1024];
	u16int rbufrp, rbufwp;
	u16int wbufrp, wbufwp;
	vlong addr;
	int cnt;
	u8int err, scratched, wr;
	enum {
		IIOIDLE,
		IIOBUSY,
	} state;
};

struct IDE {
	IDEIO io;
	enum {
		IDEPRESENT = 1,
		IDEKEEPFEAT = 2,
		IDEPIOREAD = 4,
		IDEPIOWRITE = 8,
	} flags;
	u8int stat, err, irq;
	u8int ctrl , feat;
	u8int sec, cnt;
	u16int cyl;
	u8int head;
	int fd;
	vlong size;
	int pcyl, phead, psec;
	int lcyl, lhead, lsec;
} ide[4];

uchar ideint13[4*12];
int idediskno;
enum {
	/* ctrl */
	IDESRST = 4,
	IDENIEN = 2,
	/* stat */
	IDEBUSY = 0x80,
	IDEDRDY = 0x40,
	IDEDSC = 0x10,
	IDEDRQ = 0x08,
	IDEERR = 0x01,
	/* error */
	IDEUNC = 0x40,
	IDEIDNF = 0x10,
	IDEABRT = 0x04,
};

static void
idereset(IDE *d)
{
	d->ctrl &= IDESRST;
	if((d->flags & IDEPRESENT) != 0){
		qlock(&d->io);
		while(d->io.state != IIOIDLE){
			d->io.scratched = 1;
			rwakeup(&d->io);
			rsleep(&d->io);
		}
		d->io.rbufrp = d->io.rbufwp = 0;
		d->io.scratched = 0;
		qunlock(&d->io);
		d->stat = IDEDRDY | IDEDSC;
		d->err = 1;
		d->flags &= IDEPRESENT | IDEKEEPFEAT;
		d->sec = d->cnt = 1;
		d->cyl = 0;
		d->head = 0xa0;
		d->feat = 0;
	}
}

static void
ideirq(IDE *d, int n)
{
	IDE *s;

	if(n >= 0)
		d->irq = n;
	s = (d - ide & ~1) + (d->head >> 4 & 1) + ide;
	irqline(14 + (d - ide)/2, s->irq && (s->ctrl & IDENIEN) == 0);
}

static vlong
getlba(IDE *d)
{
	int he;

	if((d->head & 0x40) != 0)
		return d->sec | d->cyl << 8 | (d->head & 0xf) << 16;
	if(d->sec == 0 || d->sec > d->lsec)
		return -1;
	he = d->head & 0xf;
	if(d->cyl >= d->lcyl || he >= d->lhead)
			return -1;
	return d->sec - 1 + (he + d->cyl * d->lhead) * d->lsec;

}

static void
idegoio(IDE *d, int wr)
{
	vlong addr;
	
	addr = getlba(d);
	if(addr < 0){
		vmerror("ide%d: access to invalid sector address (access to CHS=(%#.4ux,%#ux,%#.2ux); geometry is (%#.4ux,%#ux,%#.2ux)", d-ide, d->cyl, d->head&0xf, d->sec, d->lcyl, d->lhead, d->lsec);
		postexc("#bp", NOERRC);
		d->stat = IDEDRDY | IDEDSC | IDEDRQ | IDEERR;
		d->err = IDEIDNF;
		ideirq(d, 1);
		return;
	}
	if(wr){
		d->stat = IDEDRDY | IDEDRQ | IDEDSC;
		d->flags |= IDEPIOWRITE;
	}else{
		d->stat = IDEDRDY | IDEBUSY | IDEDSC;
		d->flags |= IDEPIOREAD;
	}
	qlock(&d->io);
	while(d->io.state != IIOIDLE)
		rsleep(&d->io);
	d->io.addr = addr;
	d->io.cnt = (d->cnt - 1 & 0xff) + 1;
	d->io.err = 0;
	d->io.wr = wr;
	d->io.state = IIOBUSY;
	rwakeup(&d->io);
	qunlock(&d->io);
}

static void
ideincaddr(IDE *d)
{
	if((d->head & 0x40) != 0){
		if(d->sec++ == 255 && d->cyl++ == 65535)
			d->head = d->head + 1 & 0xf | d->head & 0xf0;
	}else{
		if(d->sec++ == d->lsec){
			d->sec = 1;
			if((d->head & 0xf) == d->lhead-1){
				d->head &= 0xf0;
				if(d->cyl++ == d->lcyl)
					d->cyl = 0;
			}else
				d->head++;
		}
	}
}

static void
idesecrend(IDE *d)
{
	if((d->flags & IDEPIOREAD) == 0){
		d->stat &= ~IDEDRQ;
		return;
	}
	ideincaddr(d);
	if(d->io.rbufwp == (u16int)(d->io.rbufrp + sizeof(d->io.rbuf) - 512))
		rwakeup(&d->io);
	if(--d->cnt == 0){
		d->stat &= ~(IDEBUSY|IDEDRQ);
		d->flags &= ~IDEPIOREAD;
	}else if(d->io.rbufrp == d->io.rbufwp){
		if(d->io.err != 0){
			d->stat = d->stat | IDEERR;
			d->err = d->io.err;
			ideirq(d, 1);
		}else
			d->stat = d->stat & ~IDEDRQ | IDEBUSY;
	}else
		ideirq(d, 1);
}

static void
idesecrdone(void *dp)
{
	IDE *d;
	
	d = dp;
	qlock(&d->io);
	d->stat = d->stat & ~IDEBUSY | IDEDRQ;
	if(d->io.err != 0){
		d->stat |= IDEERR;
		d->err = d->io.err;
	}
	ideirq(d, 1);
	qunlock(&d->io);
}

static void
idesecwend(IDE *d)
{
	ideincaddr(d);
	d->cnt--;
	if((u16int)(d->io.wbufwp - 512) == d->io.wbufrp)
		rwakeup(&d->io);
	if(d->io.wbufwp == (u16int)(d->io.wbufrp + sizeof(d->io.wbuf))){
		d->stat = d->stat & ~IDEDRQ | IDEBUSY;
	}else{
		if(d->cnt == 0)
			d->stat = d->stat & ~(IDEDRQ|IDEBUSY);
		ideirq(d, 1);
	}
}

static void
idesecwdone(void *dp)
{
	IDE *d;
	
	d = dp;
	qlock(&d->io);
	if(d->cnt == 0)
		d->stat = d->stat & ~(IDEDRQ|IDEBUSY);
	else
		d->stat = d->stat & ~IDEBUSY | IDEDRQ;
	ideirq(d, 1);
	qunlock(&d->io);
}

typedef struct Sector Sector;
struct Sector {
	uchar data[512];
	vlong addr;
	Sector *next;
};
Sector *sectors;

static int
getsector(vlong a, uchar *p)
{
	Sector *s;
	
	for(s = sectors; s != nil; s = s->next)
		if(s->addr == a){
			vmdebug("reading updated sector %lld", a);
			memmove(p, s->data, 512);
			return 0;
		}
	return -1;
}

static void
putsector(vlong a, uchar *dp)
{
	Sector *s, **p;
	
	for(p = &sectors; s = *p, s != nil; p = &s->next)
		if(s->addr == a){
			memmove(s->data, dp, 512);
			return;
		}
	s = emalloc(sizeof(Sector));
	s->addr = a;
	memmove(s->data, dp, 512);
	*p = s;
}

static void
ideioproc(void *dp)
{
	IDE *d;
	IDEIO *io;
	vlong a;
	uchar *p;
	int i, n;
	
	d = dp;
	io = &d->io;
	threadsetname("ide");
	for(;;){
		qlock(io);
		io->state = IIOIDLE;
		rwakeup(io);
		while(io->state == IIOIDLE)
			rsleep(io);
		a = io->addr;
		n = io->cnt;
		qunlock(io);
		
		if(io->wr){
			for(i = 0; i < n; i++){
				qlock(io);
				while(!io->scratched && io->wbufrp == (io->wbufwp & ~511))
					rsleep(io);
				if(io->scratched){
					qunlock(io);
					break;
				}
				p = io->wbuf + (io->wbufrp & sizeof(io->wbuf) - 1);
				qunlock(io);
				putsector(a+i, p);
				qlock(io);
				if(io->wbufwp == (u16int)(io->wbufrp + sizeof(io->wbuf)))
					sendnotif(idesecwdone, d);
				io->wbufrp += 512;
				qunlock(io);
			}
		}else{
			for(i = 0; i < n; i++){
				qlock(io);
				while(!io->scratched && io->rbufwp == (u16int)((io->rbufrp & ~511) + sizeof(io->rbuf)))
					rsleep(io);
				if(io->scratched){
					qunlock(io);
					break;
				}
				p = io->rbuf + (io->rbufwp & sizeof(io->rbuf) - 1);
				qunlock(io);
				werrstr("eof");
				if(getsector(a+i, p) < 0 && pread(d->fd, p, 512, (a+i)*512) < 512){
					vmerror("ide%d: read: %r", d - ide);
					qlock(io);
					io->err = IDEUNC;
					qunlock(io);
					sendnotif(idesecrdone, d);
					break;
				}
				qlock(io);
				if(io->rbufrp == io->rbufwp)
					sendnotif(idesecrdone, d);
				io->rbufwp += 512;
				qunlock(io);
			}
		}
	}
}

static void
idecmd(IDE *d, u8int cmd)
{
	u8int *p;
	vlong vl;

	if(cmd == 0)
		return;
	switch(cmd){
	case 0x90:
		break;
	default:
		if((d->flags & IDEPRESENT) == 0){
			vmerror("ide%d: command %#ux issued to absent drive", d-ide, cmd);
			return;
		}
	}
	if(cmd >> 4 == 1 || cmd >> 4 == 7){
		/* relibrate / seek */
		d->stat = IDEDRDY|IDEDSC;
		ideirq(d, 1);
		return;
	}
	switch(cmd){
	case 0x20: case 0x21: /* read (pio) */
		idegoio(d, 0);
		break;
	case 0x30: case 0x31: /* write (pio) */
		idegoio(d, 1);
		break;
	case 0x40: case 0x41: /* read verify */
		while(--d->cnt != 0)
			ideincaddr(d);
		d->stat = IDEDRDY|IDEDSC;
		ideirq(d, 1);
		break;
	case 0x90: /* diagnostics */
		d = (d - ide & ~1) + ide;
		d[0].err = 0;
		d[1].err = 0;
		if((d->flags & IDEPRESENT) != 0){
			d->stat = IDEDRDY|IDEDSC;
			ideirq(d, 1);
		}
		break;
	case 0x91: /* set translation mode */
		d->lhead = (d->head & 0xf) + 1;
		d->lsec = d->cnt;
		if(d->cnt != 0){
			vl = d->size / (d->lhead * d->lsec);
			d->lcyl = vl >= 65535 ? 65535 : vl;
		}
		d->stat = IDEDRDY|IDEDSC;
		ideirq(d, 1);
		break;
	case 0xec: /* identify */
		qlock(&d->io);
		while(d->io.state != IIOIDLE)
			rsleep(&d->io);
		p = d->io.rbuf + (d->io.rbufwp & sizeof(d->io.rbuf) - 1);
		d->io.rbufwp += 512;
		memset(p, 0, 512);
		strcpy((char*)p+20, "13149562358579393248");
		strcpy((char*)p+46, ".2.781  ");
		sprint((char*)p+54, "%-40s", "jhidks s");
		PUT16(p, 0, 0x40);
		PUT16(p, 2, d->pcyl);
		PUT16(p, 6, d->phead);
		PUT16(p, 8, d->psec << 9);
		PUT16(p, 10, 512);
		PUT16(p, 12, d->psec);
		PUT16(p, 98, 0x200);
		if(d->lsec != 0){
			PUT16(d, 106, 1);
			PUT16(d, 108, d->lcyl);
			PUT16(d, 110, d->lhead);
			PUT16(d, 112, d->lsec);
			PUT32(d, 114, d->lcyl * d->lhead * d->lsec);
		}
		PUT32(p, 120, d->size >= (u32int)-1 ? -1 : d->size);
		PUT16(p, 160, 7);
		qunlock(&d->io);
		d->stat = IDEDRDY|IDEDSC|IDEDRQ;
		ideirq(d, 1);
		break;
	case 0xef: /* set feature */
		switch(d->feat){
		case 1: case 0x81: /* enable/disable 8-bit transfers */
		case 2: case 0x82: /* enable/disable cache */
			break;
		case 0x66: d->flags |= IDEKEEPFEAT; break; /* retain settings */
		case 0xcc: d->flags &= ~IDEKEEPFEAT; break; /* revert to default on reset */
		default:
			vmerror("ide%d: unknown feature %#ux", d-ide, d->feat);
			d->stat = IDEDRDY|IDEDSC|IDEERR;
			d->err = IDEABRT;
			return;
		}
		d->stat = IDEDRDY|IDEDSC;
		break;
	default:
		vmerror("ide%d: unknown command %#ux", d-ide, cmd);
		d->stat = IDEDRDY|IDEDSC|IDEERR;
		d->err = IDEABRT;
	}
}

u32int
ideio(int isin, u16int port, u32int val, int sz, void *)
{
	IDE *d, *e;
	u32int rc;

	d = &ide[2 * ((port & 0x80) == 0)];
	d += d->head >> 4 & 1;
	e = (d - ide ^ 1) + ide;
	if((port|0x80) != 0x1f0){
		if(sz != 1)
			vmerror("ide: access to port %#x with incorrect size %d", port, sz);
		val = (u8int) val;
	}
	if(isin && (d->flags & IDEPRESENT) == 0)
		return 0;
	switch(isin << 16 | port | 0x80){
	case 0x001f0:
		if((d->flags & IDEPIOWRITE) == 0)
			return 0;
		qlock(&d->io);
		PUT16(d->io.wbuf, d->io.wbufwp & sizeof(d->io.wbuf) - 1, (u16int)val);
		d->io.wbufwp += 2;
		if((d->io.wbufwp & 511) == 0)
			idesecwend(d);
		qunlock(&d->io);
		if(sz == 4)
			ideio(0, port, val >> 16, 2, nil);
		return 0;
	case 0x001f1: d->feat = e->feat = val; return 0;
	case 0x001f2: d->cnt = e->cnt = val; return 0;
	case 0x001f3: d->sec = e->sec = val; return 0;
	case 0x001f4: d->cyl = d->cyl & 0xff00 | val; e->cyl = e->cyl & 0xff00 | val; return 0;
	case 0x001f5: d->cyl = d->cyl & 0xff | val << 8; e->cyl = e->cyl & 0xff | val << 8; return 0;
	case 0x001f6: d->head = e->head = val | 0xa0; return 0;
	case 0x001f7: idecmd(d, val); return 0;
	case 0x003f6:
		d->ctrl = e->ctrl = val;
		if((val & IDESRST) != 0){
			idereset(d);
			idereset(e);
		}
		ideirq(d, -1);
		return 0;

	case 0x101f0:
		qlock(&d->io);
		if(d->io.rbufrp != d->io.rbufwp){
			rc = GET16(d->io.rbuf, d->io.rbufrp & sizeof(d->io.rbuf)-1);
			d->io.rbufrp += 2;
			if((d->io.rbufrp & 511) == 0)
				idesecrend(d);
		}else
			rc = 0;
		qunlock(&d->io);
		if(sz == 4)
			rc |= ideio(1, port, 0, 2, nil) << 16;
		return rc;
	case 0x101f1: return d->err;
	case 0x101f2: return d->cnt;
	case 0x101f3: return d->sec;
	case 0x101f4: return (u8int)d->cyl;
	case 0x101f5: return d->cyl >> 8;
	case 0x101f6: return d->head;
	case 0x101f7:
		ideirq(d, 0);
	case 0x103f6:
		if((d->ctrl & IDESRST) != 0)
			return IDEBUSY;
		rc = d->stat;
		/* stupid hack to work around different expectations of how DRQ behaves on error */
		if((d->stat & IDEERR) != 0)
			d->stat &= ~IDEDRQ;
		return rc;
	default: return iowhine(isin, port, val, sz, "ide");
	}
}

static int
idegeom(vlong vsz, int *cp, int *hp, int *sp)
{
	int sz, c, h, s, t;
	int max;
	
	if(vsz >= 63*255*1023){
		*cp = 1023;
		*hp = 255;
		*sp = 63;
		return 0;
	}
	sz = vsz;
	max = 0;
	for(s = 63; s >= 1; s--)
		for(h = 1; h <= 16; h++){
			c = sz / (h * s);
			if(c >= 1023) c = 1023;
			t = c * h * s;
			if(t > max){
				max = t;
				*cp = c;
				*hp = h;
				*sp = s;
			}
		}
	return max == 0 ? -1 : 0;
}

int
mkideblk(char *fn)
{
	int fd;
	IDE *d;
	uchar *p;
	
	if(idediskno >= 4){
		werrstr("too many ide disks");
		return -1;
	}
	d = &ide[idediskno];
	d->io.Rendez.l = &d->io;
	fd = open(fn, ORDWR);
	if(fd < 0)
		return -1;
	d->size = seek(fd, 0, 2) >> 9;
	if(idegeom(d->size, &d->pcyl, &d->phead, &d->psec) < 0){
		werrstr("disk file too small");
		return -1;
	}
	if(idediskno < 2){
		cmos[0x12] |= 0xf << (1-idediskno) * 4;
		cmos[0x19 + idediskno] = 47;
	}
	p = ideint13 + idediskno * 12;
	PUT16(p, 0, 0x80 | idediskno);
	PUT32(p, 2, d->pcyl);
	PUT16(p, 6, d->psec);
	PUT16(p, 8, d->phead);
	PUT16(p, 10, 1);
	d->flags |= IDEPRESENT;
	d->fd = fd;
	idereset(&ide[idediskno]);
	idediskno++;
	proccreate(ideioproc, d, 8192);
	return 0;

}
