#include <u.h>
#include <libc.h>
#include <thread.h>
#include "../eui.h"
#include "dat.h"
#include "fns.h"

u8int ppustate, ppuy;
ulong hblclock, rendclock;
jmp_buf mainjmp, renderjmp;
static int cyc, done, ppux, ppux0;
extern int prish;
extern u32int white;

Var ppuvars[] = {VAR(ppustate), VAR(ppuy), VAR(hblclock), VAR(rendclock), {nil, 0, 0}};

#define ryield() {if(setjmp(renderjmp) == 0) longjmp(mainjmp, 1);}
#define myield() {if(setjmp(mainjmp) == 0) longjmp(renderjmp, 1);}

typedef struct sprite sprite;
struct sprite {
	u8int dy, x, t;
	u8int fetched, pal;
	u16int chr;
};
enum {
	SPRPRI = 0x80,
	SPRYFL = 0x40,
	SPRXFL = 0x20,
	SPRPAL = 0x10,
	SPRBANK = 0x08,
	
	TILCOL0 = 0x01,
	TILPRI = 0x02,
	TILSPR = 0x04,
};
sprite spr[10], *sprm;

void
ppurender(void)
{
	int x, y, i, n, m, win;
	u16int ta, ca, chr;
	u8int tile, attr, pali;
	u32int sr[8], *picp;
	#define eat(nc) if(cyc <= nc){for(i = 0; i < nc; i++) if(--cyc == 0) ryield();} else cyc -= nc;
	
	ryield();
	
	attr = 0;
	for(;;){
		eat(6*2);
		m = 168 + (reg[SCX] & 7);
		win = 0;
		if((reg[LCDC] & WINEN) != 0 && ppuy >= reg[WY] && reg[WX] <= 166)
			if(reg[WX] == 0)
				m = 7;
			else if(reg[WX] == 166)
				m = 0;
			else{
				m = reg[WX] + (reg[SCX] & 7) + 1;
				win = -1;
			}
		ppux = 0;
		ppux0 = 0;
		picp = (u32int*)pic + ppuy * PICW * scale;
		y = ppuy + reg[SCY] << 1 & 14;
		ta = 0x1800 | reg[LCDC] << 7 & 0x400 | ppuy + reg[SCY] << 2 & 0x3e0 | reg[SCX] >> 3;
		x = -(reg[SCX] & 7);
	restart:
		do{
			tile = vram[ta];
			if((mode & COL) != 0)
				attr = vram[8192 + ta];
			if((reg[LCDC] & BGTILE) != 0)
				ca = (tile << 4) + y;
			else
				ca = 0x1000 + ((s32int)(tile << 24) >> 20) + y;
			if((attr & 0x40) != 0)
				ca ^= 14;
			ca |= (attr & 8) << 10;
			chr = vram[ca] << 8 | vram[ca+1];
			pali = attr << 2 & 0x1c;
			if((attr & 0x20) == 0)
				for(i = 0; i < 8; i++){
					sr[i] = pal[pali | chr >> 15 | chr >> 6 & 2] | ((chr & 0x8080) == 0) << prish;
					chr <<= 1;
				}
			else
				for(i = 0; i < 8; i++){
					sr[i] = pal[pali | chr << 1 & 2 | chr >> 8 & 1] | ((chr & 0x0101) == 0) << prish;
					chr >>= 1;
				}
			if((attr & 0x80) != 0)
				for(i = 0; i < 8; i++)
					sr[i] |= 2 << prish;
			if((reg[LCDC] & BGEN) == 0 && (mode & COL) == 0 && ((mode & CGB) != 0 || win == 0))
				for(i = 0; i < 8; i++)
					sr[i] = white;
			if(cyc <= 2*8){
				for(i = 0; i < 2*8; i++)
					if(--cyc == 0)
						ryield();
				y = ppuy + reg[SCY] << 1 & 14;
				ta = 0x1800 | reg[LCDC] << 7 & 0x400 | ppuy + reg[SCY] << 2 & 0x3e0 | ta & 0x1f;
			}else
				cyc -= 2*8;
			m -= 8;
			n = m < 8 ? m : 8;
			if((ta & 0x1f) == 0x1f)
				ta &= ~0x1f;
			else
				ta++;
			for(i = 0; i < n; i++, x++)
				if(x >= 0)
					picp[x] = sr[i];
			ppux = x;
		}while(m > 8);
		if(win == -1){
			win = 1;
			ta = 0x1800 | reg[LCDC] << 4 & 0x400 | ppuy - reg[WY] << 2 & 0x3e0;
			y = ppuy - reg[WY] << 1 & 14;
			cyc += 2;
			m = 175 - reg[WX];
			goto restart;
		}
		done = 1;
		ryield();
	}
}

void
oamsearch(void)
{
	u8int *p;
	sprite *q;
	int y0, sy;
	u8int t, tn;
	u8int *chrp;
	
	y0 = ppuy + 16;
	sy = (reg[LCDC] & SPR16) != 0 ? 16 : 8;
	sprm = spr;
	if((reg[LCDC] & SPREN) == 0)
		return;
	for(p = oam; p < oam + 160; p += 4){
		if((u8int)(y0 - p[0]) >= sy)
			continue;
		if((mode & COL) == 0){
			for(q = spr; q < sprm; q++)
				if(q->x > p[1]){
					if(sprm != spr + 10){
						memmove(q + 1, q, (sprm - q) * sizeof(sprite));
						sprm++;
					}else
						memmove(q + 1, q, (sprm - 1 - q) * sizeof(sprite));
					goto found;
				}
			if(q == spr + 10)
				continue;
			sprm++;
		found:;
		}else
			q = sprm++;
		q->dy = y0 - p[0];
		q->x = p[1];
		q->t = t = p[3];
		if((t & SPRYFL) != 0)
			q->dy ^= sy - 1;
		tn = p[2];
		if(sy == 16)
			tn = tn & ~1 | q->dy >> 3;
		chrp = vram + (tn << 4 | q->dy << 1 & 14);
		if((mode & COL) != 0){
			chrp += t << 10 & 0x2000;
			q->pal = 0x20 | t << 2 & 0x1c;
		}else
			q->pal = 4 + (t >> 2 & 4);
		q->chr = chrp[0] << 8 | chrp[1];
		if(p[1] < 8)
			if((t & SPRXFL) != 0)
				q->chr >>= 8 - p[1];
			else
				q->chr <<= 8 - p[1];
		q->fetched = 0;
		if((mode & COL) != 0 && sprm == spr + 10)
			break;
	}
}

void
sprites(void)
{
	sprite *q;
	u8int attr;
	u32int *picp;
	int x, x1;
	u16int chr;
	
	picp = (u32int*)pic + ppuy * PICW * scale;
	for(q = spr; q < sprm; q++){
		if(q->x <= ppux0 || q->x >= ppux + 8)
			continue;
		x = q->x - 8;
		if(x < ppux0) x = ppux0;
		x1 = q->x;
		if(x1 > ppux) x1 = ppux;
		for(; x < x1; x++){
			attr = picp[x] >> prish;
			chr = q->chr;
			if((chr & ((q->t & SPRXFL) != 0 ? 0x0101 : 0x8080)) != 0 && (attr & TILSPR) == 0 &&
					((mode & COL) != 0 && (reg[LCDC] & BGPRI) == 0 ||
					(attr & TILCOL0) != 0 ||
					(attr & TILPRI) == 0 && (q->t & SPRPRI) == 0))
				if((q->t & SPRXFL) == 0)
					picp[x] = pal[q->pal | chr >> 15 | chr >> 6 & 2] | TILSPR << prish;
				else
					picp[x] = pal[q->pal | chr << 1 & 2 | chr >> 8 & 1] | TILSPR << prish;
			if((q->t & SPRXFL) != 0)
				q->chr >>= 1;
			else
				q->chr <<= 1;
		}
	}
	ppux0 = ppux;
}

void
ppusync(void)
{
	if(ppustate != 3)
		return;
	cyc = clock - rendclock;
	if(cyc != 0)
		myield();
	sprites();
	rendclock = clock;
}

int
linelen(void)
{
	int t;
	
	t = 174 + (reg[SCX] & 7);
	if((reg[LCDC] & WINEN) != 0 && ppuy >= reg[WY] && reg[WX] < 166)
		if(reg[WX] == 0)
			t += 7;
		else
			t += 6;
	return t*2;
}

static void
lineexpand(void)
{
	u32int *picp, *p, *q, l;
	int i;

	picp = (u32int*)pic + ppuy * PICW * scale;
	p = picp + PICW;
	q = picp + PICW * scale;
	for(i = PICW; --i >= 0; ){
		l = *--p;
		switch(scale){
		case 16: *--q = l;
		case 15: *--q = l;
		case 14: *--q = l;
		case 13: *--q = l;
		case 12: *--q = l;
		case 11: *--q = l;
		case 10: *--q = l;
		case 9: *--q = l;
		case 8: *--q = l;
		case 7: *--q = l;
		case 6: *--q = l;
		case 5: *--q = l;
		case 4: *--q = l;
		case 3: *--q = l;
		case 2: *--q = l;
		case 1: *--q = l;
		}
	}
}

void
hblanktick(void *)
{
	extern Event evhblank;
	int t;
	
	switch(ppustate){
	case 0:
		hblclock = clock + evhblank.time;
		if(++ppuy == 144){
			ppustate = 1;
			if((reg[STAT] & IRQM1) != 0)
				reg[IF] |= IRQLCDS;
			addevent(&evhblank, 456*2);
			reg[IF] |= IRQVBL;
			flush();
		}else{
			ppustate = 2;
			if((reg[STAT] & IRQM2) != 0)
				reg[IF] |= IRQLCDS;
			addevent(&evhblank, 80*2);
		}
		if((reg[STAT] & IRQLYC) != 0 && ppuy == reg[LYC])
			reg[IF] |= IRQLCDS;
		break;
	case 1:
		hblclock = clock + evhblank.time;
		if(++ppuy == 154){
			ppuy = 0;
			ppustate = 2;
			if((reg[STAT] & IRQM2) != 0)
				reg[IF] |= IRQLCDS;
			addevent(&evhblank, 80*2);
		}else
			addevent(&evhblank, 456*2);
		if((reg[STAT] & IRQLYC) != 0 && ppuy == reg[LYC])
			reg[IF] |= IRQLCDS;
		break;
	case 2:
		oamsearch();
		rendclock = clock + evhblank.time;
		ppustate = 3;
		addevent(&evhblank, linelen());
		break;
	case 3:
		ppusync();
		if(!done) print("not done?!\n");
		done = 0;
		if(scale > 1)
			lineexpand();
		ppustate = 0;
		if((reg[STAT] & IRQM0) != 0)
			reg[IF] |= IRQLCDS;
		t = hblclock + 456 * 2 - clock;
		addevent(&evhblank, t < 0 ? 456 * 2 : t);
		if(dma < 0)
			dma = 1;
		break;
	}
}

void
ppuinit(void)
{
	static char ppustack[4096];
	
	renderjmp[JMPBUFPC] = (uintptr)ppurender;
	renderjmp[JMPBUFSP] = (uintptr)(ppustack + sizeof(ppustack) - 64);
	myield();
}
