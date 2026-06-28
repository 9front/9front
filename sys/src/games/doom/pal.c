#include <u.h>
#include <libc.h>

void
pal2xrgb(u32int *pal, u8int *s, u32int *d, int n)
{
	while(n-- > 0)
		*d++ = pal[*s++];
}
