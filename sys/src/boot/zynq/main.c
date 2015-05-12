#include <u.h>
#include <a.out.h>
#include "dat.h"
#include "fns.h"

void
puts(char *s)
{
	while(*s)
		putc(*s++);
}

void
puthex(u32int u)
{
	static char *dig = "0123456789abcdef";
	int i;
	
	for(i = 0; i < 8; i++){
		putc(dig[u >> 28]);
		u <<= 4;
	}
}

void
putdec(int n)
{
	if(n / 10 != 0)
		putdec(n / 10);
	putc(n % 10 + '0');
}

void
print(char *s, ...)
{
	va_list va;
	int n;
	u32int u;
	
	va_start(va, s);
	while(*s)
		if(*s == '%'){
			switch(*++s){
			case 's':
				puts(va_arg(va, char *));
				break;
			case 'x':
				puthex(va_arg(va, u32int));
				break;
			case 'd':
				n = va_arg(va, int);
				if(n < 0){
					putc('-');
					putdec(-n);
				}else
					putdec(n);
				break;
			case 'I':
				u = va_arg(va, u32int);
				putdec(u >> 24);
				putc('.');
				putdec((uchar)(u >> 16));
				putc('.');
				putdec((uchar)(u >> 8));
				putc('.');
				putdec((uchar)u);
				break;
			case 0:
				va_end(va);
				return;
			}
			s++;
		}else
			putc(*s++);			
	va_end(va);
}

void
memset(void *v, char c, int n)
{
	char *vc;
	
	vc = v;
	while(n--)
		*vc++ = c;
}

void
memcpy(void *d, void *s, int n)
{
	char *cd, *cs;
	
	cd = d;
	cs = s;
	while(n--)
		*cd++ = *cs++;
}

u32int
u32get(void *pp)
{
	uchar *p;
	
	p = pp;
	return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

uchar *
u32put(uchar *p, u32int v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
	return p + 4;
}

void
run(void)
{
	ulong t, tr;
	char *p, *d;
	int n;
	ulong *h;

	h = (ulong *) TZERO;
	if(u32get(&h[0]) != E_MAGIC){
		print("invalid magic: %x != %x\n", u32get(&h[0]), E_MAGIC);
		return;
	}
	t = u32get(&h[1]) + 0x20;
	tr = t + 0xfff & ~0xfff;
	if(t != tr){
		n = u32get(&h[2]);
		p = (char *) (TZERO + t + n);
		d = (char *) (TZERO + tr + n);
		while(n--)
			*--d = *--p;
	}
	p = (char *) (TZERO + tr + u32get(&h[2]));
	memset(p, 0, u32get(&h[3]));
	jump((void *) (u32get(&h[5]) & 0xfffffff));
}

enum {
	TIMERVALL,
	TIMERVALH,
	TIMERCTL,
	TIMERSTAT,
	TIMERCOMPL,
	TIMERCOMPH,
};

void
timeren(int n)
{
	ulong *r;
	
	r = (ulong *) 0xf8f00200;
	if(n < 0){
		r[TIMERSTAT] |= 1;
		r[TIMERCTL] = 0;
		return;
	}
	r[TIMERCTL] = 0;
	r[TIMERVALL] = 0;
	r[TIMERVALH] = 0;
	r[TIMERCOMPL] = 1000 * n;
	r[TIMERCOMPH] = 0;
	r[TIMERSTAT] |= 1;
	r[TIMERCTL] = 100 << 8 | 3;
}

int
timertrig(void)
{
	ulong *r;
	
	r = (ulong *) 0xf8f00200;
	if((r[TIMERSTAT] & 1) != 0){
		r[TIMERCTL] = 0;
		r[TIMERSTAT] |= 1;
		return 1;
	}
	return 0;
}

void
sleep(int n)
{
	timeren(n);
	while(!timertrig())
		;
}

void
main(void)
{
	puts("Booting ...\n");
	if(mmcboot() > 0 || netboot() > 0)
		run();
	print("hjboot: ending\n");
}
