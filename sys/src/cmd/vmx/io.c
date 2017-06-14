#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <cursor.h>
#include <mouse.h>
#include "dat.h"
#include "fns.h"

static uchar
bcd(uchar c)
{
	return c / 10 << 4 | c % 10;
}

static u32int
rtcio(int isin, u16int port, u32int val, int sz, void *)
{
	static u8int addr;
	uintptr basemem, extmem;
	Tm *tm;
	
	switch(isin << 16 | port){
	case 0x10070: return addr;
	case 0x70: addr = val; return 0;
	case 0x10071:
		tm = gmtime(time(nil));
		basemem = gavail(gptr(0, 0)) >> 10;
		if(basemem > 640) basemem = 640;
		extmem = gavail(gptr(1<<20, 0)) >> 10;
		if(extmem >= 65535) extmem = 65535;
		switch(addr){
		case 0x00: return bcd(tm->sec);
		case 0x02: return bcd(tm->min);
		case 0x04: return bcd(tm->hour);
		case 0x06: return bcd(tm->wday + 1);
		case 0x07: return bcd(tm->mday);
		case 0x08: return bcd(tm->mon + 1);
		case 0x09: return bcd(tm->year % 100);
		case 0x0a: return 0x26;
		case 0x0b: return 1<<1 | 1<<2;
		case 0x0d: return 1<<7; /* cmos valid */
		case 0x0e: return 0; /* diagnostics ok */
		case 0x10: return 0; /* no floppies */
		case 0x15: return basemem;
		case 0x16: return basemem >> 8;
		case 0x17: return extmem;
		case 0x18: return extmem >> 8;
		case 0x32: return bcd(tm->year / 100 + 19);
		default: vmerror("rtc read from unknown address %#x", addr); return 0;
		}
	}
	return iowhine(isin, port, val, sz, "rtc");
}

typedef struct Pic Pic;
struct Pic {
	enum {
		AEOI = 1,
		ROTAEOI = 2,
		MASKMODE = 4,
		POLL = 8,
		READSR = 16,
	} flags;
	u8int lines;
	u8int irr, isr;
	u8int imr;
	u8int elcr;
	u8int init;
	u8int prio;
	u8int base;
} pic[2];
int irqactive = -1;

static u8int
picprio(u8int v, u8int p, u8int *n)
{
	p++;
	v = v >> p | v << 8 - p;
	v &= -v;
	v = v << p | v >> 8 - p;
	if(n != nil)
		*n = ((v & 0xf0) != 0) << 2 | ((v & 0xcc) != 0) << 1 | (v & 0xaa) != 0;
	return v;
}

static u8int
piccheck(Pic *p, u8int *n)
{
	u8int s;
	
	s = p->isr;
	if((p->flags & MASKMODE) != 0 && p->imr != 0)
		s = 0;
	return picprio(p->irr & ~p->imr | s, p->prio, n) & ~s;
}

static void
picaeoi(Pic *p, u8int b)
{
	if((p->flags & AEOI) == 0)
		return;
	p->isr &= ~(1<<b);
	if((p->flags & ROTAEOI) != 0)
		p->prio = b;
}

static void
picupdate(Pic *p)
{
	u8int m, n;
	
	if(p->init != 4) return;
	m = piccheck(p, &n);
	if(p == &pic[1])
		irqline(2, m != 0);
	else{
		if(m != 0 && n == 2){
			m = piccheck(&pic[1], &n);
			n |= pic[1].base;
		}else
			n |= p->base;
		if(m != 0 && irqactive != n){
			if(ctl("irq %d", n) < 0)
				sysfatal("ctl: %r");
			halt = 0;
			irqactive = n;
		}else if(m == 0 && irqactive >= 0){
			if(ctl("irq") < 0)
				sysfatal("ctl: %r");
			irqactive = -1;
		}
	}
}

void
irqline(int n, int s)
{
	Pic *p;
	u8int ol, m;
	
	assert(n >= 0 && n <= 15);
	p = &pic[n / 8];
	n %= 8;
	ol = p->lines;
	m = 1<<n;
	switch(s){
	case 1: case IRQLLOHI: p->lines |= m; break;
	case 0: p->lines &= ~m; break;
	case IRQLTOGGLE: p->lines ^= m; break;
	default: assert(0);
	}
	if((p->elcr & m) != 0)
		p->irr = p->irr & ~m | ~p->lines & m;
	else
		p->irr |= p->lines & ~ol & m;
	if(s == IRQLLOHI && (p->elcr & m) == 0)
		p->irr |= m;
	picupdate(p);
}

void
irqack(int n)
{
	Pic *p;
	extern int nextexit;
	
	irqactive = -1;
	if((n & ~7) == pic[0].base)
		p = &pic[0];
	else if((n & ~7) == pic[1].base)
		p = &pic[1];
	else
		return;
	if(p == &pic[1]) irqack(pic[0].base + 2);
	n &= 7;
	p->irr &= ~(1<<n);
	p->isr |= 1<<n;
	picaeoi(p, n);
	picupdate(p);
}

void
elcr(u16int a)
{
	pic[0].elcr = a;
	pic[1].elcr = a >> 8;
}

static u32int
picio(int isin, u16int port, u32int val, int sz, void *)
{
	Pic *p;
	u8int m, b;
	
	p = &pic[(port & 0x80) != 0];
	val = (u8int)val;
	switch(isin << 16 | port){
	case 0x20:
	case 0xa0:
		if((val & 1<<4) != 0){ /* ICW1 */
			if(irqactive){
				if(ctl("irq") < 0)
					sysfatal("ctl: %r");
				irqactive = -1;
			}
			p->irr = 0;
			p->isr = 0;
			p->imr = 0;
			p->prio = 7;
			p->flags = 0;
			if((val & 0x0b) != 0x01) vmerror("PIC%ld ICW1 with unsupported value %#ux", p-pic, val);
			p->init = 1;
			return 0;
		}
		if((val & 0x18) == 0){ /* OCW2 */
			switch(val >> 5){
			case 0: /* rotate in automatic eoi mode (clear) */
				p->flags &= ~ROTAEOI;
				break;
			case 1: /* non-specific eoi command */
				p->isr &= ~picprio(p->isr, p->prio, nil);
				break;
			case 2: /* no operation */
				break;
			case 3: /* specific eoi command */
				p->isr &= 1<<(val & 7);
				break;
			case 4: /* rotate in automatic eoi mode (set) */
				p->flags |= ROTAEOI;
				break;
			case 5: /* rotate on non-specific eoi command */
				p->isr &= ~picprio(p->isr, p->prio, &p->prio);
				break;
			case 6: /* set priority */
				p->prio = val & 7;
				break;
			case 7: /* rotate on specific eoi command */
				p->isr &= 1<<(val & 7);
				p->prio = val & 7;
				break;
			}
			picupdate(p);
			return 0;
		}
		if((val & 0x98) == 8){ /* OCW3 */
			if((val & 0x40) != 0)
				if((val & 0x20) != 0)
					p->flags |= MASKMODE;
				else
					p->flags &= ~MASKMODE;
			if((val & 4) != 0)
				p->flags |= POLL;
			if((val & 2) != 0)
				if((val & 10) != 0)
					p->flags |= READSR;
				else
					p->flags &= ~READSR;
			picupdate(p);
			
		}
		return 0;
	case 0x21:
	case 0xa1:
		switch(p->init){
		default:
			vmerror("write to PIC%ld in init=%d state", p-pic, p->init);
			return 0;
		case 1:
			p->base = val;
			p->init = 2;
			return 0;
		case 2:
			if(p == &pic[0] && val != 4 || p == &pic[1] && val != 2)
				vmerror("PIC%ld ICW3 with unsupported value %#ux", p-pic, val);
			p->init = 3;
			return 0;
		case 3:
			if((val & 0xfd) != 1) vmerror("PIC%ld ICW4 with unsupported value %#ux", p-pic, val);
			if((val & 2) != 0) p->flags |= AEOI;
			p->init = 4;
			picupdate(p);
			return 0;
		case 4:
			p->imr = val;
			picupdate(p); 
			return 0;
		}
		break;
	case 0x10020:
		if((p->flags & READSR) != 0)
			return p->isr;
		if((p->flags & POLL) != 0){
			p->flags &= ~POLL;
			m = piccheck(p, &b);
			if(m != 0){
				p->irr &= ~m;
				p->isr |= m;
				picaeoi(p, b);
				picupdate(p);
				return 1<<7 | b;
			}
			return 0;
		}
		return p->irr;
	case 0x100a0:
	case 0x10021:
	case 0x100a1:
		return p->imr;
	case 0x4d0:
	case 0x4d1:
		pic[port & 1].elcr = val;
		return 0;
	case 0x104d0:
	case 0x104d1:
		return pic[port & 1].elcr;
	}
	return iowhine(isin, port, val, sz, "pic");
}

typedef struct PITChannel PITChannel;

struct PITChannel {
	u8int mode;
	u8int bcd;
	u8int access;
	u8int state;
	u16int count, reload;
	int latch;
	enum { READLO, READHI, READLATLO, READLATHI } readstate;
	u8int writestate;
	vlong lastnsec;
};
PITChannel pit[3] = {
	[0] { .state 1 },
};
enum { PERIOD = 838 };

void
settimer(vlong targ)
{
	extern vlong timerevent;
	extern Lock timerlock;
	extern int timerid;
	int sendint;

	sendint = 0;
	lock(&timerlock);
	if(targ < timerevent){
		timerevent = targ;
		sendint = 1;
	}
	unlock(&timerlock);
	if(sendint)
		threadint(timerid);
}

void
pitadvance(void)
{
	int i;
	int nc;
	PITChannel *p;
	vlong nt, t;
	int rel;

	for(i = 0; i < 3; i++){
		p = &pit[i];
		nt = nsec();
		t = nt - p->lastnsec;
		p->lastnsec = nt;
		switch(p->mode){
		case 0:
			if(p->state != 0){
				nc = t / PERIOD;
				if(p->count <= nc && i == 0)
					irqline(0, 1);
				p->count -= nc;
				p->lastnsec -= t % PERIOD;
				if(i == 0 && (pic[0].lines & 1<<0) == 0)
					settimer(p->lastnsec + p->count * PERIOD);
			}
			break;
		case 2:
			if(p->state != 0){
				nc = t / PERIOD;
				if(p->count == 0 || p->count - 1 > nc)
					p->count -= nc;
				else{
					rel = p->reload - 1;
					if(rel <= 0) rel = 65535;
					nc -= p->count - 1;
					nc %= rel;
					p->count = rel - nc + 1;
					if(i == 0)
						irqline(0, IRQLLOHI);
				}
				p->lastnsec -= t % PERIOD;
				settimer(p->lastnsec + p->count * PERIOD);
			}
			break;
		case 3:
			if(p->state != 0){
				nc = 2 * (t / PERIOD);
				if(p->count > nc)
					p->count -= nc;
				else{
					rel = p->reload;
					if(rel <= 1) rel = 65536;
					nc -= p->count;
					nc %= rel;
					p->count = rel - nc;
					if(i == 0)
						irqline(0, IRQLTOGGLE);
				}
				p->lastnsec -= t % PERIOD;
				settimer(p->lastnsec + p->count / 2 * PERIOD);
			}
			break;
		}
	}
}

static void
pitsetreload(int n, int hi, u8int v)
{
	PITChannel *p;
	
	p = &pit[n];
	if(hi)
		p->reload = p->reload >> 8 | v << 8;
	else
		p->reload = p->reload & 0xff00 | v;
	switch(p->mode){
	case 0:
		if(n == 0)
			irqline(0, 0);
		if(p->access != 3 || hi){
			p->count = p->reload;
			p->state = 1;
			p->lastnsec = nsec();
			settimer(p->lastnsec + p->count * PERIOD);
		}else
			p->state = 0;
		break;
	case 2:
	case 3:
		if(p->state == 0 && (p->access != 3 || hi)){
			p->count = p->reload;
			p->state = 1;
			p->lastnsec = nsec();
			pitadvance();
		}
		break;
	default:
		vmerror("PIT reload in mode %d not implemented", p->mode);
		break;
	}	
}

static u32int
pitio(int isin, u16int port, u32int val, int sz, void *)
{
	int n;

	val = (u8int) val;
	pitadvance();
	switch(isin << 16 | port){
	case 0x10040:
	case 0x10041:
	case 0x10042:
		n = port & 3;
		switch(pit[n].readstate){
		case READLO:
			if(pit[n].access == 3)
				pit[n].readstate = READHI;
			return pit[n].count;
		case READHI:
			if(pit[n].access == 3)
				pit[n].readstate = READLO;
			return pit[n].count >> 8;
		case READLATLO:
			pit[n].readstate = READLATHI;
			return pit[n].latch;
		case READLATHI:
			pit[n].readstate = pit[n].access == 1 ? READHI : READLO;
			return pit[n].latch >> 8;
		}
		return 0;
	case 0x40:
	case 0x41:
	case 0x42:
		n = port & 3;
		switch(pit[n].writestate){
		case READLO:
			if(pit[n].access == 3)
				pit[n].writestate = READHI;
			pitsetreload(n, 0, val);
			break;
		case READHI:
			if(pit[n].access == 3)
				pit[n].writestate = READLO;
			pitsetreload(n, 1, val);
			break;
		}
		return 0;
	case 0x43:
		n = val >> 6;
		if(n == 3) return 0;
		if((val & ~0xc0) == 0){
			pit[n].latch = pit[n].count;
			pit[n].readstate = READLATLO;
		}else{
			pit[n].mode = val >> 1 & 7;
			pit[n].access = val >> 4 & 3;
			pit[n].bcd = val & 1;
			if(pit[n].bcd != 0)
				vmerror("pit: bcd mode not implemented");
			switch(pit[n].mode){
			case 0: case 2: case 3: break;
			default: vmerror("pit: mode %d not implemented", pit[n].mode);
			}
			pit[n].state = 0;
			pit[n].count = 0;
			pit[n].reload = 0;
			pit[n].readstate = pit[n].access == 1 ? READHI : READLO;
			pit[n].writestate = pit[n].access == 1 ? READHI : READLO;
			pit[n].lastnsec = nsec();
			if(n == 0)
				switch(pit[n].mode){
				case 0:
					irqline(0, 0);
					break;
				default:
					irqline(0, 1);
				}
		}
		return 0;
	}
	return iowhine(isin, port, val, sz, "pit");
}

typedef struct I8042 I8042;
struct I8042 {
	u8int cfg, stat, oport;
	int cmd;
	u16int buf; /* |0x100 == kbd, |0x200 == mouse, |0x400 == cmd */
} i8042 = {
	.cfg 0x34,
	.stat 0x10,
	.oport 0x01,
	.cmd -1,
};
Channel *kbdch, *mousech;
typedef struct PCKeyb PCKeyb;
struct PCKeyb {
	u8int buf[64];
	u8int bufr, bufw;
	u8int actcmd;
	u8int quiet;
} kbd;
typedef struct PCMouse PCMouse;
struct PCMouse {
	Mouse;
	u8int gotmouse;
	enum {
		MOUSERESET,
		MOUSESTREAM,
		MOUSEREMOTE,
		MOUSEREP = 0x10,
		MOUSEWRAP = 0x20,
	} state;
	u8int buf[64];
	u8int bufr, bufw;
	u8int actcmd;
	u8int scaling21, res, rate;
} mouse = {
	.res = 2,
	.rate = 100
};
#define keyputc(c) kbd.buf[kbd.bufw++ & 63] = (c)
#define mouseputc(c) mouse.buf[mouse.bufw++ & 63] = (c)

static void
i8042putbuf(u16int val)
{
	i8042.buf = val;
	i8042.stat = i8042.stat & ~0x20 | val >> 4 & 0x20;
	if((i8042.cfg & 1) != 0 && (val & 0x100) != 0){
		irqline(1, 1);
		i8042.oport |= 0x10;
	}
	if((i8042.cfg & 2) != 0 && (val & 0x200) != 0){
		irqline(12, 1);
		i8042.oport |= 0x20;
	}
	if(val == 0){
		irqline(1, 0);
		irqline(12, 0);
		i8042.oport &= ~0x30;
		i8042.stat &= ~1;
		i8042kick(nil);
	}else
		i8042.stat |= 1;
}

static void
kbdcmd(u8int val)
{
	switch(kbd.actcmd){
	case 0xf0: /* set scancode set */
		keyputc(0xfa);
		if(val == 0) keyputc(1);
		kbd.actcmd = 0;
		break;
	case 0x3d: /* set leds */
		keyputc(0xfa);
		kbd.actcmd = 0;
		break;
	default:
		switch(val){
		case 0xed: case 0xf0: kbd.actcmd = val; keyputc(0xfa); break;

		case 0xff: keyputc(0xfa); break; /* reset */
		case 0xf5: kbd.quiet = 1; keyputc(0xfa); break; /* disable scanning */
		case 0xf4: kbd.quiet = 0; keyputc(0xfa); break; /* enable scanning */
		case 0xf2: keyputc(0xfa); keyputc(0xab); keyputc(0x83); break; /* keyboard id */
		case 0xee: keyputc(0xee); break; /* echo */
		default:
			vmerror("unknown kbd command %#ux", val);
		}
	}
	i8042kick(nil);
}

static void
updatemouse(void)
{
	Mouse m;
	
	while(nbrecv(mousech, &m) > 0){
		mouse.xy = addpt(mouse.xy, m.xy);
		mouse.buttons = m.buttons;
		mouse.gotmouse = 1;
	}
}

static void
clearmouse(void)
{
	updatemouse();
	mouse.xy = Pt(0, 0);
	mouse.gotmouse = 0;
}

static void
mousepacket(int force)
{
	int dx, dy;
	u8int b0;

	updatemouse();
	if(!mouse.gotmouse && !force)
		return;
	dx = mouse.xy.x;
	dy = -mouse.xy.y;
	b0 = 8;
	if((ulong)(dx + 256) > 511) dx = dx >> 31 & 0x1ff ^ 0xff;
	if((ulong)(dy + 256) > 511) dy = dy >> 31 & 0x1ff ^ 0xff;
	b0 |= dx >> 5 & 0x10 | dy >> 4 & 0x20;
	b0 |= (mouse.buttons * 0x111 & 0x421) % 7;
	mouseputc(b0);
	mouseputc((u8int)dx);
	mouseputc((u8int)dy);
	mouse.xy.x -= dx;
	mouse.xy.y += dy;
	mouse.gotmouse = 0;
}

static void
mousedefaults(void)
{
	clearmouse();
	mouse.res = 2;
	mouse.rate = 100;
}

static void
mousecmd(u8int val)
{
	if((mouse.state & MOUSEWRAP) != 0 && val != 0xec && val != 0xff){
		mouseputc(val);
		i8042kick(nil);
		return;
	}
	switch(mouse.actcmd){
	case 0xe8: /* set resolution */
		mouse.res = val;
		mouseputc(0xfa);
		mouse.actcmd = 0;
		break;
	case 0xf3: /* set sampling rate */
		mouse.rate = val;
		mouseputc(0xfa);
		mouse.actcmd = 0;
		break;
	default:
		switch(val){
		case 0xf3: case 0xe8: mouseputc(0xfa); mouse.actcmd = val; break;
		
		case 0xff: mouseputc(0xfa); mousedefaults(); mouse.state = MOUSERESET; break; /* reset */
		case 0xf6: mouseputc(0xfa); mousedefaults(); mouse.state = mouse.state & ~0xf | MOUSESTREAM; break; /* set defaults */
		case 0xf5: mouseputc(0xfa); clearmouse(); if((mouse.state&0xf) == MOUSESTREAM) mouse.state &= ~MOUSEREP; break; /* disable reporting */
		case 0xf4: mouseputc(0xfa); clearmouse(); if((mouse.state&0xf) == MOUSESTREAM) mouse.state |= MOUSEREP; break; /* enable reporting */
		case 0xf2: mouseputc(0xfa); mouseputc(0x00); clearmouse(); break; /* report device id */
		case 0xf0: mouseputc(0xfa); clearmouse(); mouse.state = mouse.state & ~0xf | MOUSEREMOTE; break; /* set remote mode */
		case 0xee: mouseputc(0xfa); clearmouse(); mouse.state |= MOUSEWRAP; break; /* set wrap mode */
		case 0xec: mouseputc(0xfa); clearmouse(); mouse.state &= ~MOUSEWRAP; break; /* reset wrap mode */
		case 0xeb: mouseputc(0xfa); mousepacket(1); break; /* read data */
		case 0xea: mouseputc(0xfa); clearmouse(); mouse.state = mouse.state & ~0xf | MOUSESTREAM; break; /* set stream mode */
		case 0xe9: /* status request */
			mouseputc(0xfa);
			mouseputc(((mouse.state & 0xf) == MOUSEREMOTE) << 6 | ((mouse.state & MOUSEREP) != 0) << 5 | mouse.scaling21 << 4 | (mouse.buttons * 0x111 & 0x142) % 7);
			mouseputc(mouse.res);
			mouseputc(mouse.rate);
			break;
		case 0xe7: mouseputc(0xfa); mouse.scaling21 = 1; break; /* set 2:1 scaling */
		case 0xe6: mouseputc(0xfa); mouse.scaling21 = 0; break; /* set 1:1 scaling */
		default: vmerror("unknown mouse command %#ux", val); mouseputc(0xfc);
		}
	}
	i8042kick(nil);
}

static void
mousekick(void)
{	
	switch(mouse.state){
	case MOUSERESET:
		mouseputc(0xaa);
		mouseputc(0);
		mouse.state = MOUSESTREAM;
		break;
	case MOUSESTREAM | MOUSEREP:
		if(mouse.actcmd == 0)
			mousepacket(0);
		break;
	}
}


void
i8042kick(void *)
{
	ulong ch;
	
	if((i8042.cfg & 0x10) == 0 && i8042.buf == 0)
		if(kbd.bufr != kbd.bufw)
			i8042putbuf(0x100 | kbd.buf[kbd.bufr++ & 63]);
		else if(!kbd.quiet && nbrecv(kbdch, &ch) > 0)
			i8042putbuf(0x100 | (u8int)ch);
	if((i8042.cfg & 0x20) == 0 && i8042.buf == 0){
		if(mouse.bufr == mouse.bufw)
			mousekick();
		if(mouse.bufr != mouse.bufw)
			i8042putbuf(0x200 | mouse.buf[mouse.bufr++ & 63]);
	}
}

static u32int
i8042io(int isin, u16int port, u32int val, int sz, void *)
{
	int rc;

	val = (u8int)val;
	switch(isin << 16 | port){
	case 0x60:
		i8042.stat &= ~8;
		switch(i8042.cmd){
		case 0x60: i8042.cfg = val; break;
		case 0xd1:
			i8042.oport = val;
			irqline(1, i8042.oport >> 4 & 1);
			irqline(12, i8042.oport >> 5 & 1);
			break;
		case 0xd2: i8042putbuf(0x100 | val); break;
		case 0xd3: i8042putbuf(0x200 | val); break;
		case 0xd4: mousecmd(val); break;
		case -1: kbdcmd(val); break;
		}
		i8042.cmd = -1;
		return 0;
	case 0x10060:
		i8042kick(nil);
		rc = i8042.buf;
		i8042putbuf(0);
		return rc;
	case 0x64:
		i8042.stat |= 8;
		switch(val){
		case 0x20: i8042putbuf(0x400 | i8042.cfg); return 0;
		case 0xa1: i8042putbuf(0x4f1); return 0; /* no keyboard password */
		case 0xa7: i8042.cfg |= 1<<5; return 0;
		case 0xa8: i8042.cfg &= ~(1<<5); return 0;
		case 0xa9: i8042putbuf(0x400); return 0; /* test second port */
		case 0xaa: i8042putbuf(0x455); return 0; /* test controller */
		case 0xab: i8042putbuf(0x400); return 0; /* test first port */
		case 0xad: i8042.cfg |= 1<<4; return 0;
		case 0xae: i8042.cfg &= ~(1<<4); return 0;
		case 0xd0: i8042putbuf(0x400 | i8042.oport); return 0;
		case 0x60: case 0xd1: case 0xd2: case 0xd3: case 0xd4:
			i8042.cmd = val;
			return 0;
		}
		vmerror("unknown i8042 command %#ux", val);
		return 0;
	case 0x10064:
		i8042kick(nil);
		return i8042.stat | i8042.cfg & 4;
	}
	return iowhine(isin, port, val, sz, "i8042");
}

typedef struct UART UART;
struct UART {
	u8int ier, fcr, lcr, lsr, mcr, scr, dll, dlh;
	u8int rbr, tbr;
	enum {
		UARTTXIRQ = 1,
		UARTRXIRQ = 2,
	} irq;
	int infd, outfd;
	Channel *inch, *outch;
} uart[2] = { { .lsr = 0x60 }, { .lsr = 0x60 } };

static void
uartkick(UART *p)
{
	char c;

	irqline(4 - (p - uart), (p->irq & p->ier) != 0);
	if((p->irq & UARTRXIRQ) == 0 && p->inch != nil && nbrecv(p->inch, &c) > 0){
		p->rbr = c;
		p->irq |= UARTRXIRQ;
	}
	if((p->lsr & 1<<5) == 0){
		if(p->outch == nil){
			p->lsr |= 3<<5;
			p->irq |= UARTTXIRQ;
		}else if(nbsend(p->outch, &p->tbr) > 0){
			p->tbr = 0;
			p->lsr |= 3<<5;
			p->irq |= UARTTXIRQ;
		}
	}
	irqline(4 - (p - uart), (p->irq & p->ier) != 0);
}

static u32int
uartio(int isin, u16int port, u32int val, int sz, void *)
{
	UART *p;
	int rc;

	if((port & 0xff8) == 0x3f8) p = &uart[0];
	else if((port & 0xff8) == 0x2f8) p = &uart[1];
	else return 0;
	
	val = (u8int) val;
	switch(isin << 4 | port & 7){
	case 0x00:
		if((p->lcr & 1<<7) != 0)
			p->dll = val;
		else{ /* transmit byte */
			if((p->mcr & 1<<4) != 0){
				p->irq |= UARTRXIRQ;
				p->rbr = val;
				p->lsr |= 3<<5;
			}else{
				p->tbr = val;
				p->lsr &= ~(3<<5);
				p->irq &= ~UARTTXIRQ;
			}
			uartkick(p);
		}
		return 0;
	case 0x01:
		if((p->lcr & 1<<7) != 0)
			p->dlh = val;
		else
			p->ier = val & 15;
		return 0;
	case 0x02: p->fcr = val; return 0;
	case 0x03: p->lcr = val; return 0;
	case 0x04: p->mcr = val & 0x1f; return 0;
	case 0x07: p->scr = val; return 0;
	case 0x10:
		if((p->lcr & 1<<7) != 0) return p->dll;
		p->irq &= ~UARTRXIRQ;
		rc = p->rbr;
		uartkick(p);
		return rc;
	case 0x11:
		if((p->lcr & 1<<7) != 0) return p->dlh;
		return p->ier;
	case 0x12:
		rc = 0;
		uartkick(p);
		if((p->irq & UARTRXIRQ) != 0)
			return rc | 4;
		else if((p->irq & UARTTXIRQ) != 0){
			p->irq &= ~UARTTXIRQ;
			uartkick(p);
			return rc | 2;
		}else
			return rc | 1;
	case 0x13: return p->lcr;
	case 0x14: return p->mcr;
	case 0x15:
		uartkick(p);
		rc = p->lsr; /* line status */
		if((p->irq & UARTRXIRQ) != 0)
			rc |= 1;
		return rc;
	case 0x16: /* modem status */
		if((p->mcr & 0x10) != 0)
			return p->mcr << 1 & 2 | p->mcr >> 1 & 1 | p->mcr & 0xc;
		return 0;
	case 0x17: return p->scr;
	}
	return iowhine(isin, port, val, sz, "uart");
}

static void
uartrxproc(void *uv)
{
	UART *u;
	char buf[128], *p;
	int rc;
	
	threadsetname("uart rx");
	u = uv;
	for(;;){
		rc = read(u->infd, buf, sizeof(buf));
		if(rc < 0){
			vmerror("read(uartrx): %r");
			threadexits("read: %r");
		}
		if(rc == 0){
			vmerror("read(uartrx): eof");
			threadexits("read: eof");
		}
		for(p = buf; p < buf + rc; p++){
			send(u->inch, p);
			sendnotif((void(*)(void*))uartkick, u);
		}
	}
}

static void
uarttxproc(void *uv)
{
	UART *u;
	char buf[128], *p;
	
	threadsetname("uart tx");
	u = uv;
	for(;;){
		p = buf;
		recv(u->outch, p);
		p++;
		while(sendnotif((void(*)(void*))uartkick, u), p < buf+sizeof(buf) && nbrecv(u->outch, p) > 0)
			p++;
		if(write(u->outfd, buf, p - buf) < p - buf)
			vmerror("write(uarttx): %r");
	}
}

void
uartinit(int n, char *cfg)
{
	char *p, *infn, *outfn;
	
	p = strchr(cfg, ',');
	if(p == nil){
		infn = cfg;
		outfn = cfg;
	}else{
		*p = 0;
		infn = cfg;
		outfn = p + 1;
	}
	if(infn != nil && *infn != 0){
		uart[n].infd = open(infn, OREAD);
		if(uart[n].infd < 0)
			sysfatal("open: %r");
		uart[n].inch = chancreate(sizeof(char), 256);
		proccreate(uartrxproc, &uart[n], 4096);
	}
	if(outfn != nil && *outfn != 0){
		uart[n].outfd = open(outfn, OWRITE);
		if(uart[n].outfd < 0)
			sysfatal("open: %r");
		uart[n].outch = chancreate(sizeof(char), 256);
		proccreate(uarttxproc, &uart[n], 4096);
	}
}

static u32int
nopio(int, u16int, u32int, int, void *)
{
	return 0;
}

u32int
iowhine(int isin, u16int port, u32int val, int sz, void *mod)
{
	if(isin)
		vmerror("%s%sread from unknown i/o port %#ux ignored (sz=%d)", mod != nil ? mod : "", mod != nil ? ": " : "", port, sz);
	else
		vmerror("%s%swrite to unknown i/o port %#ux ignored (val=%#ux, sz=%d)", mod != nil ? mod : "", mod != nil ? ": " : "", port, val, sz);
	return 0;
}

typedef struct IOHandler IOHandler;
struct IOHandler {
	u16int lo, hi;
	u32int (*io)(int, u16int, u32int, int, void *);
	void *aux;
};

u32int vgaio(int, u16int, u32int, int, void *);
u32int pciio(int, u16int, u32int, int, void *);
IOHandler handlers[] = {
	0x20, 0x21, picio, nil,
	0x40, 0x43, pitio, nil,
	0x70, 0x71, rtcio, nil,
	0xa0, 0xa1, picio, nil,
	0x60, 0x60, i8042io, nil,
	0x64, 0x64, i8042io, nil,
	0x2f8, 0x2ff, uartio, nil,
	0x3b0, 0x3bb, vgaio, nil,
	0x3c0, 0x3df, vgaio, nil,
	0x3f8, 0x3ff, uartio, nil,
	0x4d0, 0x4d1, picio, nil,
	0xcf8, 0xcff, pciio, nil,

	0x061, 0x061, nopio, nil, /* pc speaker */
	0x084, 0x084, nopio, nil, /* dma -- used by openbsd for delay by dummy read */
	0x100, 0x110, nopio, nil, /* elnk3 */
	0x170, 0x177, nopio, nil, /* ide secondary */
	0x1f0, 0x1f7, nopio, nil, /* ide primary */
	0x279, 0x279, nopio, nil, /* isa pnp */
	0x280, 0x28f, nopio, nil, /* 8003 */
	0x2e8, 0x2ef, nopio, nil, /* COM4 */
	0x378, 0x37a, nopio, nil, /* LPT1 */
	0x3e0, 0x3e3, nopio, nil, /* cardbus */
	0x3e8, 0x3ef, nopio, nil, /* COM3 */
	0x3f0, 0x3f7, nopio, nil, /* floppy */
	0x778, 0x77a, nopio, nil, /* LPT1 (ECP) */
	0xa79, 0xa79, nopio, nil, /* isa pnp */
};

u32int
io(int dir, u16int port, u32int val, int size)
{
	IOHandler *h;
	extern PCIBar iobars;
	PCIBar *p;

	for(h = handlers; h < handlers + nelem(handlers); h++)
		if(port >= h->lo && port <= h->hi)
			return h->io(dir, port, val, size, h->aux);
	for(p = iobars.busnext; p != &iobars; p = p->busnext)
		if(port >= p->addr && port < p->addr + p->length)
			return p->io(dir, port - p->addr, val, size, p->aux);
	return iowhine(dir, port, val, size, nil);
}
