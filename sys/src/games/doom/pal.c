#include <u.h>
#include <libc.h>

void
pal2xrgb(u32int *pal, u8int *s, u32int *d, int n, int scale)
{
	int i;
	u32int c;

	while(n-- > 0){
		c = pal[*s++];
		for(i = 0; i < scale; i++)
			*d++ = c;
	}
}
