#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

extern ulong *uart;

void
uartputs(char *s, int n)
{
	for(; n--; s++){
		while(uart[17] & 1)
			;
		uart[0] = *s;
	}
}
