#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <bio.h>
#include <mouse.h>
#include "dat.h"
#include "fns.h"

u32int irq;
u32int irql[8] = {
	[1] INTVBL,
	[2] INTKEY,
	[4] INTMOUSE,
	[5] INTUART,
};
int diag;

ushort ram[128*1024];
ushort rom[3*4096];
Channel *keych;
Channel *uartrxch, *uarttxch;
int mousex, mousey, mousebut;

int yes;
u8int kbdctrl, uartctrl;
enum {
	ACIATXMASK = 0x60,
	ACIATXIRQ = 0x20,
	ACIARXIRQ = 0x80,
};

int keybuf = -1;
int uartrxbuf = -1;
int uarttxbuf = -1;

void
meminit(void)
{
	int i, x;
	Biobuf *bp;
	char *s;
	ushort *p, *q;
	
	p = rom;
	if(diag){
		bp = Bopen("/sys/lib/blit/diagbits", OREAD);
		if(bp == nil) sysfatal("Bopen: %r");
		Bread(bp, rom, sizeof(rom));
		Bterm(bp);
		return;
	}
	for(i = 0; i < 6; i++){
		bp = Bopen(smprint("/sys/lib/blit/rom%d", i), OREAD);
		if(bp == nil) sysfatal("Bopen: %r");
		q = p;
		for(;;){
			s = Brdline(bp, '\n');
			if(s == nil || Blinelen(bp) == 0) break;
			s[Blinelen(bp) - 1] = 0;
			x = strtol(s, nil, 8);
			if((i & 1) != 0)
				*p |= x << 8;
			else
				*p |= x;
			p++;
		}
		if((i & 1) == 0) p = q;
		Bterm(bp);
	}
}

void
keycheck(void)
{
	yield();

	if(keybuf < 0)
		nbrecv(keych, &keybuf);
	if(keybuf >= 0 && (kbdctrl & ACIARXIRQ) != 0)
		irq |= INTKEY;
	else
		irq &= ~INTKEY;
	
	if(uartrxbuf < 0 && uartrxctr <= 0){
		nbrecv(uartrxch, &uartrxbuf);
		uartrxctr = FREQ * 11 / baud;
	}
	if(uarttxbuf >= 0 && nbsend(uarttxch, &uarttxbuf) > 0)
		uarttxbuf = -1;
	if(uartrxbuf >= 0 && (uartctrl & ACIARXIRQ) != 0 || uarttxbuf < 0 && (uartctrl & ACIATXMASK) == ACIATXIRQ)
		irq |= INTUART;
	else
		irq &= ~INTUART;
}

u16int
memread(u32int a)
{
	int rc;

	a &= 0x3fffff;
	if(a < 8) a += 0x40000;
	if(a < 0x40000) return ram[a/2];
	if(a >= 0x40000 && a < 0x40000 + sizeof(rom))
		return rom[(a - 0x40000)/2];
	switch(a & ~1){
	case 01400000: return mousey;
	case 01400002: return mousex;
	case 01400010: /* uart status */
		rc = 0;
		if(uartrxbuf >= 0) rc |= 1;
		if(uarttxbuf < 0) rc |= 2;
		return rc | rc << 8;
	case 01400012: /* uart data */
		rc = uartrxbuf;
		uartrxbuf = -1;
		yes=1;
		return rc | rc << 8;
	case 01400020:
	case 01400024:
		irq &= ~INTMOUSE;
		return mousebut | mousebut << 8;
	case 01400026: return 0; /* mouse: unknown purpose */
	case 01400030: return daddr >> 2; /* display address */
	case 01400040: return dstat; /* display status */
	case 01400060: /* keyboard status */
		rc = 2;
		if(keybuf >= 0) rc |= 1;
		return rc | rc << 8;
	case 01400062: /* keyboard data */
		rc = keybuf;
		keybuf = -1;
		return rc | rc << 8;
	}
	print("read %.8o (curpc = %.6x)\n", a, curpc & 0x3fffff);
	return 0;
}

void
memwrite(u32int a, u16int v, u16int m)
{
	extern Rectangle updated;
	int x, y;

	a &= 0x3fffff;
	if(a < 0x40000){
		if(a >= daddr){
			y = (a - daddr) / 100;
			x = (((a & ~1) - daddr) % 100) * 8;
			if(updated.min.x > x) updated.min.x = x;
			if(updated.max.x < x+16) updated.max.x = x+16;
			if(updated.min.y > y) updated.min.y = y;
			if(updated.max.y <= y) updated.max.y = y+1;
		}
		ram[a/2] = ram[a/2] & ~m | v & m;
		return;
	}
	switch(a & ~1){
	case 01400010: uartctrl = v; return;
	case 01400012: uarttxbuf = (uchar) v; return;
	case 01400024: return; /* mouse: purpose unknown */
	case 01400026: return; /* mouse: purpose unknown */
	case 01400030: daddr = ((daddr >> 2) & ~m | v & m) << 2; updated = Rect(0, 0, SX, SY); return;
	case 01400040: dstat = dstat & ~m | v & m; invert = -(dstat & 1); updated = Rect(0, 0, SX, SY); return;
	case 01400056: /* sound; exact function unknown */ return;
	case 01400060: kbdctrl = v; return;
	case 01400062: /* reset keyboard */ return;
	case 01400070: irq &= ~INTVBL; return;
	case 01400156: /* sound; exact function unknown */ return;
	}
	print("write %.8o = %.4x (mask = %.4x, curpc = %.6x)\n", a, v, m, curpc & 0x3fffff);
}

int
intack(int l)
{
	return 24+l;
}
